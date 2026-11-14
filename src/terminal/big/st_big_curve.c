/* BIG's WORD-angle, metric and circular-aspect scalar adapters (13/61). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_curve.h"
#include "st_big_number.h"
#include "st_bgi_trig.h"
#include "io_error.h"
#include <stddef.h>

int32_t st_big_curve_sin(uint16_t angle)
{
	return st_bgi_sin32((int)st_big_number_signed(angle));
}
int32_t st_big_curve_cos(uint16_t angle)
{
	return st_big_curve_sin((uint16_t)(angle+90u));
}
uint16_t st_big_curve_metric(uint16_t x, uint16_t y)
{
	/* NEG precedes the sign branch; in particular8000 remains negative. */
	uint16_t neg_y=(uint16_t)(0u-y);
	if (st_big_number_signed(x)>=0) {
		if (st_big_number_signed(neg_y)>=0) return (uint16_t)(neg_y-x);
		return (uint16_t)(x+6000u+neg_y);
	}
	if (st_big_number_signed(neg_y)>=0) return (uint16_t)(0u-x+2000u-neg_y);
	return (uint16_t)(x+4000u-neg_y);
}
int st_big_curve_aspect(uint16_t radius, uint16_t aspect_x, uint16_t aspect_y, uint16_t *radius_y)
{
	if (radius_y==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int32_t denominator=st_big_number_signed(aspect_y);
	if (denominator==0) return IO_ERROR_OUT_OF_RANGE;
	/* Unsigned16 times signed16 fits signed32, including radiusffff and
	 * aspect8000. Its quotient by a nonzero signed16 also fits signed32. */
	int32_t numerator=(int32_t)radius*st_big_number_signed(aspect_x);
	*radius_y=(uint16_t)(numerator/denominator);
	return 0;
}
