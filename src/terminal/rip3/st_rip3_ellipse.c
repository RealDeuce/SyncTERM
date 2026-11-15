/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ellipse.h"

#include "st_rip3_ports.h"
#include "io_error.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const double radians_per_degree = 0.017453292519943295769236907684886;
static const double full_turn = 6.283185307179586476925286766559;

static double
angle_units(double degrees, double *cosine, double *sine)
{
	double normalized = fmod(degrees, 360);

	if (normalized < 0)
		normalized += 360;
	/* Exact cardinal endpoints also keep complementary chord coefficients
	 * identical under reversal, rather than inheriting libm's tiny residual. */
	if (normalized == 0) {
		*cosine = 1;
		*sine = 0;
	}
	else if (normalized == 90) {
		*cosine = 0;
		*sine = 1;
	}
	else if (normalized == 180) {
		*cosine = -1;
		*sine = 0;
	}
	else if (normalized == 270) {
		*cosine = 0;
		*sine = -1;
	}
	else {
		*cosine = cos(normalized * radians_per_degree);
		*sine = sin(normalized * radians_per_degree);
	}
	return normalized;
}

static bool
coordinate_valid(double value)
{
	return isfinite(value) && value >= INT32_MIN && value <= INT32_MAX;
}

static bool
basis_valid(double value)
{
	return isfinite(value) && fabs(value) <= INT32_MAX;
}

int
st_rip3_ellipse_axes(struct st_rip3_ellipse *result, double center_x,
    double center_y, double radius_x, double radius_y, double rotation_degrees)
{
	double cosine, sine;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!coordinate_valid(center_x) || !coordinate_valid(center_y)
	    || !basis_valid(radius_x) || !basis_valid(radius_y)
	    || radius_x < 0 || radius_y < 0 || !isfinite(rotation_degrees))
		return IO_ERROR_OUT_OF_RANGE;
	(void)angle_units(rotation_degrees, &cosine, &sine);
	*result = (struct st_rip3_ellipse){center_x, center_y,
	    radius_x * cosine, -radius_y * sine,
	    -radius_x * sine, -radius_y * cosine};
	return 0;
}

struct ellipse_test {
	const struct st_rip3_ellipse *ellipse;
	double inv_xx, inv_xy, inv_yx, inv_yy;
	bool whole;
	double start;
	double sweep;
	enum st_rip3_ellipse_region region;
	double nx, ny, constant;
	bool chord_boundary;
};

static bool
contains(const struct ellipse_test *test, double x, double y)
{
	double dx = x - test->ellipse->center_x;
	double dy = y - test->ellipse->center_y;
	double qx = test->inv_xx * dx + test->inv_xy * dy;
	double qy = test->inv_yx * dx + test->inv_yy * dy;
	double norm;

	if (!isfinite(qx) || !isfinite(qy) || fabs(qx) >= 1 || fabs(qy) >= 1)
		return false;
	norm = qx * qx + qy * qy;
	if (norm >= 1)
		return false;
	if (test->whole)
		return true;
	if (test->region == ST_RIP3_ELLIPSE_PIE) {
		double angle;
		double delta;

		if (norm == 0)
			return true;
		angle = atan2(qy, qx);
		if (angle < 0)
			angle += full_turn;
		delta = angle - test->start;
		if (delta < 0)
			delta += full_turn;
		return delta < test->sweep;
	}
	else {
		double a = test->nx * qx;
		double b = test->ny * qy;
		double side = a + b + test->constant;
		/* A rounding-error-sized band gives shared chord boundaries one
		 * owner, avoiding a seam or a double XOR between complementary caps. */
		double error = 8 * DBL_EPSILON * (fabs(a) + fabs(b) + fabs(test->constant));

		if (fabs(side) <= error)
			return test->chord_boundary;
		return side < 0;
	}
}

