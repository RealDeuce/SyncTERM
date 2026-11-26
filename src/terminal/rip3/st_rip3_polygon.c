/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "io_error.h"
#include "st_rip3_polygon_internal.h"
#include "st_rip3_ports.h"

/* Edge determinants can exceed int64_t although their visible crossings fit
 * device coordinates. These private magnitude operations retain exact integral
 * cancellation; they do not require compiler extensions or long-double width.
 * Inputs are bounded by the coordinate/viewport contracts below. */
struct edge_integer {
	uint64_t high, low;
	bool     negative;
};

static struct edge_integer
edge_product(int64_t a, int64_t b)
{
	uint64_t            x      = (uint64_t)(a < 0 ? -a : a);
	uint64_t            y      = (uint64_t)(b < 0 ? -b : b);
	uint64_t            first  = (x & UINT32_MAX) * (y & UINT32_MAX);
	uint64_t            middle = (x >> 32) * (y & UINT32_MAX) + (first >> 32);
	uint64_t            last   = (middle & UINT32_MAX) + (x & UINT32_MAX) * (y >> 32);
	struct edge_integer result = {
	    .high     = (x >> 32) * (y >> 32) + (middle >> 32) + (last >> 32),
	    .low      = (last << 32) | (first & UINT32_MAX),
	    .negative = (a < 0) != (b < 0),
	};
	if (result.high == 0 && result.low == 0)
		result.negative = false;
	return result;
}

static int
edge_magnitude_compare(struct edge_integer a, struct edge_integer b)
{
	if (a.high != b.high)
		return a.high > b.high ? 1 : -1;
	return (a.low > b.low) - (a.low < b.low);
}

static struct edge_integer
edge_negate(struct edge_integer value)
{
	if (value.high != 0 || value.low != 0)
		value.negative = !value.negative;
	return value;
}

static struct edge_integer
edge_add(struct edge_integer a, struct edge_integer b)
{
	struct edge_integer result;
	if (a.negative == b.negative) {
		result.low      = a.low + b.low;
		result.high     = a.high + b.high + (result.low < a.low);
		result.negative = a.negative;
	}
	else {
		if (edge_magnitude_compare(a, b) < 0) {
			struct edge_integer swap = a;
			a                        = b;
			b                        = swap;
		}
		result.low      = a.low - b.low;
		result.high     = a.high - b.high - (a.low < b.low);
		result.negative = a.negative;
	}
	if (result.high == 0 && result.low == 0)
		result.negative = false;
	return result;
}

struct polygon_edge {
	int64_t             first_y, last_y, dx, dy;
	struct edge_integer twice_determinant;
};

static struct polygon_edge
make_edge(const struct st_rip3_vector *a, const struct st_rip3_vector *b)
{
	if (a->y > b->y) {
		const struct st_rip3_vector *swap = a;
		a                                 = b;
		b                                 = swap;
	}
	int64_t             ax = (int64_t)a->x, ay = (int64_t)a->y;
	int64_t             bx = (int64_t)b->x, by = (int64_t)b->y;
	struct edge_integer determinant = edge_add(edge_product(ax, by), edge_negate(edge_product(ay, bx)));
	determinant.high                = (determinant.high << 1) | (determinant.low >> 63);
	determinant.low <<= 1;
	return (struct polygon_edge) {ay, by, bx - ax, by - ay, determinant};
}

/* ceil(intersection_x - 1/2), clipped to [0,limit]. At scanline y+1/2 its
 * numerator is 2*det + dx*(2*y+1) - dy, with positive denominator 2*dy.
 * Sorting these pixel boundaries preserves parity without rounding a rational
 * crossing through floating point. Coincident/subdivided edges agree exactly. */
