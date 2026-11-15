/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_draw.h"

#include "st_rip3_ports.h"
#include "io_error.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

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

int
st_rip3_draw_line(struct st_rip3_ports *ports,
    const struct st_rip3_point *first, const struct st_rip3_point *last,
    const struct st_rip3_pen *pen, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	unsigned current;
	int32_t left, right, top, bottom;
	double ax, ay, bx, by, dx, dy, length, squared, major, radius;
	double lower_x, lower_y, upper_x, upper_y;
	int status;

	if (ports == NULL || first == NULL || last == NULL || pen == NULL
	    || pen->pattern == 0 || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (pen->thickness == 0 || pen->thickness > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0)
		return status;
	if (!info.viewport_active)
		return 0;
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
	left = top = 0;
	right = info.viewport.x1 - info.viewport.x0;
	bottom = info.viewport.y1 - info.viewport.y0;
	lower_x = fmin(ax, bx) - radius;
	lower_y = fmin(ay, by) - radius;
	upper_x = fmax(ax, bx) + radius;
	upper_y = fmax(ay, by) + radius;
	if (lower_x > right || lower_y > bottom || upper_x < 0 || upper_y < 0)
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
			phase = (unsigned)fmod(floor(projection * major + 0.5), 16.0);
			on = (pen->pattern & (UINT16_C(0x8000) >> phase)) != 0;
			if (on || pen->draw_background) {
				status = st_rip3_ports_plot(ports, x, y,
				    on ? foreground : background, mode);
				if (status != 0)
					return status;
			}
		}
	}
	return 0;
}
