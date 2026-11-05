/* Integer recurrences from the documented Kickstart 34.1/40.24 probes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_skypix_walkers.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
int cterm_skypix_word(unsigned value)
{
	value &= 65535;
	return value < 32768 ? (int)value : (int)value - 65536;
}
void cterm_skypix_line(int x, int y, int end_x, int end_y, cterm_skypix_plot_t plot, void *context)
{
	x = cterm_skypix_word((unsigned)x);
	y = cterm_skypix_word((unsigned)y);
	int dx = cterm_skypix_word((unsigned)(end_x - x));
	int dy = cterm_skypix_word((unsigned)(end_y - y));
	int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, ax = abs(dx), ay = abs(dy);
	bool x_major = ax > ay;
	int major = x_major ? ax : ay, minor = x_major ? ay : ax;
	int error = cterm_skypix_word((unsigned)(2 * minor - major));
	for (int i = 0; i <= major; ++i) {
		plot(context, x, y);
		bool step_minor = error >= 0;
		error = cterm_skypix_word((unsigned)(error + 2 * minor - (step_minor ? 2 * major : 0)));
		if (x_major) {
			x = cterm_skypix_word((unsigned)(x + sx));
			if (step_minor)
				y = cterm_skypix_word((unsigned)(y + sy));
		} else {
			y = cterm_skypix_word((unsigned)(y + sy));
			if (step_minor)
				x = cterm_skypix_word((unsigned)(x + sx));
		}
	}
}
static int64_t wrap(uint64_t value, bool kick13)
{
	if (kick13) {
		value &= UINT32_MAX;
		return (int64_t)value - ((value & UINT64_C(0x80000000)) ? INT64_C(0x100000000) : 0);
	}
	return value <= INT64_MAX ? (int64_t)value : -(int64_t)(~value) - 1;
}
static void four(int cx, int cy, int64_t x, int64_t y, cterm_skypix_plot_t plot, void *context)
{
	int right = cterm_skypix_word((unsigned)((uint64_t)cx + (uint64_t)x));
	int left = cterm_skypix_word((unsigned)((uint64_t)cx - (uint64_t)x));
	int bottom = cterm_skypix_word((unsigned)((uint64_t)cy + (uint64_t)y));
	int top = cterm_skypix_word((unsigned)((uint64_t)cy - (uint64_t)y));
	plot(context, right, bottom);
	if (left < right)
		plot(context, left, bottom);
	if (top < bottom) {
		plot(context, right, top);
		if (left < right)
			plot(context, left, top);
	}
}
void cterm_skypix_ellipse(int cx, int cy, int rx, int ry, bool kick13, cterm_skypix_plot_t plot,
						  void *context)
{
	rx = cterm_skypix_word((unsigned)rx);
	ry = cterm_skypix_word((unsigned)ry);
	if (rx < 0 || ry < 0)
		return;
	int64_t x = rx, y = 0;
	uint64_t aa = (uint64_t)rx * (unsigned)rx, bb = (uint64_t)ry * (unsigned)ry;
	int64_t a2 = wrap(2 * aa, kick13), a4 = wrap(4 * aa, kick13), b2 = wrap(2 * bb, kick13),
			b4 = wrap(4 * bb, kick13);
	int64_t vx = wrap((uint64_t)b4 * (uint64_t)x, kick13), vy = 0;
	int64_t e1 = wrap((uint64_t)a2 - (uint64_t)b2 * (uint64_t)x + bb, kick13);
	int64_t e2 = wrap((uint64_t)b2 - (uint64_t)vx + aa, kick13);
	bool row_written = false;
	/* Bounded work also covers hostile word-sized overflow cycles outside
	 * the documented, ROM-checked positive-radius domain. */
	unsigned budget = 262144;
	while (e2 < 0 && budget != 0) {
		--budget;
		four(cx, cy, x, y, plot, context);
		y = wrap((uint64_t)y + 1, kick13);
		vy = wrap((uint64_t)vy + (uint64_t)a4, kick13);
		if (e1 < 0) {
			e1 = wrap((uint64_t)e1 + (uint64_t)vy + (uint64_t)a2, kick13);
			e2 = wrap((uint64_t)e2 + (uint64_t)vy, kick13);
		} else {
			x = wrap((uint64_t)x - 1, kick13);
			vx = wrap((uint64_t)vx - (uint64_t)b4, kick13);
			e1 = wrap((uint64_t)e1 + (uint64_t)vy + (uint64_t)a2 - (uint64_t)vx, kick13);
			e2 = wrap((uint64_t)e2 + (uint64_t)vy + (uint64_t)b2 - (uint64_t)vx, kick13);
		}
	}
	while (x >= 0 && budget != 0) {
		--budget;
		four(cx, cy, x, y, plot, context);
		row_written = true;
		x = wrap((uint64_t)x - 1, kick13);
		vx = wrap((uint64_t)vx - (uint64_t)b4, kick13);
		if (e2 < 0) {
			y = wrap((uint64_t)y + 1, kick13);
			row_written = false;
			vy = wrap((uint64_t)vy + (uint64_t)a4, kick13);
			e2 = wrap((uint64_t)e2 + (uint64_t)vy + (uint64_t)b2 - (uint64_t)vx, kick13);
		} else
			e2 = wrap((uint64_t)e2 + (uint64_t)b2 - (uint64_t)vx, kick13);
	}
	if (row_written)
		y = wrap((uint64_t)y + 1, kick13);
	while (cterm_skypix_word((unsigned)y) <= ry && budget != 0) {
		--budget;
		int top = cterm_skypix_word((unsigned)((uint64_t)cy - (uint64_t)y));
		int bottom = cterm_skypix_word((unsigned)((uint64_t)cy + (uint64_t)y));
		if (top < bottom)
			plot(context, cx, top);
		plot(context, cx, bottom);
		y = wrap((uint64_t)y + 1, kick13);
	}
}