static int32_t
edge_boundary(const struct polygon_edge *edge, int32_t y, int32_t limit)
{
	struct edge_integer numerator =
	    edge_add(edge->twice_determinant, edge_product(edge->dx, 2 * (int64_t)y + 1));
	struct edge_integer denominator = {.low = (uint64_t)edge->dy, .negative = true};
	numerator                       = edge_add(numerator, denominator);
	if (numerator.negative || (numerator.high == 0 && numerator.low == 0))
		return 0;
	int64_t divisor = edge->dy * 2;
	if (edge_magnitude_compare(numerator, edge_product(divisor, limit)) >= 0)
		return limit;
	if (numerator.high == 0)
		return (int32_t)(numerator.low / (uint64_t)divisor + (numerator.low % (uint64_t)divisor != 0));
	/* Large offscreen viewports may need a quotient beyond the 64-bit
	 * numerator fast path. Its clipped range is at most INT32_MAX. */
	int32_t low = 0, high = limit;
	while (low < high) {
		int32_t middle = low + (high - low) / 2;
		if (edge_magnitude_compare(edge_product(divisor, middle), numerator) >= 0)
			high = middle;
		else
			low = middle + 1;
	}
	return low;
}

static int
boundary_compare(const void *first, const void *second)
{
	int32_t a = *(const int32_t *)first, b = *(const int32_t *)second;
	return (a > b) - (a < b);
}

static int
fill_span(struct st_rip3_ports *ports, struct st_rip3_rect span, const struct st_rip3_fill *fill,
    uint32_t foreground, uint32_t background, enum st_rip3_write_mode mode, const struct st_rip3_stroke_mask *omit)
{
	if (omit != NULL && omit->coverage != NULL && span.y0 >= omit->bounds.y0 && span.y0 <= omit->bounds.y1) {
		const uint8_t *row   = omit->coverage + (size_t)(span.y0 - omit->bounds.y0) * omit->width;
		int32_t        first = span.x0 > omit->bounds.x0 ? span.x0 : omit->bounds.x0;
		int32_t        last  = span.x1 < omit->bounds.x1 ? span.x1 : omit->bounds.x1;
		for (int32_t x = first; x <= last; x++) {
			if (row[x - omit->bounds.x0] == 0)
				continue;
			if (x > span.x0) {
				struct st_rip3_rect run = {span.x0, span.y0, x - 1, span.y1};
				int status = st_rip3_draw_bar(ports, &run, fill, foreground, background, mode);
				if (status != 0)
					return status;
			}
			span.x0 = x + 1;
		}
	}
	if (span.x0 > span.x1)
		return 0;
	return st_rip3_draw_bar(ports, &span, fill, foreground, background, mode);
}

