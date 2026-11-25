/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_fill.h"
#include "st_rip3_draw_internal.h"

#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int
st_rip3_fill_custom(struct st_rip3_fill *fill, const unsigned rows[8],
    const struct st_rip3_point *origin)
{
	struct st_rip3_fill value;

	if (fill == NULL || rows == NULL || origin == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 8; i++) {
		if (rows[i] > 255)
			return IO_ERROR_OUT_OF_RANGE;
		value.rows[i] = (uint8_t)rows[i];
	}
	value.origin = *origin;
	*fill = value;
	return 0;
}

int
st_rip3_fill_solid(struct st_rip3_fill *fill, bool foreground)
{
	if (fill == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < 8; i++)
		fill->rows[i] = foreground ? 255 : 0;
	fill->origin = (struct st_rip3_point){0, 0};
	return 0;
}

static struct st_rip3_rect
normalize(struct st_rip3_rect area)
{
	int32_t swap;

	if (area.x0 > area.x1) {
		swap = area.x0;
		area.x0 = area.x1;
		area.x1 = swap;
	}
	if (area.y0 > area.y1) {
		swap = area.y0;
		area.y0 = area.y1;
		area.y1 = swap;
	}
	return area;
}

static int
fill_area(struct st_rip3_ports *ports, struct st_rip3_rect area,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	unsigned current;
	int status;

	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0)
		return status;
	if ((!info.viewport_active || info.clip.x1 < 0 || info.clip.y1 < 0))
		return 0;
	if (area.x0 < 0)
		area.x0 = 0;
	if (area.y0 < 0)
		area.y0 = 0;
	if (area.x1 > info.clip.x1)
		area.x1 = info.clip.x1;
	if (area.y1 > info.clip.y1)
		area.y1 = info.clip.y1;
	for (int32_t y = area.y0; y <= area.y1; y++) {
		/* Unsigned modular differences give Euclidean phase modulo eight,
		 * even for negative or extreme origins, without signed overflow. */
		unsigned row = ((uint32_t)y - (uint32_t)fill->origin.y) & 7;

		for (int32_t x = area.x0; x <= area.x1; x++) {
			unsigned col = ((uint32_t)x - (uint32_t)fill->origin.x) & 7;
			bool on = (fill->rows[row] & (UINT8_C(0x80) >> col)) != 0;

			status = st_rip3_ports_plot(ports, x, y,
			    on ? foreground : background, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}

int
st_rip3_draw_bar(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
    enum st_rip3_write_mode mode)
{
	if (ports == NULL || area == NULL || fill == NULL
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	return fill_area(ports, normalize(*area), fill, foreground, background, mode);
}

int
st_rip3_draw_filled_rectangle(struct st_rip3_ports *ports,
    const struct st_rip3_rect *area, const struct st_rip3_fill *fill,
    uint32_t foreground, uint32_t background, const struct st_rip3_pen *border,
    uint32_t border_color, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect rect;
	struct st_rip3_pen outline;
	struct st_rip3_point corners[4];
	int status;

	if (ports == NULL || area == NULL || fill == NULL
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Validate before the fill. Border patterns are deliberately ignored. */
	if (border != NULL) {
		status = st_rip3_pen_set(&outline, false, 0, 0, border->thickness);
		if (status != 0)
			return status;
	}
	rect = normalize(*area);
	if (rect.x0 != rect.x1 && rect.y0 != rect.y1) {
		struct st_rip3_rect interior = rect;

		interior.x1--;
		interior.y1--;
		status = fill_area(ports, interior, fill, foreground, background, mode);
		if (status != 0)
			return status;
	}
	if (border == NULL)
		return 0;
	corners[0] = (struct st_rip3_point){rect.x0, rect.y0};
	corners[1] = (struct st_rip3_point){rect.x1, rect.y0};
	corners[2] = (struct st_rip3_point){rect.x1, rect.y1};
	corners[3] = (struct st_rip3_point){rect.x0, rect.y1};
	if (rect.x0 == rect.x1 || rect.y0 == rect.y1)
		return st_rip3_draw_line(ports, &corners[0], &corners[2], &outline,
		    border_color, background, ST_RIP3_COPY);
	for (unsigned i = 0; i < 4; i++) {
		status = st_rip3_draw_line(ports, &corners[i], &corners[(i + 1) % 4],
		    &outline, border_color, background, ST_RIP3_COPY);
		if (status != 0)
			return status;
	}
	return 0;
}

static int
intersection_compare(const void *a, const void *b)
{
	double left = *(const double *)a;
	double right = *(const double *)b;

	return (left > right) - (left < right);
}

struct polygon_edge {
	int32_t first_y;
	int32_t last_y;
	int64_t p;
	int64_t q;
	int64_t c;
};

static struct polygon_edge
polygon_edge(const struct st_rip3_point *a, const struct st_rip3_point *b)
{
	int64_t dx, dy, determinant;
	uint64_t gcd, remainder;

	if (a->y > b->y) {
		const struct st_rip3_point *swap = a;

		a = b;
		b = swap;
	}
	dx = (int64_t)b->x - a->x;
	dy = (int64_t)b->y - a->y;
	gcd = dx < 0 ? (uint64_t)-dx : (uint64_t)dx;
	remainder = (uint64_t)dy;
	while (remainder != 0) {
		uint64_t next = gcd % remainder;

		gcd = remainder;
		remainder = next;
	}
	/* Each product of signed 32-bit coordinates fits int64_t, and their
	 * determinant lies strictly inside +/-2^63. Reduced coefficients make
	 * reversed/subdivided collinear edges evaluate identically at a scanline. */
	determinant = (int64_t)a->x * b->y - (int64_t)a->y * b->x;
	return (struct polygon_edge){a->y, b->y, dx / (int64_t)gcd,
	    dy / (int64_t)gcd, determinant / (int64_t)gcd};
}

int
st_rip3_draw_filled_polygons(struct st_rip3_ports *ports,
    const struct st_rip3_contour *contours, size_t count,
    const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
    const struct st_rip3_pen *border, uint32_t border_color,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	struct st_rip3_stroke_mask outline = {0};
	struct st_rip3_pen checked_border;
	struct polygon_edge *edges = NULL;
	double min_y, max_y, start_y, end_y;
	double *intersections = NULL;
	size_t vertices;
	size_t edge_count = 0;
	unsigned current;
	int32_t max_x;
	int status;

	if (ports == NULL || fill == NULL || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_contours_validate(contours, count, &vertices);
	if (status != 0)
		return status;
	if (border != NULL) {
		status = st_rip3_pen_set(&checked_border, border->draw_background,
		    4, border->pattern, border->thickness);
		if (status != 0)
			return status;
	}
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || (!info.viewport_active || info.clip.x1 < 0 || info.clip.y1 < 0))
		return status;
	min_y = max_y = contours[0].points[0].y;
	for (size_t c = 0; c < count; c++) {
		const struct st_rip3_point *points = contours[c].points;
		size_t length = contours[c].count;

		for (size_t i = 0; i < length; i++) {
			min_y = fmin(min_y, points[i].y);
			max_y = fmax(max_y, points[i].y);
			if (points[i].y != points[(i + 1) % length].y)
				edge_count++;
		}
	}
	start_y = fmax(min_y, 0);
	end_y = fmin(max_y - 1, info.clip.y1);
	max_x = info.clip.x1;
	if (start_y <= end_y && edge_count != 0) {
		size_t index = 0;

		if (edge_count > SIZE_MAX / sizeof(*intersections)
		    || edge_count > SIZE_MAX / sizeof(*edges))
			return IO_ERROR_OUT_OF_RANGE;
		intersections = malloc(edge_count * sizeof(*intersections));
		edges = malloc(edge_count * sizeof(*edges));
		if (intersections == NULL || edges == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto done;
		}
		for (size_t c = 0; c < count; c++) {
			const struct st_rip3_point *points = contours[c].points;
			size_t length = contours[c].count;

			for (size_t i = 0; i < length; i++) {
				const struct st_rip3_point *a = &points[i];
				const struct st_rip3_point *b = &points[(i + 1) % length];

				if (a->y != b->y)
					edges[index++] = polygon_edge(a, b);
			}
		}
	}
	/* No allocation may fail after the first interior pixel changes. */
	if (border != NULL) {
		status = st_rip3_stroke_collect(ports, contours, count, true,
		    &checked_border, &outline);
		if (status != 0)
			goto done;
	}
	if (edges != NULL) {
		for (int32_t y = (int32_t)start_y; y <= (int32_t)end_y; y++) {
			double sample_y = (double)y + 0.5;
			size_t found = 0;

			for (size_t i = 0; i < edge_count; i++) {
				const struct polygon_edge *edge = &edges[i];

				if (edge->first_y <= sample_y && edge->last_y > sample_y)
					intersections[found++] =
					    ((double)edge->p * sample_y + (double)edge->c)
					    / (double)edge->q;
			}
			qsort(intersections, found, sizeof(*intersections), intersection_compare);
			for (size_t i = 0; i + 1 < found; i += 2) {
				double first = ceil(intersections[i] - 0.5);
				double last = ceil(intersections[i + 1] - 0.5) - 1;
				struct st_rip3_rect span;

				if (first > max_x || last < 0 || first > last)
					continue;
				span = (struct st_rip3_rect){(int32_t)fmax(first, 0), y,
				    (int32_t)fmin(last, max_x), y};
				status = fill_area(ports, span, fill, foreground, background, mode);
				if (status != 0)
					goto done;
			}
		}
	}
	status = st_rip3_stroke_apply(ports, &outline, border_color, background, ST_RIP3_COPY);
done:
	st_rip3_stroke_free(&outline);
	free(edges);
	free(intersections);
	return status;
}
