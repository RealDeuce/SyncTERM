/* BIG's WORD-angle, metric and circular-aspect scalar adapters (13/61). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_curve.h"
#include "st_big_number.h"
#include "st_big_producer.h"
#include "st_bgi_trig.h"
#include "io_error.h"
#include <stddef.h>
#include <stdbool.h>

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

static bool pair_less(uint32_t a, uint32_t b)
{
	/* Signed HIGH comparison, then unsigned LOW: signed32 bit ordering
	 * without converting an out-of-range unsigned value to signed. */
	return (a^UINT32_C(0x80000000))<(b^UINT32_C(0x80000000));
}
static bool checked_product(uint32_t value, uint16_t factor, uint32_t *result)
{
	if (factor!=0 && value>UINT32_MAX/factor) return false;
	*result=value*factor; return true;
}
static int decimal_points(const struct st_big_curve_decimal_frame *frame,
    const struct st_big_curve_point_sink *sink, uint16_t x, uint16_t y,
    int32_t start, int32_t end, bool inverse)
{
	uint16_t nx=(uint16_t)(0u-x),ny=(uint16_t)(0u-y);
	const uint16_t candidates[4][2]={{x,y},{nx,y},{x,ny},{nx,ny}};
	for (size_t i=0;i<4;++i) {
		int32_t metric=st_big_number_signed(st_big_curve_metric(candidates[i][0],candidates[i][1]));
		if (inverse?(metric<start && metric>end):(metric<start || metric>end)) continue;
		int status=sink->point(sink->context,(uint16_t)(frame->center[0]+candidates[i][0]),
		    (uint16_t)(frame->center[1]+candidates[i][1]));
		if (status!=0) return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
	}
	return 0;
}
int st_big_curve_decimal_outline(struct st_big_curve_decimal_frame frame,
    const struct st_big_curve_point_sink *sink)
{
	if (sink==NULL || sink->point==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int32_t start=st_big_number_signed(st_big_curve_metric(frame.start[0],frame.start[1]));
	int32_t end=st_big_number_signed(st_big_curve_metric(frame.end[0],frame.end[1]));
	bool inverse=end<=start;
	uint16_t rx=frame.radius[0]==0?1:frame.radius[0], ry=frame.radius[1]==0?1:frame.radius[1];
	uint16_t maximum=rx>ry?rx:ry;
	uint32_t base;
	if (!checked_product((uint32_t)maximum*maximum,100,&base)) return 0;
	uint32_t step=base/rx/rx,constant=base/ry/ry,temp,product;
	if (!checked_product(constant,ry,&temp) || !checked_product(temp,ry,&product)) return 0;
	uint32_t error=product-base,delta=temp*2u,accumulated=0;
	uint16_t x=0,y=ry;
	do {
		int status=decimal_points(&frame,sink,x,y,start,end,inverse);
		if (status!=0) return status;
		uint32_t increment=accumulated+step,threshold=delta-constant;
		if (!pair_less(error*2u+increment*2u,threshold)) {
			y=(uint16_t)(y-1u); error-=threshold; delta=threshold-constant;
		}
		x=(uint16_t)(x+1u); error+=increment; accumulated=increment+step;
	} while (pair_less(accumulated,delta));
	/* The original second regime enters its body before the Y sign test,
	 * and halves the increment logically even when its high sign bit is set. */
	do {
		int status=decimal_points(&frame,sink,x,y,start,end,inverse);
		if (status!=0) return status;
		uint32_t increment=accumulated+step,threshold=delta-constant;
		if (!pair_less(threshold,(increment>>1)+error)) {
			x=(uint16_t)(x+1u); error+=increment; accumulated=increment+step;
		}
		y=(uint16_t)(y-1u); error-=threshold; delta=threshold-constant;
	} while ((y&0x8000u)==0);
	return 0;
}

struct decimal_drawing {
	struct st_big_state *state;
	struct st_big_producer *producer;
	const struct st_big_producer_sink *sink;
};
static int decimal_move(void *context, uint16_t x, uint16_t y)
{
	struct decimal_drawing *drawing=context;
	struct st_big_producer_registers registers={.ax=x,.bx=y};
	return st_big_producer_move(drawing->state,drawing->producer,&registers,drawing->sink);
}
static int synchronous(int status) { return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status; }
int st_big_curve_decimal_draw(struct st_big_state *state, struct st_big_producer *producer,
    struct st_big_curve_decimal_frame frame, const struct st_big_producer_sink *sink,
    const struct st_big_curve_batch_sink *batch)
{
	if (state==NULL || producer==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t color=state->bgi.color;
	if (state->bgi.curve_batch_gate==0) {
		if (batch==NULL) producer->batch=true;
		else {
			if (batch->begin==NULL) return IO_ERROR_INVALID_ARGUMENT;
			int status=synchronous(batch->begin(batch->context,&color));
			if (status!=0) return status;
		}
	}
	/* Ordinary sixteen-pen begin converts no colour bits. The returned
	 * WORD is still published before limits/products, even on empty output. */
	state->bgi.color=color;
	struct decimal_drawing drawing={state,producer,sink};
	const struct st_big_curve_point_sink points={&drawing,decimal_move};
	int status=st_big_curve_decimal_outline(frame,&points);
	if (status!=0) return status;
	/* End uses the current gate and binding; it is neither an entry-time
	 * paired decision nor an unconditional failure-cleanup operation. */
	if (state->bgi.curve_batch_gate==0) {
		if (batch==NULL) producer->batch=false;
		else {
			if (batch->end==NULL) return IO_ERROR_INVALID_ARGUMENT;
			return synchronous(batch->end(batch->context));
		}
	}
	return 0;
}
