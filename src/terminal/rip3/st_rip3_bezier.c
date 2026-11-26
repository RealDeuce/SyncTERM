/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdlib.h>

#include "io_error.h"
#include "st_rip3_bezier.h"

static int32_t
signed_word(uint32_t bits)
{
	return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(4294967296));
}

static int32_t
signed_half(uint32_t bits)
{
	bits &= 65535;
	return bits <= 32767 ? (int32_t)bits : (int32_t)bits - 65536;
}

static int32_t
coordinate(const struct st_rip3_point controls[4], bool y, unsigned i, unsigned n)
{
	uint32_t q[4];
	for (size_t j = 0; j < 4; j++)
		q[j] = (uint32_t)(y ? controls[j].y : controls[j].x) << 8;
	for (size_t level = 3; level != 0; level--)
		for (size_t j = 0; j < level; j++) {
			uint32_t difference = (uint32_t)(q[j + 1] - q[j]);
			uint32_t product    = (uint32_t)((uint64_t)difference * i);
			int32_t  quotient   = (int32_t)((int64_t)signed_word(product) / n);
			q[j]                = (uint32_t)(q[j] + (uint32_t)quotient);
		}
	int64_t value = signed_word(q[0]);
	/* C division truncates; the final fixed-point conversion floors. */
	int32_t whole = (int32_t)(value >= 0 ? value / 256 : -((-value + 255) / 256));
	return signed_half((uint32_t)whole);
}

int
st_rip3_bezier_sample(struct st_rip3_point *output, const struct st_rip3_point controls[4], unsigned count)
{
	if (output == NULL || controls == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (count > 4095)
		return IO_ERROR_OUT_OF_RANGE;
	output[0] = controls[0];
	for (unsigned i = 1; i + 1 < count; i++)
		output[i] = (struct st_rip3_point) {coordinate(controls, false, i, count),
		    coordinate(controls, true, i, count)};
	output[count < 2 ? 1 : count - 1] =
	    (struct st_rip3_point) {signed_half((uint32_t)controls[3].x), signed_half((uint32_t)controls[3].y)};
	return 0;
}

static struct st_rip3_point
point_record(struct st_rip3_point point)
{
	return (struct st_rip3_point) {(int32_t)((uint32_t)point.x & 65535), (int32_t)((uint32_t)point.y & 65535)};
}

int
st_rip3_bezier_expand(struct st_rip3_bezier_path *result, const struct st_rip3_bezier_record *records,
    size_t count, unsigned requested, bool closed)
{
	struct st_rip3_bezier_path path   = {0};
	size_t                     curves = 0, lines = 0, next = 0;
	unsigned                   n = requested;
	if (result == NULL || records == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (count > 4095 || requested > 4095)
		return IO_ERROR_OUT_OF_RANGE;
	if (count == 0)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < count; i++) {
		unsigned coordinates = st_rip3_bezier_record_coordinates(records[i].type);
		if (coordinates == 0)
			return IO_ERROR_INVALID_FORMAT;
		if (coordinates == 6)
			curves++;
		else
			lines++;
	}
	if (2 + 7 * curves + 3 * lines > 8193)
		return IO_ERROR_OUT_OF_RANGE;
	if (!closed && st_rip3_bezier_record_coordinates(records[count - 1].type) == 6)
		return IO_ERROR_INVALID_FORMAT;
	if (curves != 0) {
		if (curves * n + lines + 1 > 4095)
			n = (unsigned)((4094 - lines) / curves);
		if (n == 0)
			return IO_ERROR_OUT_OF_RANGE;
	}
	path.count  = curves * n + lines;
	path.points = malloc(path.count * sizeof(*path.points));
	if (path.points == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < count; i++) {
		if (st_rip3_bezier_record_coordinates(records[i].type) == 2)
			path.points[next++] = point_record(records[i].points[0]);
		else if (n == 1)
			path.points[next++] = records[i].points[0];
		else {
			const struct st_rip3_point controls[] = {records[i].points[0], records[i].points[1],
			    records[i].points[2], point_record(records[(i + 1) % count].points[0])};
			(void)st_rip3_bezier_sample(path.points + next, controls, n);
			next += n;
		}
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
	*path = (struct st_rip3_bezier_path) {0};
}