static int
fill_contours(struct st_rip3_ports *ports, const struct st_rip3_vector_contour *contours, size_t count,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background, const struct st_rip3_pen *border,
    uint32_t border_color, enum st_rip3_write_mode mode, bool marker)
{
	struct st_rip3_port_info   info;
	struct st_rip3_stroke_mask outline = {0};
	struct st_rip3_pen         checked_border;
	struct polygon_edge       *edges      = NULL;
	int32_t                   *boundaries = NULL;
	size_t                     edge_count = 0, vertices = 0;
	int64_t                    first_y, last_y;
	unsigned                   current;
	int                        status;

	if (ports == NULL || contours == NULL || fill == NULL || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (count == 0 || count > 4096)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t c = 0; c < count; c++) {
		if (contours[c].points == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		if (contours[c].count == 0 || contours[c].count > 4096 - vertices)
			return IO_ERROR_OUT_OF_RANGE;
		vertices += contours[c].count;
		for (size_t i = 0; i < contours[c].count; i++) {
			const struct st_rip3_vector *point = contours[c].points + i;
			if (!isfinite(point->x) || !isfinite(point->y) || fabs(point->x) > 1024.0 * INT32_MAX
			    || fabs(point->y) > 1024.0 * INT32_MAX || trunc(point->x) != point->x
			    || trunc(point->y) != point->y)
				return IO_ERROR_OUT_OF_RANGE;
		}
	}
	if (border != NULL) {
		status = st_rip3_pen_set(&checked_border, border->draw_background, 4, border->pattern,
		    border->thickness);
		if (status != 0)
			return status;
	}
	status = st_rip3_ports_current(ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || (!info.viewport_active || info.clip.x1 < 0 || info.clip.y1 < 0))
		return status;
	first_y = last_y = (int64_t)contours[0].points[0].y;
	for (size_t c = 0; c < count; c++)
		for (size_t i = 0; i < contours[c].count; i++) {
			const struct st_rip3_vector *points = contours[c].points;
			int64_t                      y      = (int64_t)points[i].y;
			if (y < first_y)
				first_y = y;
			if (y > last_y)
				last_y = y;
			if (y != (int64_t)points[(i + 1) % contours[c].count].y)
				edge_count++;
		}
	if (first_y < 0)
		first_y = 0;
	if (last_y > info.clip.y1 + 1)
		last_y = info.clip.y1 + 1;
	if (first_y < last_y && edge_count != 0) {
		if (edge_count > SIZE_MAX / sizeof(*edges) || edge_count > SIZE_MAX / sizeof(*boundaries))
			return IO_ERROR_OUT_OF_RANGE;
		edges      = malloc(edge_count * sizeof(*edges));
		boundaries = malloc(edge_count * sizeof(*boundaries));
		if (edges == NULL || boundaries == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto done;
		}
		size_t next = 0;
		for (size_t c = 0; c < count; c++)
			for (size_t i = 0; i < contours[c].count; i++) {
				const struct st_rip3_vector *a = contours[c].points + i;
				const struct st_rip3_vector *b = contours[c].points + (i + 1) % contours[c].count;
				if (a->y != b->y)
					edges[next++] = make_edge(a, b);
			}
	}
	if (border != NULL) {
		status = st_rip3_stroke_vector_contours(ports, contours, count, true, &checked_border, &outline);
		if (status != 0)
			goto done;
	}
	/* An opaque marker pen owns its covered pixels, including line gaps.
	 * The pen and brush each apply W once to their respective destination. */
	const struct st_rip3_stroke_mask *omit =
	    marker && border->draw_background && mode != ST_RIP3_COPY ? &outline : NULL;
	/* All fallible preparation is complete before any interior pixel changes. */
	if (edges != NULL) {
		int32_t limit = info.clip.x1 + 1;
		for (int32_t y = (int32_t)first_y; y < last_y; y++) {
			size_t found = 0;
			for (size_t i = 0; i < edge_count; i++) {
				if (edges[i].first_y <= y && edges[i].last_y > y)
					boundaries[found++] = edge_boundary(edges + i, y, limit);
			}
			qsort(boundaries, found, sizeof(*boundaries), boundary_compare);
			for (size_t i = 0; i + 1 < found; i += 2) {
				if (boundaries[i] == boundaries[i + 1])
					continue;
				struct st_rip3_rect span = {boundaries[i], y, boundaries[i + 1] - 1, y};
				status = fill_span(ports, span, fill, foreground, background, mode, omit);
				if (status != 0)
					goto done;
			}
		}
	}
	status = st_rip3_stroke_apply(ports, &outline, border_color, background, marker ? mode : ST_RIP3_COPY);
done:
	st_rip3_stroke_free(&outline);
	free(boundaries);
	free(edges);
	return status;
}

int
st_rip3_polygon_fill_contours(struct st_rip3_ports *ports, const struct st_rip3_vector_contour *contours,
    size_t count, const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
    const struct st_rip3_pen *border, uint32_t border_color, enum st_rip3_write_mode mode)
{
	return fill_contours(ports, contours, count, fill, foreground, background, border, border_color, mode,
	    false);
}

int
st_rip3_polygon_marker_fill(struct st_rip3_ports *ports, const struct st_rip3_vector *points, size_t count,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background, const struct st_rip3_pen *pen,
    uint32_t draw_color, enum st_rip3_write_mode mode)
{
	if (pen == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	const struct st_rip3_vector_contour contour = {points, count};
	return fill_contours(ports, &contour, 1, fill, foreground, background, pen, draw_color, mode, true);
}

int
st_rip3_polygon_fill(struct st_rip3_ports *ports, const struct st_rip3_vector *points, size_t count,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background, const struct st_rip3_pen *border,
    uint32_t border_color, enum st_rip3_write_mode mode)
{
	const struct st_rip3_vector_contour contour = {points, count};
	return st_rip3_polygon_fill_contours(ports, &contour, 1, fill, foreground, background, border,
	    border_color, mode);
}