int
st_rip3_draw_ellipse_interior(struct st_rip3_ports *ports,
    const struct st_rip3_ellipse *ellipse, double start_degrees,
    double sweep_degrees, enum st_rip3_ellipse_region region,
    const struct st_rip3_fill *fill, uint8_t foreground, uint8_t background,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_port_info info;
	struct ellipse_test test = {0};
	double determinant;
	double x_extent, y_extent;
	double min_x, min_y, max_x, max_y;
	int32_t left, right, top, bottom;
	unsigned current;
	int status;

	if (ports == NULL || ellipse == NULL || fill == NULL
	    || (region != ST_RIP3_ELLIPSE_PIE && region != ST_RIP3_ELLIPSE_CHORD)
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!coordinate_valid(ellipse->center_x) || !coordinate_valid(ellipse->center_y)
	    || !basis_valid(ellipse->xx) || !basis_valid(ellipse->xy)
	    || !basis_valid(ellipse->yx) || !basis_valid(ellipse->yy)
	    || !isfinite(start_degrees) || !isfinite(sweep_degrees)
	    || sweep_degrees < 0 || sweep_degrees > 360)
		return IO_ERROR_OUT_OF_RANGE;
	if (sweep_degrees == 0)
		return 0;
	determinant = ellipse->xx * ellipse->yy - ellipse->xy * ellipse->yx;
	if (determinant == 0)
		return 0;
	test.ellipse = ellipse;
	test.inv_xx = ellipse->yy / determinant;
	test.inv_xy = -ellipse->xy / determinant;
	test.inv_yx = -ellipse->yx / determinant;
	test.inv_yy = ellipse->xx / determinant;
	if (!isfinite(test.inv_xx) || !isfinite(test.inv_xy)
	    || !isfinite(test.inv_yx) || !isfinite(test.inv_yy))
		return IO_ERROR_OUT_OF_RANGE;
	test.whole = sweep_degrees == 360;
	test.region = region;
	if (!test.whole) {
		double ax, ay, bx, by;
		double start = angle_units(start_degrees, &ax, &ay);
		double edge_x, edge_y;

		(void)angle_units(start + sweep_degrees, &bx, &by);
		test.start = start * radians_per_degree;
		test.sweep = sweep_degrees * radians_per_degree;
		test.nx = ay - by;
		test.ny = bx - ax;
		test.constant = ax * by - ay * bx;
		edge_x = ellipse->xx * (bx - ax) + ellipse->xy * (by - ay);
		edge_y = ellipse->yx * (bx - ax) + ellipse->yy * (by - ay);
		test.chord_boundary = edge_y > 0 || (edge_y == 0 && edge_x > 0);
	}
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0 || !info.viewport_active)
		return status;
	left = top = 0;
	right = info.viewport.x1 - info.viewport.x0;
	bottom = info.viewport.y1 - info.viewport.y0;
	x_extent = hypot(ellipse->xx, ellipse->xy);
	y_extent = hypot(ellipse->yx, ellipse->yy);
	min_x = ellipse->center_x - x_extent - 0.5;
	max_x = ellipse->center_x + x_extent - 0.5;
	min_y = ellipse->center_y - y_extent - 0.5;
	max_y = ellipse->center_y + y_extent - 0.5;
	if (min_x > right || min_y > bottom || max_x < left || max_y < top)
		return 0;
	if (min_x > left)
		left = (int32_t)ceil(min_x);
	if (min_y > top)
		top = (int32_t)ceil(min_y);
	if (max_x < right)
		right = (int32_t)floor(max_x);
	if (max_y < bottom)
		bottom = (int32_t)floor(max_y);
	for (int32_t y = top; y <= bottom; y++) {
		int32_t run = -1;

		for (int32_t x = left; x <= right; x++) {
			bool inside = contains(&test, (double)x + 0.5, (double)y + 0.5);

			if (inside && run < 0)
				run = x;
			if (!inside && run >= 0) {
				struct st_rip3_rect span = {run, y, x - 1, y};

				status = st_rip3_draw_bar(ports, &span, fill, foreground, background, mode);
				if (status != 0)
					return status;
				run = -1;
			}
		}
		if (run >= 0) {
			struct st_rip3_rect span = {run, y, right, y};

			status = st_rip3_draw_bar(ports, &span, fill, foreground, background, mode);
			if (status != 0)
				return status;
		}
	}
	return 0;
}
