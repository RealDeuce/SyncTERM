/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_rounded_rectangle.h"
#include "st_rip3_ellipse_internal.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool
valid(const struct st_rip3_rounded_rectangle *rectangle)
{
	return isfinite(rectangle->x0) && isfinite(rectangle->y0)
	    && isfinite(rectangle->x1) && isfinite(rectangle->y1)
	    && fabs(rectangle->x0) <= 1024.0*INT32_MAX && fabs(rectangle->y0) <= 1024.0*INT32_MAX
	    && fabs(rectangle->x1) <= 1024.0*INT32_MAX && fabs(rectangle->y1) <= 1024.0*INT32_MAX
	    && rectangle->x0 <= rectangle->x1 && rectangle->y0 <= rectangle->y1
	    && isfinite(rectangle->radius) && rectangle->radius >= 0
	    && rectangle->radius <= fmin(rectangle->x1-rectangle->x0,rectangle->y1-rectangle->y0)/2;
}

int
st_rip3_rounded_rectangle_set(struct st_rip3_rounded_rectangle *result,
    double x0, double y0, double x1, double y1, double radius)
{
	struct st_rip3_rounded_rectangle value;

	if (result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) return IO_ERROR_OUT_OF_RANGE;
	value = (struct st_rip3_rounded_rectangle){fmin(x0,x1),fmin(y0,y1),fmax(x0,x1),fmax(y0,y1),radius};
	if (!valid(&value)) return IO_ERROR_OUT_OF_RANGE;
	*result = value;
	return 0;
}

struct rounded_outline { struct st_rip3_stroke_mask masks[8]; };

static void
outline_free(struct rounded_outline *outline)
{
	for (size_t i = 0; i < 8; i++) st_rip3_stroke_free(outline->masks+i);
}

