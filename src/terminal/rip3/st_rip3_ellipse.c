/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ellipse.h"

#include "st_rip3_ports.h"
#include "st_rip3_draw_internal.h"
#include "io_error.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
	/* Five-digit wire coordinates at small worlds project beyond int32_t.
	 * Keep geometry wide until visible coverage, as in the stroke kernel. */
	return isfinite(value) && fabs(value) <= 1024.0 * INT32_MAX;
}

static bool
basis_valid(double value)
{
	return coordinate_valid(value);
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

struct ellipse_mesh {
	struct st_rip3_vector *vertices;
	size_t vertex_count, vertex_capacity;
	struct st_rip3_vector_run *runs;
	size_t run_count, run_capacity;
};

static void
mesh_free(struct ellipse_mesh *mesh)
{
	free(mesh->vertices);
	free(mesh->runs);
	*mesh = (struct ellipse_mesh){0};
}

static int
mesh_point(struct ellipse_mesh *mesh, struct st_rip3_vector point)
{
	if (mesh->vertex_count == mesh->vertex_capacity) {
		size_t capacity = mesh->vertex_capacity == 0 ? 64 : mesh->vertex_capacity * 2;
		struct st_rip3_vector *vertices;

		if (capacity < mesh->vertex_capacity || capacity > SIZE_MAX / sizeof(*vertices))
			return IO_ERROR_OUT_OF_RANGE;
		vertices = realloc(mesh->vertices, capacity * sizeof(*vertices));
		if (vertices == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		mesh->vertices = vertices;
		mesh->vertex_capacity = capacity;
	}
	mesh->vertices[mesh->vertex_count++] = point;
	mesh->runs[mesh->run_count - 1].count++;
	return 0;
}

static int
mesh_run(struct ellipse_mesh *mesh, struct st_rip3_vector point)
{
	if (mesh->run_count == mesh->run_capacity) {
		size_t capacity = mesh->run_capacity == 0 ? 16 : mesh->run_capacity * 2;
		struct st_rip3_vector_run *runs;

		if (capacity < mesh->run_capacity || capacity > SIZE_MAX / sizeof(*runs))
			return IO_ERROR_OUT_OF_RANGE;
		runs = realloc(mesh->runs, capacity * sizeof(*runs));
		if (runs == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		mesh->runs = runs;
		mesh->run_capacity = capacity;
	}
	mesh->runs[mesh->run_count++] = (struct st_rip3_vector_run){mesh->vertex_count, 0};
	return mesh_point(mesh, point);
}

static struct st_rip3_vector
ellipse_point(const struct st_rip3_ellipse *ellipse, double angle)
{
	double cosine, sine;

	(void)angle_units(angle, &cosine, &sine);
	return (struct st_rip3_vector){
	    ellipse->center_x + ellipse->xx * cosine + ellipse->xy * sine,
	    ellipse->center_y + ellipse->yx * cosine + ellipse->yy * sine};
}

static int
flatten(struct ellipse_mesh *mesh, const struct st_rip3_ellipse *ellipse,
    double first_angle, double last_angle, struct st_rip3_vector first,
    struct st_rip3_vector last, unsigned depth)
{
	double angle = first_angle + (last_angle - first_angle) / 2;
	struct st_rip3_vector middle = ellipse_point(ellipse, angle);
	double dx = last.x - first.x;
	double dy = last.y - first.y;
	double length = hypot(dx, dy);
	double error = length == 0 ? hypot(middle.x - first.x, middle.y - first.y)
	    : fabs((middle.x - first.x) * dy - (middle.y - first.y) * dx) / length;
	int status;

	/* Breaks include both coordinate extrema, so projection onto the chord
	 * is monotone. For an affine ellipse its maximum perpendicular sagitta
	 * occurs at the parameter midpoint of each interval (<180 degrees). */
	if (error <= 0.25)
		return mesh_point(mesh, last);
	if (depth >= 48 || angle == first_angle || angle == last_angle)
		return IO_ERROR_OUT_OF_RANGE;
	status = flatten(mesh, ellipse, first_angle, angle, first, middle, depth + 1);
	if (status == 0)
		status = flatten(mesh, ellipse, angle, last_angle, middle, last, depth + 1);
	return status;
}

struct ellipse_clip {
	double left, top, right, bottom;
};

static bool
in_clip(const struct ellipse_clip *clip, struct st_rip3_vector point)
{
	return point.x >= clip->left && point.x <= clip->right
	    && point.y >= clip->top && point.y <= clip->bottom;
}

static void
add_break(double breaks[24], size_t *count, double angle, double start, double end)
{
	angle = fmod(angle, 360);
	if (angle < 0)
		angle += 360;
	if (angle < start)
		angle += 360;
	if (angle > start && angle < end)
		breaks[(*count)++] = angle;
}

static void
axis_breaks(double breaks[24], size_t *count, double center, double a, double b,
    double lower, double upper, double start, double end)
{
	double radius = hypot(a, b);
	double phase;
	double bounds[] = {lower, upper};

	if (radius == 0)
		return;
	phase = atan2(b, a) / radians_per_degree;
	/* These extrema also cover singular ellipses that double back on a line. */
	add_break(breaks, count, phase, start, end);
	add_break(breaks, count, phase + 180, start, end);
	for (size_t i = 0; i < 2; i++) {
		double ratio = (bounds[i] - center) / radius;
		double delta;

		if (ratio < -1 || ratio > 1)
			continue;
		delta = acos(ratio) / radians_per_degree;
		add_break(breaks, count, phase - delta, start, end);
		add_break(breaks, count, phase + delta, start, end);
	}
}

static int
compare_angle(const void *first, const void *last)
{
	double a = *(const double *)first;
	double b = *(const double *)last;

	return (a > b) - (a < b);
}

static int
mesh_line(struct ellipse_mesh *mesh, const struct ellipse_clip *clip,
    struct st_rip3_vector first, struct st_rip3_vector last)
{
	double dx = last.x - first.x;
	double dy = last.y - first.y;
	double p[] = {-dx, dx, -dy, dy};
	double q[] = {first.x - clip->left, clip->right - first.x,
	    first.y - clip->top, clip->bottom - first.y};
	double low = 0, high = 1;
	int status;

	for (size_t i = 0; i < 4; i++) {
		double ratio;

		if (p[i] == 0) {
			if (q[i] < 0)
				return 0;
			continue;
		}
		ratio = q[i] / p[i];
		if (p[i] < 0)
			low = fmax(low, ratio);
		else
			high = fmin(high, ratio);
		if (low > high)
			return 0;
	}
	last = (struct st_rip3_vector){first.x + high * dx, first.y + high * dy};
	first = (struct st_rip3_vector){first.x + low * dx, first.y + low * dy};
	status = mesh_run(mesh, first);
	if (status == 0)
		status = mesh_point(mesh, last);
	return status;
}

static int
ellipse_outline_collect(struct st_rip3_ports *ports,
    const struct st_rip3_ellipse *ellipse, double start_degrees,
    double sweep_degrees, enum st_rip3_ellipse_region region,
    uint32_t thickness, struct st_rip3_stroke_mask *result)
{
	struct st_rip3_port_info info;
	struct ellipse_mesh mesh = {0};
	struct ellipse_clip clip;
	double breaks[24];
	size_t count = 2;
	double start, end, cosine, sine, padding;
	unsigned current;
	int status;

	if (ports == NULL || ellipse == NULL || result == NULL
	    || region < ST_RIP3_ELLIPSE_PIE || region > ST_RIP3_ELLIPSE_ARC)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!coordinate_valid(ellipse->center_x) || !coordinate_valid(ellipse->center_y)
	    || !basis_valid(ellipse->xx) || !basis_valid(ellipse->xy)
	    || !basis_valid(ellipse->yx) || !basis_valid(ellipse->yy)
	    || !isfinite(start_degrees) || !isfinite(sweep_degrees)
	    || sweep_degrees < 0 || sweep_degrees > 360
	    || thickness == 0 || thickness > INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	status = st_rip3_ports_info(ports, current, &info);
	if (status != 0)
		return status;
	if (!info.viewport_active || sweep_degrees == 0) {
		*result = (struct st_rip3_stroke_mask){0};
		return 0;
	}
	padding = thickness / 2.0 + 1;
	clip = (struct ellipse_clip){-padding, -padding,
	    info.viewport.x1 - info.viewport.x0 + padding,
	    info.viewport.y1 - info.viewport.y0 + padding};
	start = angle_units(start_degrees, &cosine, &sine);
	end = start + sweep_degrees;
	/* A positive sub-ULP sweep still has a round cap at its start. */
	if (end == start && in_clip(&clip, ellipse_point(ellipse, start))) {
		status = mesh_run(&mesh, ellipse_point(ellipse, start));
		if (status != 0)
			goto done;
	}
	breaks[0] = start;
	breaks[1] = end;
	for (unsigned angle = 0; angle < 360; angle += 90)
		add_break(breaks, &count, angle, start, end);
	axis_breaks(breaks, &count, ellipse->center_x, ellipse->xx, ellipse->xy,
	    clip.left, clip.right, start, end);
	axis_breaks(breaks, &count, ellipse->center_y, ellipse->yx, ellipse->yy,
	    clip.top, clip.bottom, start, end);
	qsort(breaks, count, sizeof(*breaks), compare_angle);
	for (size_t i = 1; i < count; i++) {
		struct st_rip3_vector first, last;

		if (breaks[i - 1] == breaks[i]
		    || !in_clip(&clip, ellipse_point(ellipse,
		    breaks[i - 1] + (breaks[i] - breaks[i - 1]) / 2)))
			continue;
		first = ellipse_point(ellipse, breaks[i - 1]);
		last = ellipse_point(ellipse, breaks[i]);
		status = mesh_run(&mesh, first);
		if (status == 0)
			status = flatten(&mesh, ellipse, breaks[i - 1], breaks[i], first, last, 0);
		if (status != 0)
			goto done;
	}
	if (sweep_degrees != 360 && region != ST_RIP3_ELLIPSE_ARC) {
		struct st_rip3_vector first = ellipse_point(ellipse, start);
		struct st_rip3_vector last = ellipse_point(ellipse, end);
		struct st_rip3_vector center = {ellipse->center_x, ellipse->center_y};

		status = mesh_line(&mesh, &clip, first,
		    region == ST_RIP3_ELLIPSE_PIE ? center : last);
		if (status == 0 && region == ST_RIP3_ELLIPSE_PIE)
			status = mesh_line(&mesh, &clip, center, last);
		if (status != 0)
			goto done;
	}
	status = st_rip3_stroke_vectors(ports, mesh.vertices, mesh.vertex_count,
	    mesh.runs, mesh.run_count, thickness, result);
done:
	mesh_free(&mesh);
	return status;
}

int
st_rip3_draw_ellipse_outline(struct st_rip3_ports *ports,
    const struct st_rip3_ellipse *ellipse, double start_degrees,
    double sweep_degrees, enum st_rip3_ellipse_region region,
    uint32_t thickness, uint8_t color, enum st_rip3_write_mode mode)
{
	struct st_rip3_stroke_mask mask = {0};
	int status;

	if (mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	status = ellipse_outline_collect(ports, ellipse, start_degrees, sweep_degrees,
	    region, thickness, &mask);
	if (status == 0)
		status = st_rip3_stroke_apply(ports, &mask, color, 0, mode);
	st_rip3_stroke_free(&mask);
	return status;
}

int
st_rip3_draw_filled_ellipse(struct st_rip3_ports *ports,
    const struct st_rip3_ellipse *ellipse, double start_degrees,
    double sweep_degrees, enum st_rip3_ellipse_region region,
    const struct st_rip3_fill *fill, uint8_t foreground, uint8_t background,
    uint32_t border_thickness, uint8_t border_color,
    enum st_rip3_write_mode mode)
{
	struct st_rip3_stroke_mask mask = {0};
	int status;

	if (fill == NULL || (region != ST_RIP3_ELLIPSE_PIE && region != ST_RIP3_ELLIPSE_CHORD)
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (border_thickness != 0) {
		status = ellipse_outline_collect(ports, ellipse, start_degrees, sweep_degrees,
		    region, border_thickness, &mask);
		if (status != 0)
			return status;
	}
	status = st_rip3_draw_ellipse_interior(ports, ellipse, start_degrees, sweep_degrees,
	    region, fill, foreground, background, mode);
	if (status == 0)
		status = st_rip3_stroke_apply(ports, &mask, border_color, 0, ST_RIP3_COPY);
	st_rip3_stroke_free(&mask);
	return status;
}
