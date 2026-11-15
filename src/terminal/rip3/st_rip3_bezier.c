/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bezier.h"

#include "io_error.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static int32_t
cubic(int32_t a, int32_t b, int32_t c, int32_t d, double t)
{
	double u = 1 - t;
	/* De Casteljau evaluation stays in the convex hull and avoids the large
	 * cancelling polynomial coefficients of the expanded power form. */
	double ab = u * a + t * b;
	double bc = u * b + t * c;
	double cd = u * c + t * d;
	double abc = u * ab + t * bc;
	double bcd = u * bc + t * cd;
	double value = floor(u * abc + t * bcd + 0.5);

	/* Rounding at an extreme hull edge must not escape the integer domain. */
	if (value < INT32_MIN)
		return INT32_MIN;
	if (value > INT32_MAX)
		return INT32_MAX;
	return (int32_t)value;
}

int
st_rip3_bezier_build(struct st_rip3_bezier_path *result,
    const struct st_rip3_point *start,
    const struct st_rip3_bezier_segment *segments, size_t count,
    size_t subdivisions)
{
	struct st_rip3_bezier_path path = {0};
	struct st_rip3_point first;
	size_t vertices = 1, next = 1;

	if (result == NULL || start == NULL || segments == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (count == 0 || count > 4095 || subdivisions == 0 || subdivisions > 4095)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < count; i++) {
		size_t added;

		if (segments[i].type < ST_RIP3_BEZIER_LINE
		    || segments[i].type > ST_RIP3_BEZIER_JOINTED)
			return IO_ERROR_INVALID_ARGUMENT;
		added = segments[i].type == ST_RIP3_BEZIER_LINE ? 1 : subdivisions;
		if (added > 4096 - vertices)
			return IO_ERROR_OUT_OF_RANGE;
		vertices += added;
	}
	path.points = malloc(vertices * sizeof(*path.points));
	if (path.points == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	path.count = vertices;
	first = *start;
	path.points[0] = first;
	for (size_t i = 0; i < count; i++) {
		const struct st_rip3_point *points = segments[i].points;

		if (segments[i].type == ST_RIP3_BEZIER_LINE) {
			path.points[next++] = points[0];
			first = points[0];
			continue;
		}
		for (size_t j = 1; j < subdivisions; j++) {
			double t = (double)j / subdivisions;

			path.points[next++] = (struct st_rip3_point){
			    cubic(first.x, points[0].x, points[1].x, points[2].x, t),
			    cubic(first.y, points[0].y, points[1].y, points[2].y, t)};
		}
		path.points[next++] = points[2];
		first = points[2];
	}
	*result = path;
	return 0;
}

void
st_rip3_bezier_free(struct st_rip3_bezier_path *path)
{
	if (path == NULL)
		return;
	free(path->points);
	*path = (struct st_rip3_bezier_path){0};
}

int
st_rip3_draw_bezier_path(struct st_rip3_ports *ports,
    const struct st_rip3_point *start,
    const struct st_rip3_bezier_segment *segments, size_t count,
    size_t subdivisions, bool closed, const struct st_rip3_pen *pen,
    uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode)
{
	struct st_rip3_bezier_path path = {0};
	int status;

	status = st_rip3_bezier_build(&path, start, segments, count, subdivisions);
	if (status == 0)
		status = st_rip3_draw_path(ports, path.points, path.count, closed, pen,
		    foreground, background, mode);
	st_rip3_bezier_free(&path);
	return status;
}

int
st_rip3_draw_filled_bezier_path(struct st_rip3_ports *ports,
    const struct st_rip3_point *start,
    const struct st_rip3_bezier_segment *segments, size_t count,
    size_t subdivisions, const struct st_rip3_fill *fill,
    uint8_t foreground, uint8_t background, const struct st_rip3_pen *border,
    uint8_t border_color, enum st_rip3_write_mode mode)
{
	struct st_rip3_bezier_path path = {0};
	int status;

	status = st_rip3_bezier_build(&path, start, segments, count, subdivisions);
	if (status == 0) {
		struct st_rip3_contour contour = {path.points, path.count};

		status = st_rip3_draw_filled_polygons(ports, &contour, 1, fill,
		    foreground, background, border, border_color, mode);
	}
	st_rip3_bezier_free(&path);
	return status;
}
