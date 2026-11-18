/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_arc.h"
#include "io_error.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static struct st_rip3_arc_ray
ray(uint64_t radius, unsigned angle)
{
	static const unsigned sine[] = {0,871,1736,2588,3420,4226,5000,5736,6428,7071,
	    7660,8191,8660,9063,9397,9659,9848,9962,10000};
	struct st_rip3_arc_ray result;
	unsigned horizontal, vertical;
	bool negative_x, negative_y;

	if (angle > 360)
		angle %= 360;
	negative_x = angle > 90 && angle <= 270;
	negative_y = angle <= 180;
	if (angle <= 90) {
		horizontal = (90-angle)/5; vertical = angle/5;
	}
	else if (angle <= 180) {
		horizontal = (angle-90)/5; vertical = (180-angle)/5;
	}
	else if (angle <= 270) {
		horizontal = (270-angle)/5; vertical = (angle-180)/5;
	}
	else {
		horizontal = (angle-270)/5; vertical = (360-angle)/5;
	}
	result.x = (int64_t)(radius*sine[horizontal]/10000);
	result.y = (int64_t)(radius*sine[vertical]/10000);
	if (negative_x) result.x = -result.x;
	if (negative_y) result.y = -result.y;
	return result;
}

static bool
zero(struct st_rip3_arc_ray point)
{
	return point.x == 0 && point.y == 0;
}

static struct st_rip3_arc_ray
direction(struct st_rip3_arc_ray point)
{
	uint64_t a = (uint64_t)(point.x < 0 ? -point.x : point.x);
	uint64_t b = (uint64_t)(point.y < 0 ? -point.y : point.y);

	/* Compare primitive directions instead of cross products: the latter can
	 * overflow even though each wide projected coordinate is representable. */
	while (b != 0) {
		uint64_t remainder = a%b;
		a = b; b = remainder;
	}
	point.x /= (int64_t)a;
	point.y /= (int64_t)a;
	return point;
}

static double
parameter(struct st_rip3_arc_ray point, uint64_t rx, uint64_t ry)
{
	double angle;

	if (point.x == 0) return point.y < 0 ? 90 : 270;
	if (point.y == 0) return point.x < 0 ? 180 : 0;
	if (rx == 0 || ry == 0) rx = ry = 1;
	angle = atan2(-(double)point.y/(double)ry,
	    (double)point.x/(double)rx)*57.2957795130823208768;
	return angle < 0 ? angle+360 : angle;
}

int
st_rip3_arc_axis(struct st_rip3_arc *result, uint64_t rx, uint64_t ry,
    unsigned start, unsigned end)
{
	struct st_rip3_arc arc = {0};
	struct st_rip3_arc_ray first, last;
	uint64_t radius = rx > ry ? rx : ry;

	if (result == NULL || radius > UINT64_C(1024)*INT32_MAX || start > 4095 || end > 4095)
		return IO_ERROR_INVALID_ARGUMENT;
	arc.first = ray(radius,start); arc.last = ray(radius,end);
	if (arc.first.x == arc.last.x && arc.first.y == arc.last.y)
		arc.kind = ST_RIP3_ARC_POINT;
	else if (zero(arc.first) || zero(arc.last))
		arc.kind = ST_RIP3_ARC_EMPTY;
	else {
		arc.kind = ST_RIP3_ARC_CURVE;
		arc.start = parameter(arc.first,rx,ry);
		first = direction(arc.first); last = direction(arc.last);
		if (first.x == last.x && first.y == last.y)
			arc.sweep = 360;
		else {
			double divisor_x = rx == 0 || ry == 0 ? 1 : (double)rx;
			double divisor_y = rx == 0 || ry == 0 ? 1 : (double)ry;
			double sx = (double)arc.first.x/divisor_x, sy = -(double)arc.first.y/divisor_y;
			double ex = (double)arc.last.x/divisor_x, ey = -(double)arc.last.y/divisor_y;

			/* atan2(cross,dot) retains a tiny positive span even if subtracting
			 * two rounded parameter angles would erase it on a thin ellipse. */
			arc.sweep = atan2(sx*ey-sy*ex,sx*ex+sy*ey)*57.2957795130823208768;
			if (arc.sweep <= 0) arc.sweep += 360;
		}
	}
	*result = arc;
	return 0;
}
