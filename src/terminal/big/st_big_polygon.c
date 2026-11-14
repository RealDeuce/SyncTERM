/* BIG DDO0b03..0d67: original live-record polygon scan conversion (60). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_polygon.h"
#include "st_big_producer.h"
#include "st_big_number.h"
#include "io_error.h"
#include <stddef.h>

/* Original twelve five-byte records, SHA256
 * 7dde6db8627f4f4c515008fa2d2ad8b02a8e005502141a8fa3d77171d44e4874.
 * The loaded XOR AL,AL hook discards the last two angle bytes. */
static const uint8_t rows[]={
	0,0,1,0,0, 2,4,2,0,0x5a, 0,0,10,0xe1,0x87,
	2,10,1,0xe1,0x87, 2,2,5,0x87,0xe1, 3,2,5,0x87,0x87,
	1,0x5a,10,0,0, 1,0x87,17,0xe1,0xe1, 3,5,1,0,0,
	3,10,1,0,0, 3,5,2,0,0, 0,0,1,0,0,
};
static int synchronous(int status) { return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status; }
static int load(const struct st_big_producer *buffer, size_t word, uint16_t *value)
{
	if (buffer->points==NULL || word/2>=buffer->storage_count) return IO_ERROR_OUT_OF_RANGE;
	*value=word%2==0?buffer->points[word/2].x:buffer->points[word/2].y; return 0;
}
static int store(const struct st_big_producer *buffer, size_t word, uint16_t value)
{
	if (buffer->points==NULL || word/2>=buffer->storage_count) return IO_ERROR_OUT_OF_RANGE;
	if (word%2==0) buffer->points[word/2].x=value; else buffer->points[word/2].y=value;
	return 0;
}
static int device_color(struct st_big_state *state, const struct st_big_polygon_sink *sink, uint16_t tuple)
{
	int status=sink->color==NULL?0:synchronous(sink->color(sink->context,tuple));
	if (status==0) {
		state->bgi.device_color=(uint8_t)(tuple&15);
		state->bgi.device_fill_color=(uint8_t)((tuple>>8)&15);
	}
	return status;
}
static int bounds(const struct st_big_producer *buffer, uint16_t *minimum, uint16_t *maximum)
{
	*minimum=0x7fff; *maximum=0x8000;
	for (size_t i=0;;++i) {
		uint16_t y; int status=load(buffer,i*2+1,&y); if (status!=0) return status;
		if ((y&0xff00u)==0x8000) { if (y==0x8001) continue; return 0; }
		if (st_big_number_signed(y)<st_big_number_signed(*minimum)) *minimum=y;
		if (st_big_number_signed(y)>=st_big_number_signed(*maximum)) *maximum=y;
	}
}
static int intersection(const struct st_big_producer *buffer, size_t next, uint16_t y, uint16_t *value)
{
	/* Reload both endpoint pairs after the separate edge predicate. Native
	 * scratch shares the records, so earlier insertions may change them. */
	uint16_t x0,y0,x1,y1;
	int status=load(buffer,(next-1)*2,&x0); if (status!=0) return status;
	status=load(buffer,(next-1)*2+1,&y0); if (status!=0) return status;
	status=load(buffer,next*2,&x1); if (status!=0) return status;
	status=load(buffer,next*2+1,&y1); if (status!=0) return status;
	if (st_big_number_signed(y1)<st_big_number_signed(y0)) {
		uint16_t t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t;
	}
	int32_t dy=st_big_number_signed((uint16_t)(y1-y0));
	if (dy==0) { *value=x0; return 0; }
	int32_t numerator=st_big_number_signed((uint16_t)(y-y0))*st_big_number_signed((uint16_t)(x1-x0));
	/* A product of signed WORDs fits signed32; its division cannot incur
	 * signed32 overflow. Original IDIV still requires a signed16 quotient. */
	int32_t quotient=numerator/dy;
	if (quotient<-32768 || quotient>32767) return IO_ERROR_OUT_OF_RANGE;
	*value=(uint16_t)(x0+(uint16_t)quotient); return 0;
}
static int insert(const struct st_big_producer *buffer, size_t scratch, uint16_t capacity, uint16_t *found, uint16_t value)
{
	if (*found>=capacity) return 0;
	size_t index=(*found)++;
	while (index!=0) {
		uint16_t previous; int status=load(buffer,scratch+index-1,&previous); if (status!=0) return status;
		if (st_big_number_signed(previous)<st_big_number_signed(value)) break;
		status=store(buffer,scratch+index,previous); if (status!=0) return status;
		--index;
	}
	return store(buffer,scratch+index,value);
}
static int edges(const struct st_big_producer *buffer, size_t scratch, uint16_t capacity, uint16_t y, uint16_t *found)
{
	*found=0; uint16_t previous;
	int status=load(buffer,1,&previous); if (status!=0) return status;
	for (size_t i=1;;++i) {
		uint16_t next; status=load(buffer,i*2+1,&next); if (status!=0) return status;
		if ((next&0xff00u)==0x8000) {
			if (next!=0x8001) return 0;
			status=load(buffer,(++i)*2+1,&previous); if (status!=0) return status;
			if ((previous&0xff00u)==0x8000) return 0;
			continue;
		}
		int32_t lo=st_big_number_signed(previous),hi=st_big_number_signed(next),scan=st_big_number_signed(y);
		if (hi<lo) { int32_t t=lo; lo=hi; hi=t; }
		if (scan>=lo && scan<hi) {
			uint16_t x; status=intersection(buffer,i,y,&x); if (status!=0) return status;
			status=insert(buffer,scratch,capacity,found,x); if (status!=0) return status;
		}
		/* The original saves the predicate's next Y across helper calls;
		 * it is not reread after an aliased scratch insertion. */
		previous=next;
	}
}
static int spans(struct st_big_state *state, const struct st_big_producer *buffer,
    size_t scratch, uint16_t found, uint16_t y, const struct st_big_polygon_sink *sink)
{
	for (size_t i=0;i<found;++i) {
		uint16_t x; int status=load(buffer,scratch+i,&x); if (status!=0) return status;
		if (state->bgi.polygon_hook!=0) x&=0xff00;
		uint16_t old_x=state->bgi.polygon_pending[0],old_y=state->bgi.polygon_pending[1];
		state->bgi.polygon_pending[0]=x; state->bgi.polygon_pending[1]=y;
		if (i%2==0) continue;
		uint16_t left=x,right=old_x;
		if (left>=right) { uint16_t t=left; left=right; right=t; }
		const uint16_t points[]={left,y,right,old_y};
		if (sink->span==NULL) return IO_ERROR_INVALID_ARGUMENT;
		status=synchronous(sink->span(sink->context,state->bgi.curve_fallback!=0,points));
		if (status!=0) return status;
	}
	return 0;
}
int st_big_polygon_fill(struct st_big_state *state, const struct st_big_producer *buffer, const struct st_big_polygon_sink *sink)
{
	if (state==NULL || buffer==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (state->bgi.curve_metric>=180) state->bgi.curve_fallback=255;
	uint16_t count=buffer->count,capacity=buffer->capacity;
	/* The original raw short-count entry escapes its return stack. Keep
	 * the preceding metric prefix, then reject rather than model that RAM. */
	if (count<3) return IO_ERROR_OUT_OF_RANGE;
	size_t scratch=(size_t)count*2;
	uint16_t saved=(uint16_t)((uint8_t)state->bgi.color|((uint16_t)state->bgi.fill_low<<8));
	state->bgi.polygon_hook=0;
	uint8_t index=(uint8_t)(state->bgi.device_fill_style-1u);
	if (index<128) {
		if (state->bgi.curve_fallback==0) index=0;
		size_t offset=(uint8_t)(index*5u);
		if (offset>sizeof(rows)-5) return IO_ERROR_OUT_OF_RANGE;
		uint8_t mode=rows[offset]; uint16_t period=rows[offset+1];
		uint16_t step=(uint16_t)((uint32_t)rows[offset+2]*state->bgi.line_distance);
		uint16_t color=state->bgi.fill_low;
		int status=device_color(state,sink,(uint16_t)(color|(color<<8))); if (status!=0) return status;
		uint16_t minimum,maximum;
		status=bounds(buffer,&minimum,&maximum); if (status!=0) return status;
		uint16_t y=minimum,counter=0;
		for (;;) {
			uint16_t found; status=edges(buffer,scratch,capacity,y,&found); if (status!=0) return status;
			status=spans(state,buffer,scratch,found,y,sink); if (status!=0) return status;
			uint16_t advance=step;
			if (mode>=2) {
				counter=(uint16_t)(counter+1u);
				if (counter==period) { counter=0; advance=(uint16_t)((uint32_t)step*period); }
			}
			y=(uint16_t)(y+advance);
			if (st_big_number_signed(y)>=st_big_number_signed(maximum)) break;
			/* Counter advancement is cyclic and each Y update is modular
			 * addition: this captured transition is invertible. Every state
			 * is on a cycle, so returning to its initial row/counter proves
			 * nontermination. No finite row limit or extra callbacks. */
			if (y==minimum && counter==0) return IO_ERROR_OUT_OF_RANGE;
		}
	}
	/* The loaded post-pass XOR AL,AL hook clears carry: exactly one pass.
	 * Normal029f restores captured DDO bytes before its raw device call. */
	state->bgi.color=(uint8_t)saved; state->bgi.fill_low=(uint8_t)(saved>>8);
	return device_color(state,sink,saved);
}
