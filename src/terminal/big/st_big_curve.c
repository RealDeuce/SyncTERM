/* BIG original curve scalars/fronts and indexed producer binding (13/61). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_curve.h"
#include "st_big_number.h"
#include "st_big_producer.h"
#include "st_bgi_trig.h"
#include "st_bgi_data.h"
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

static int regular_piece(struct st_big_state *state, struct st_big_producer *producer,
    uint16_t start, uint16_t end, const struct st_big_producer_sink *sink,
    const struct st_big_curve_batch_sink *batch)
{
	struct st_big_curve_decimal_frame frame={
	    .center={state->bgi.ddo_x,state->bgi.ddo_y},
	    .radius={state->bgi.curve_radius[0],state->bgi.curve_radius[1]}};
	if (state->bgi.orientation!=0) { start=(uint16_t)(start+90u); end=(uint16_t)(end+90u); }
	state->bgi.curve_span=(uint16_t)(end-start);
	if (state->bgi.curve_span==0) {
		/*09e9 captures stale first offsets only on its first reached call.
		 * Nonzero regular arcs return before this bookkeeping site. */
		if (state->bgi.curve_first_valid==0) {
			state->bgi.curve_first_valid=255;
			state->bgi.curve_first[0]=state->bgi.curve_offsets[0];
			state->bgi.curve_first[1]=state->bgi.curve_offsets[1];
		}
		return 0;
	}
	state->bgi.curve_offsets[0]=(uint16_t)st_bgi_trig_mul(st_big_curve_cos(start),state->bgi.curve_radius[0]);
	state->bgi.curve_offsets[1]=(uint16_t)(0u-(uint16_t)st_bgi_trig_mul(st_big_curve_sin(start),state->bgi.curve_radius[1]));
	state->bgi.curve_offsets[2]=(uint16_t)st_bgi_trig_mul(st_big_curve_cos(end),state->bgi.curve_radius[0]);
	state->bgi.curve_offsets[3]=(uint16_t)(0u-(uint16_t)st_bgi_trig_mul(st_big_curve_sin(end),state->bgi.curve_radius[1]));
	frame.start[0]=state->bgi.curve_offsets[0]; frame.start[1]=state->bgi.curve_offsets[1];
	frame.end[0]=state->bgi.curve_offsets[2]; frame.end[1]=state->bgi.curve_offsets[3];
	producer->mode=2;
	bool equal=state->bgi.curve_offsets[0]==state->bgi.curve_offsets[2]
	    && state->bgi.curve_offsets[1]==state->bgi.curve_offsets[3];
	if (state->bgi.curve_span<(equal?350u:2u)) {
		struct st_big_producer_registers registers={
		    .ax=(uint16_t)(state->bgi.ddo_x+state->bgi.curve_offsets[2]),
		    .bx=(uint16_t)(state->bgi.ddo_y+state->bgi.curve_offsets[3])};
		return st_big_producer_move(state,producer,&registers,sink);
	}
	return st_big_curve_decimal_draw(state,producer,frame,sink,batch);
}
static void publish_arc(struct st_big_state *state)
{
	/* A55's DI advances from0304 to0308 after the center stores, while
	 * SI restarts at0314. Published points never overwrite these offsets. */
	state->bgi.arc[0]=(uint16_t)(state->bgi.arc[0]-state->bgi.viewport[0]);
	uint16_t x=state->bgi.arc[0];
	state->bgi.arc[1]=(uint16_t)(state->bgi.arc[1]-state->bgi.viewport[1]);
	uint16_t y=state->bgi.arc[1];
	state->bgi.arc[2]=(uint16_t)(state->bgi.curve_offsets[0]+x);
	state->bgi.arc[3]=(uint16_t)(state->bgi.curve_offsets[1]+y);
	state->bgi.arc[4]=(uint16_t)(state->bgi.curve_offsets[2]+x);
	state->bgi.arc[5]=(uint16_t)(state->bgi.curve_offsets[3]+y);
}
int st_big_curve_regular_outline(struct st_big_state *state, struct st_big_producer *producer,
    uint16_t start, uint16_t end, uint16_t radius_x, uint16_t radius_y,
    const struct st_big_producer_sink *sink, const struct st_big_curve_batch_sink *batch)
{
	if (state==NULL || producer==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	state->bgi.curve_filled=0;
	state->bgi.curve_radius[0]=radius_x; state->bgi.curve_radius[1]=radius_y;
	state->bgi.arc[0]=state->bgi.ddo_x; state->bgi.arc[1]=state->bgi.ddo_y;
	int status=regular_piece(state,producer,start,end,sink,batch);
	if (status!=0) return status;
	publish_arc(state);
	producer->mode=0;
	return 0;
}

static int angular_outline(struct st_big_state *state, struct st_big_producer *producer,
    uint16_t start, uint16_t end, const struct st_big_producer_sink *sink)
{
	if (start>=end) end=(uint16_t)(end+360u);
	if (state->bgi.orientation!=0) { start=(uint16_t)(start+90u); end=(uint16_t)(end+90u); }
	uint16_t first=start;
	for (;;) {
		uint16_t x=(uint16_t)st_bgi_trig_mul(st_big_curve_cos(start),state->bgi.curve_radius[0]);
		uint16_t y=(uint16_t)(0u-(uint16_t)st_bgi_trig_mul(st_big_curve_sin(start),state->bgi.curve_radius[1]));
		struct st_big_producer_registers r={.ax=(uint16_t)(x+state->bgi.ddo_x),.bx=(uint16_t)(y+state->bgi.ddo_y)};
		int status=st_big_producer_move(state,producer,&r,sink); if (status!=0) return status;
		start=(uint16_t)(start+1u);
		if (st_big_number_signed(start)>st_big_number_signed(end)) return 0;
		/* With signed end7fff no WORD can exit JLE. Preserve one whole
		 * turn's collector effects, then return a checked native error. */
		if (start==first) return IO_ERROR_OUT_OF_RANGE;
	}
}
int st_big_curve_outline(struct st_big_state *state, struct st_big_producer *producer,
    uint16_t start, uint16_t end, uint16_t radius_x, uint16_t radius_y,
    const struct st_big_producer_sink *sink, const struct st_big_curve_batch_sink *batch)
{
	if (state==NULL || producer==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (state->bgi.ddo_line_width<3 && state->bgi.curve_fallback==0 && state->bgi.curve_metric<180)
		return st_big_curve_regular_outline(state,producer,start,end,radius_x,radius_y,sink,batch);
	state->bgi.curve_filled=0;
	state->bgi.curve_radius[0]=radius_x; state->bgi.curve_radius[1]=radius_y;
	state->bgi.arc[0]=state->bgi.ddo_x; state->bgi.arc[1]=state->bgi.ddo_y;
	/* Device-only temporary style: retained public/DDO style and width
	 * still govern later restoration and original thickness expansion. */
	state->bgi.line_mask=0xffff; state->bgi.device_line_width=1;
	int status=st_big_producer_start(producer);
	if (status==0) status=angular_outline(state,producer,start,end,sink);
	if (status!=0) return status;
	publish_arc(state);
	struct st_big_producer_registers terminator={.ax=0x8000,.bx=0x8000};
	status=st_big_producer_move(state,producer,&terminator,sink);
	if (status!=0) return status;
	producer->mode=0;
	uint16_t last_y=terminator.bx;
	status=st_big_producer_flush(state,producer,sink,&last_y);
	if (status!=0) return status;
	/* Our appended terminator establishes BX8000 on normal reader paths
	 * and on capacity/short skips. Read the result rather than restoring
	 * the retained custom user word, which is a distinct owner. */
	if (state->bgi.ddo_line_style>4) return IO_ERROR_OUT_OF_RANGE;
	state->bgi.line_mask=state->bgi.ddo_line_style==4?last_y:st_bgi_line_patterns[state->bgi.ddo_line_style];
	state->bgi.device_line_width=1;
	return 0;
}

int st_big_curve_ellipse(struct st_big_state *state, struct st_big_producer *producer,
    const struct st_big_curve_request *request, const struct st_big_producer_sink *sink,
    const struct st_big_curve_batch_sink *batch)
{
	if (state==NULL || producer==NULL || request==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (state->bgi.state==0) { state->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	/* Public selector08 bypasses the point producer and public moveto. */
	uint16_t x=request->center[0],y=request->center[1];
	state->bgi.ddo_x=state->bgi.device_x=(uint16_t)(x+state->bgi.viewport[0]);
	state->bgi.ddo_y=state->bgi.device_y=(uint16_t)(y+state->bgi.viewport[1]);
	uint16_t start=request->start,end=request->end;
	uint16_t radius_x=request->radius[0],radius_y=request->radius[1];
	if (start==0xffff) return 0;
	uint16_t saved_x=state->bgi.ddo_x,saved_y=state->bgi.ddo_y;
	int status=st_big_curve_outline(state,producer,start,end,radius_x,radius_y,sink,batch);
	if (status!=0) return status;
	state->bgi.ddo_x=saved_x; state->bgi.ddo_y=saved_y;
	return 0;
}
int st_big_curve_arc(struct st_big_state *state, struct st_big_producer *producer,
    const struct st_big_curve_request *request, const struct st_big_producer_sink *sink,
    const struct st_big_curve_batch_sink *batch)
{
	if (state==NULL || producer==NULL || request==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t radius=request->radius[0];
	/*12d9 captures the signed denominator before the numerator aspect. */
	uint16_t aspect_y=state->bgi.aspect[1];
	uint16_t aspect_x=state->bgi.aspect[0],radius_y;
	int status=st_big_curve_aspect(radius,aspect_x,aspect_y,&radius_y);
	if (status!=0) return status;
	struct st_big_curve_request ellipse=*request;
	ellipse.radius[0]=radius; ellipse.radius[1]=radius_y;
	return st_big_curve_ellipse(state,producer,&ellipse,sink,batch);
}
