/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_draw.h"
#include "st_rip3_draw_internal.h"

#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int
st_rip3_pen_set(struct st_rip3_pen *pen, bool draw_background,
    unsigned style, uint16_t custom, uint32_t thickness)
{
	static const uint16_t patterns[] = {0xffff, 0x3333, 0x1e3f, 0x1f1f};
	struct st_rip3_pen value;

	if (pen == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (style > 4 || thickness == 0 || thickness > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (style == 4 && custom == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	value.pattern = style == 4 ? custom : patterns[style];
	value.thickness = thickness;
	value.draw_background = draw_background;
	*pen = value;
	return 0;
}

int
st_rip3_pen_init(struct st_rip3_pen *pen)
{
	return st_rip3_pen_set(pen, false, 0, 0, 1);
}

struct stroke_target {
	struct st_rip3_ports *ports;
	struct st_rip3_rect clip;
	bool active;
	uint8_t foreground;
	uint8_t background;
	enum st_rip3_write_mode mode;
	/* NULL emits directly for a single segment. Paths collect coverage so
	 * adjacent segments cannot apply XOR twice at a join or crossing. */
	uint8_t *coverage;
	size_t width;
	size_t height;
};

static int
stroke_init(struct stroke_target *target, struct st_rip3_ports *ports,
    const struct st_rip3_pen *pen, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	unsigned current;
	int status;

	if (ports == NULL || pen == NULL || pen->pattern == 0
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (pen->thickness == 0 || pen->thickness > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0)
		return status;
	*target = (struct stroke_target){
		.ports = ports,
		.clip = {0, 0, info.viewport.x1 - info.viewport.x0,
		    info.viewport.y1 - info.viewport.y0},
		.active = info.viewport_active,
		.foreground = foreground,
		.background = background,
		.mode = mode,
	};
	return 0;
}

static int
stroke_pixel(struct stroke_target *target, int32_t x, int32_t y, bool on)
{
	if (target->coverage != NULL) {
		size_t offset = (size_t)(y - target->clip.y0) * target->width
		    + (size_t)(x - target->clip.x0);
		uint8_t value = on ? 2 : 1;

		if (target->coverage[offset] < value)
			target->coverage[offset] = value;
		return 0;
	}
	return st_rip3_ports_plot(target->ports, x, y,
	    on ? target->foreground : target->background, target->mode);
}

static int
stroke_segment(struct stroke_target *target,
    const struct st_rip3_vector *first, const struct st_rip3_vector *last,
    const struct st_rip3_pen *pen, unsigned initial_phase)
{
	int32_t left, right, top, bottom;
	double ax, ay, bx, by, dx, dy, length, squared, major, radius;
	double lower_x, lower_y, upper_x, upper_y;
	int status;

	ax = first->x;
	ay = first->y;
	bx = last->x;
	by = last->y;
	dx = bx - ax;
	dy = by - ay;
	length = hypot(dx, dy);
	squared = dx * dx + dy * dy;
	major = fmax(fabs(dx), fabs(dy));
	radius = pen->thickness / 2.0;
	if ((pen->thickness & 1) == 0) {
		double offset_x;
		double offset_y;

		if (length == 0) {
			offset_x = 0.5;
			offset_y = 0.5;
		}
		else {
			offset_x = -dy / length * 0.5;
			offset_y = dx / length * 0.5;
			if (offset_y < 0 || (offset_y == 0 && offset_x < 0)) {
				offset_x = -offset_x;
				offset_y = -offset_y;
			}
		}
		ax += offset_x;
		bx += offset_x;
		ay += offset_y;
		by += offset_y;
	}
	/* Work is bounded by the visible viewport, even for extreme endpoints
	 * or pen widths. No traversal of an enormous offscreen line is needed. */
	left = target->clip.x0;
	top = target->clip.y0;
	right = target->clip.x1;
	bottom = target->clip.y1;
	lower_x = fmin(ax, bx) - radius;
	lower_y = fmin(ay, by) - radius;
	upper_x = fmax(ax, bx) + radius;
	upper_y = fmax(ay, by) + radius;
	if (lower_x > right || lower_y > bottom || upper_x < left || upper_y < top)
		return 0;
	if (lower_x > left)
		left = (int32_t)ceil(lower_x);
	if (lower_y > top)
		top = (int32_t)ceil(lower_y);
	if (upper_x < right)
		right = (int32_t)floor(upper_x);
	if (upper_y < bottom)
		bottom = (int32_t)floor(upper_y);
	for (int32_t y = top; y <= bottom; y++) {
		for (int32_t x = left; x <= right; x++) {
			double projection = squared == 0 ? 0
			    : (((double)x - ax) * dx + ((double)y - ay) * dy) / squared;
			double distance_x;
			double distance_y;
			unsigned phase;
			bool on;

			if (projection < 0)
				projection = 0;
			if (projection > 1)
				projection = 1;
			distance_x = x - (ax + projection * dx);
			distance_y = y - (ay + projection * dy);
			if (distance_x * distance_x + distance_y * distance_y > radius * radius)
				continue;
			phase = (unsigned)fmod(floor(projection * major + 0.5) + initial_phase, 16.0);
			on = (pen->pattern & (UINT16_C(0x8000) >> phase)) != 0;
			if (on || pen->draw_background) {
				status = stroke_pixel(target, x, y, on);
				if (status != 0)
					return status;
			}
		}
	}
	return 0;
}

int
st_rip3_draw_line(struct st_rip3_ports *ports,
    const struct st_rip3_point *first, const struct st_rip3_point *last,
    const struct st_rip3_pen *pen, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct stroke_target target;
	int status;

	if (first == NULL || last == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = stroke_init(&target, ports, pen, foreground, background, mode);
	if (status != 0 || !target.active)
		return status;
	{
		struct st_rip3_vector a = {first->x, first->y};
		struct st_rip3_vector b = {last->x, last->y};

		return stroke_segment(&target, &a, &b, pen, 0);
	}
}

int
st_rip3_contours_validate(const struct st_rip3_contour *contours,
    size_t count, size_t *vertices)
{
	size_t total = 0;

	if (contours == NULL || vertices == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (count == 0 || count > 4096)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < count; i++) {
		if (contours[i].points == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		if (contours[i].count == 0 || contours[i].count > 4096
		    || contours[i].count > SIZE_MAX - total)
			return IO_ERROR_OUT_OF_RANGE;
		total += contours[i].count;
	}
	*vertices = total;
	return 0;
}

static int
stroke_allocate(struct stroke_target *target, const struct st_rip3_pen *pen,
    double min_x, double min_y, double max_x, double max_y)
{
	double margin = pen->thickness / 2.0 + 1;

	min_x -= margin;
	min_y -= margin;
	max_x += margin;
	max_y += margin;
	if (min_x > target->clip.x1 || min_y > target->clip.y1
	    || max_x < target->clip.x0 || max_y < target->clip.y0) {
		return 0;
	}
	if (min_x > target->clip.x0)
		target->clip.x0 = (int32_t)ceil(min_x);
	if (min_y > target->clip.y0)
		target->clip.y0 = (int32_t)ceil(min_y);
	if (max_x < target->clip.x1)
		target->clip.x1 = (int32_t)floor(max_x);
	if (max_y < target->clip.y1)
		target->clip.y1 = (int32_t)floor(max_y);
	target->width = (size_t)(target->clip.x1 - target->clip.x0) + 1;
	target->height = (size_t)(target->clip.y1 - target->clip.y0) + 1;
	if (target->width > SIZE_MAX / target->height)
		return IO_ERROR_OUT_OF_RANGE;
	target->coverage = calloc(target->width * target->height, 1);
	if (target->coverage == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}

int
st_rip3_stroke_collect(struct st_rip3_ports *ports,
    const struct st_rip3_contour *contours, size_t count, bool closed,
    const struct st_rip3_pen *pen, struct st_rip3_stroke_mask *result)
{
	struct stroke_target target;
	double min_x, min_y, max_x, max_y;
	size_t total;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_contours_validate(contours, count, &total);
	if (status != 0)
		return status;
	status = stroke_init(&target, ports, pen, 0, 0, ST_RIP3_COPY);
	if (status != 0)
		return status;
	if (!target.active) {
		*result = (struct st_rip3_stroke_mask){0};
		return 0;
	}
	min_x = max_x = contours[0].points[0].x;
	min_y = max_y = contours[0].points[0].y;
	for (size_t c = 0; c < count; c++) {
		for (size_t i = 0; i < contours[c].count; i++) {
			min_x = fmin(min_x, contours[c].points[i].x);
			max_x = fmax(max_x, contours[c].points[i].x);
			min_y = fmin(min_y, contours[c].points[i].y);
			max_y = fmax(max_y, contours[c].points[i].y);
		}
	}
	status = stroke_allocate(&target, pen, min_x, min_y, max_x, max_y);
	if (status != 0)
		return status;
	if (target.coverage == NULL) {
		*result = (struct st_rip3_stroke_mask){0};
		return 0;
	}
	for (size_t c = 0; c < count; c++) {
		const struct st_rip3_point *points = contours[c].points;
		size_t vertices = contours[c].count;
		size_t segments = vertices == 1 ? 1 : (closed ? vertices : vertices - 1);
		unsigned phase = 0;

		for (size_t i = 0; i < segments; i++) {
			const struct st_rip3_point *first = &points[i];
			const struct st_rip3_point *last = &points[(i + 1) % vertices];
			double major;

			struct st_rip3_vector a = {first->x, first->y};
			struct st_rip3_vector b = {last->x, last->y};

			status = stroke_segment(&target, &a, &b, pen, phase);
			if (status != 0)
				goto failed;
			major = fmax(fabs((double)last->x - first->x),
			    fabs((double)last->y - first->y));
			phase = (phase + (unsigned)fmod(major, 16.0)) & 15;
		}
	}
	*result = (struct st_rip3_stroke_mask){
		.bounds = target.clip,
		.width = target.width,
		.height = target.height,
		.coverage = target.coverage,
	};
	return 0;
failed:
	free(target.coverage);
	return status;
}

int
st_rip3_stroke_vectors(struct st_rip3_ports *ports,
    const struct st_rip3_vector *vertices, size_t vertex_count,
    const struct st_rip3_vector_run *runs, size_t run_count,
    uint32_t thickness, struct st_rip3_stroke_mask *result)
{
	struct stroke_target target;
	struct st_rip3_pen pen;
	double min_x, min_y, max_x, max_y;
	int status;

	if (result == NULL || (vertices == NULL && vertex_count != 0)
	    || (runs == NULL && run_count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (vertex_count > SIZE_MAX / sizeof(*vertices)
	    || run_count > SIZE_MAX / sizeof(*runs))
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t r = 0; r < run_count; r++) {
		if (runs[r].count == 0 || runs[r].first > vertex_count
		    || runs[r].count > vertex_count - runs[r].first)
			return IO_ERROR_OUT_OF_RANGE;
	}
	for (size_t i = 0; i < vertex_count; i++) {
		/* Includes signed-32 centers plus both ellipse basis vectors. */
		if (!isfinite(vertices[i].x) || !isfinite(vertices[i].y)
		    || fabs(vertices[i].x) > 4.0 * INT32_MAX
		    || fabs(vertices[i].y) > 4.0 * INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
	}
	status = st_rip3_pen_set(&pen, false, 0, 0, thickness);
	if (status != 0)
		return status;
	status = stroke_init(&target, ports, &pen, 0, 0, ST_RIP3_COPY);
	if (status != 0)
		return status;
	if (!target.active || run_count == 0) {
		*result = (struct st_rip3_stroke_mask){0};
		return 0;
	}
	min_x = max_x = vertices[0].x;
	min_y = max_y = vertices[0].y;
	for (size_t i = 1; i < vertex_count; i++) {
		min_x = fmin(min_x, vertices[i].x);
		max_x = fmax(max_x, vertices[i].x);
		min_y = fmin(min_y, vertices[i].y);
		max_y = fmax(max_y, vertices[i].y);
	}
	status = stroke_allocate(&target, &pen, min_x, min_y, max_x, max_y);
	if (status != 0)
		return status;
	if (target.coverage == NULL) {
		*result = (struct st_rip3_stroke_mask){0};
		return 0;
	}
	for (size_t r = 0; r < run_count; r++) {
		size_t segments = runs[r].count == 1 ? 1 : runs[r].count - 1;

		for (size_t i = 0; i < segments; i++) {
			const struct st_rip3_vector *first = &vertices[runs[r].first + i];
			const struct st_rip3_vector *last = runs[r].count == 1
			    ? first : &vertices[runs[r].first + i + 1];

			status = stroke_segment(&target, first, last, &pen, 0);
			if (status != 0)
				goto failed;
		}
	}
	*result = (struct st_rip3_stroke_mask){
		.bounds = target.clip,
		.width = target.width,
		.height = target.height,
		.coverage = target.coverage,
	};
	return 0;
failed:
	free(target.coverage);
	return status;
}

int
st_rip3_stroke_apply(struct st_rip3_ports *ports,
    const struct st_rip3_stroke_mask *mask, uint8_t foreground,
    uint8_t background, enum st_rip3_write_mode mode)
{
	if (ports == NULL || mask == NULL || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (mask->coverage == NULL)
		return 0;
	for (size_t y = 0; y < mask->height; y++) {
		for (size_t x = 0; x < mask->width; x++) {
			uint8_t coverage = mask->coverage[y * mask->width + x];
			int status;

			if (coverage == 0)
				continue;
			status = st_rip3_ports_plot(ports,
			    mask->bounds.x0 + (int32_t)x, mask->bounds.y0 + (int32_t)y,
			    coverage == 2 ? foreground : background, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}

static bool
mask_at(const struct st_rip3_stroke_mask *mask, int32_t x, int32_t y)
{
	if (mask->coverage == NULL || x < mask->bounds.x0 || x > mask->bounds.x1
	    || y < mask->bounds.y0 || y > mask->bounds.y1)
		return false;
	return mask->coverage[(size_t)(y - mask->bounds.y0) * mask->width
	    + (size_t)(x - mask->bounds.x0)] != 0;
}

int
st_rip3_stroke_shadow_apply(struct st_rip3_ports *ports,
    const struct st_rip3_stroke_mask *ink,
    const struct st_rip3_stroke_mask *shadow, uint8_t foreground,
    uint8_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect bounds;

	if (ports == NULL || ink == NULL || shadow == NULL
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (ink->coverage == NULL)
		return st_rip3_stroke_apply(ports, shadow, background, background, mode);
	if (shadow->coverage == NULL)
		return st_rip3_stroke_apply(ports, ink, foreground, foreground, mode);
	bounds = ink->bounds;
	if (shadow->bounds.x0 < bounds.x0)
		bounds.x0 = shadow->bounds.x0;
	if (shadow->bounds.y0 < bounds.y0)
		bounds.y0 = shadow->bounds.y0;
	if (shadow->bounds.x1 > bounds.x1)
		bounds.x1 = shadow->bounds.x1;
	if (shadow->bounds.y1 > bounds.y1)
		bounds.y1 = shadow->bounds.y1;
	for (int32_t y = bounds.y0; y <= bounds.y1; y++) {
		for (int32_t x = bounds.x0; x <= bounds.x1; x++) {
			bool foreground_pixel = mask_at(ink, x, y);
			int status;

			if (!foreground_pixel && !mask_at(shadow, x, y))
				continue;
			status = st_rip3_ports_plot(ports, x, y,
			    foreground_pixel ? foreground : background, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}

void
st_rip3_stroke_free(struct st_rip3_stroke_mask *mask)
{
	if (mask == NULL)
		return;
	free(mask->coverage);
	*mask = (struct st_rip3_stroke_mask){0};
}

int
st_rip3_draw_path(struct st_rip3_ports *ports,
    const struct st_rip3_point *points, size_t count, bool closed,
    const struct st_rip3_pen *pen, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_contour contour = {points, count};
	struct st_rip3_stroke_mask mask = {0};
	int status;

	if (mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_stroke_collect(ports, &contour, 1, closed, pen, &mask);
	if (status == 0)
		status = st_rip3_stroke_apply(ports, &mask, foreground, background, mode);
	st_rip3_stroke_free(&mask);
	return status;
}

int
st_rip3_draw_rectangle(struct st_rip3_ports *ports,
    const struct st_rip3_rect *area, const struct st_rip3_pen *pen,
    uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_point corners[4];
	int32_t x0, x1, y0, y1;

	if (area == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	x0 = area->x0 < area->x1 ? area->x0 : area->x1;
	x1 = area->x0 > area->x1 ? area->x0 : area->x1;
	y0 = area->y0 < area->y1 ? area->y0 : area->y1;
	y1 = area->y0 > area->y1 ? area->y0 : area->y1;
	corners[0] = (struct st_rip3_point){x0, y0};
	corners[1] = (struct st_rip3_point){x1, y0};
	corners[2] = (struct st_rip3_point){x1, y1};
	corners[3] = (struct st_rip3_point){x0, y1};
	if (x0 == x1 || y0 == y1)
		return st_rip3_draw_line(ports, &corners[0], &corners[2], pen,
		    foreground, background, mode);
	return st_rip3_draw_path(ports, corners, 4, true, pen, foreground,
	    background, mode);
}