static int
outline_collect(struct st_rip3_ports *ports, const struct st_rip3_rounded_rectangle *rectangle,
    uint32_t thickness, struct rounded_outline *result)
{
	const double x0 = rectangle->x0, y0 = rectangle->y0, x1 = rectangle->x1, y1 = rectangle->y1;
	const double radius = rectangle->radius;
	const struct st_rip3_pen pen = {.pattern=0xffff,.thickness=thickness};
	int status;

	if (radius == 0) {
		struct st_rip3_vector points[] = {{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
		bool closed = x0 != x1 && y0 != y1;
		if (!closed) points[1] = (struct st_rip3_vector){x1,y1};
		return st_rip3_stroke_path_vectors(ports, points, closed ? 4 : 2,closed,&pen,result->masks);
	}
	const struct st_rip3_vector centers[] = {{x1-radius,y0+radius},{x0+radius,y0+radius},
	    {x0+radius,y1-radius},{x1-radius,y1-radius}};
	const struct st_rip3_vector lines[][2] = {
	    {{x0+radius,y0},{x1-radius,y0}}, {{x0,y0+radius},{x0,y1-radius}},
	    {{x0+radius,y1},{x1-radius,y1}}, {{x1,y0+radius},{x1,y1-radius}},
	};
	for (size_t i = 0; i < 4; i++) {
		struct st_rip3_ellipse arc;
		status = st_rip3_ellipse_axes(&arc, centers[i].x,centers[i].y,radius,radius,0);
		if (status == 0) status = st_rip3_ellipse_collect(ports, &arc, (double)i*90,90,
		    ST_RIP3_ELLIPSE_ARC,thickness,result->masks+i);
		if (status != 0) return status;
	}
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_stroke_path_vectors(ports, lines[i],2,false,&pen,result->masks+4+i);
		if (status != 0) return status;
	}
	return 0;
}

static int
outline_apply(struct st_rip3_ports *ports, const struct rounded_outline *outline,
    uint8_t color, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect bounds = {0};
	bool found = false;

	for (size_t i = 0; i < 8; i++) {
		const struct st_rip3_stroke_mask *mask = outline->masks+i;
		if (mask->coverage == NULL) continue;
		if (!found) { bounds = mask->bounds; found = true; }
		else {
			if (mask->bounds.x0 < bounds.x0) bounds.x0 = mask->bounds.x0;
			if (mask->bounds.y0 < bounds.y0) bounds.y0 = mask->bounds.y0;
			if (mask->bounds.x1 > bounds.x1) bounds.x1 = mask->bounds.x1;
			if (mask->bounds.y1 > bounds.y1) bounds.y1 = mask->bounds.y1;
		}
	}
	if (!found) return 0;
	for (int32_t y = bounds.y0; y <= bounds.y1; y++) for (int32_t x = bounds.x0; x <= bounds.x1; x++) {
		for (size_t i = 0; i < 8; i++) {
			const struct st_rip3_stroke_mask *mask = outline->masks+i;
			if (mask->coverage == NULL || x < mask->bounds.x0 || x > mask->bounds.x1
			    || y < mask->bounds.y0 || y > mask->bounds.y1) continue;
			if (mask->coverage[(size_t)(y-mask->bounds.y0)*mask->width+(size_t)(x-mask->bounds.x0)] != 0) {
				int status = st_rip3_ports_plot(ports, x,y,color,mode);
				if (status != 0) return status;
				/* Joins belong to the outline once, including XOR/NOT. */
				break;
			}
		}
	}
	return 0;
}

static bool
contains(const struct st_rip3_rounded_rectangle *rectangle, double x, double y)
{
	double radius = rectangle->radius, cx, cy, dx, dy;

	if (x < rectangle->x0 || x >= rectangle->x1 || y < rectangle->y0 || y >= rectangle->y1) return false;
	if (radius == 0 || (x >= rectangle->x0+radius && x <= rectangle->x1-radius)
	    || (y >= rectangle->y0+radius && y <= rectangle->y1-radius)) return true;
	cx = x < rectangle->x0+radius ? rectangle->x0+radius : rectangle->x1-radius;
	cy = y < rectangle->y0+radius ? rectangle->y0+radius : rectangle->y1-radius;
	dx = x-cx; dy = y-cy;
	return dx*dx+dy*dy < radius*radius;
}

static int
fill_interior(struct st_rip3_ports *ports, const struct st_rip3_rounded_rectangle *rectangle,
    const struct st_rip3_fill *fill, uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	unsigned current;
	int32_t left = 0, top = 0, right, bottom;
	double min_x = rectangle->x0-0.5, min_y = rectangle->y0-0.5;
	double max_x = rectangle->x1-0.5, max_y = rectangle->y1-0.5;
	int status;

	if (rectangle->x0 == rectangle->x1 || rectangle->y0 == rectangle->y1) return 0;
	status = st_rip3_ports_current(ports, &current);
	if (status == 0) status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || !info.viewport_active) return status;
	right = info.viewport.x1-info.viewport.x0; bottom = info.viewport.y1-info.viewport.y0;
	if (min_x > right || min_y > bottom || max_x < left || max_y < top) return 0;
	if (min_x > left) left = (int32_t)ceil(min_x);
	if (min_y > top) top = (int32_t)ceil(min_y);
	if (max_x < right) right = (int32_t)floor(max_x);
	if (max_y < bottom) bottom = (int32_t)floor(max_y);
	for (int32_t y = top; y <= bottom; y++) {
		int32_t run = -1;
		for (int32_t x = left; x <= right; x++) {
			bool inside = contains(rectangle, (double)x+0.5,(double)y+0.5);
			if (inside && run < 0) run = x;
			if (!inside && run >= 0) {
				const struct st_rip3_rect span = {run,y,x-1,y};
				status = st_rip3_draw_bar(ports, &span,fill,foreground,background,mode);
				if (status != 0) return status;
				run = -1;
			}
		}
		if (run >= 0) {
			const struct st_rip3_rect span = {run,y,right,y};
			status = st_rip3_draw_bar(ports, &span,fill,foreground,background,mode);
			if (status != 0) return status;
		}
	}
	return 0;
}

int
st_rip3_draw_rounded_rectangle(struct st_rip3_ports *ports,
    const struct st_rip3_rounded_rectangle *rectangle,
    uint32_t thickness, uint8_t color, enum st_rip3_write_mode mode)
{
	struct rounded_outline outline = {0};
	int status;

	if (ports == NULL || rectangle == NULL || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT) return IO_ERROR_INVALID_ARGUMENT;
	if (!valid(rectangle) || thickness == 0 || thickness > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	status = outline_collect(ports, rectangle,thickness,&outline);
	if (status == 0) status = outline_apply(ports, &outline,color,mode);
	outline_free(&outline);
	return status;
}

int
st_rip3_draw_filled_rounded_rectangle(struct st_rip3_ports *ports,
    const struct st_rip3_rounded_rectangle *rectangle, const struct st_rip3_fill *fill,
    uint8_t foreground, uint8_t background, uint32_t border_thickness,
    uint8_t border_color, enum st_rip3_write_mode mode)
{
	struct rounded_outline outline = {0};
	int status = 0;

	if (ports == NULL || rectangle == NULL || fill == NULL || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT) return IO_ERROR_INVALID_ARGUMENT;
	if (!valid(rectangle) || border_thickness > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	if (border_thickness != 0) status = outline_collect(ports, rectangle,border_thickness,&outline);
	if (status == 0) status = fill_interior(ports, rectangle,fill,foreground,background,mode);
	if (status == 0) status = outline_apply(ports, &outline,border_color,ST_RIP3_COPY);
	outline_free(&outline);
	return status;
}
