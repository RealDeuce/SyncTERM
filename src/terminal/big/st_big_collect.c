/* BIG original input count/byte/zone operators, references32/64/81. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_collect.h"
#include "st_big_number.h"
#include "io_error.h"
#include <stddef.h>

void st_big_collect_cancel(struct st_big_collect *r)
{
	if (r!=NULL) *r=(struct st_big_collect){0};
}
int st_big_collect_begin(struct st_big_collect *r, struct st_big_control *c, uint16_t type, uint16_t options)
{
	if (r==NULL || c==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*r=(struct st_big_collect){.type=type,.options=options,.active=true};
	c->input.pending=1; c->input.count=0;
	if (options==2) c->input.pending=0;
	if (options==3) { r->options=0; c->input.pending=0; }
	return 0;
}
static int fail(struct st_big_collect *r, int status)
{
	if (status==IO_ERROR_IN_PROGRESS) status=IO_ERROR_PROTOCOL;
	r->error=status; return status;
}
static int echo(struct st_big_collect *r, const struct st_big_collect_sink *sink, uint8_t byte)
{
	int status=sink->echo(sink->context,byte);
	return status==0?0:fail(r,status);
}
static bool valid(const struct st_big_collect *r, const struct st_big_control *c, const struct st_big_collect_sink *sink)
{
	return r!=NULL && c!=NULL && sink!=NULL && sink->echo!=NULL && r->active;
}
int st_big_collect_key(struct st_big_collect *r, struct st_big_control *c, uint8_t key,
    const struct st_big_collect_sink *sink)
{
	if (!valid(r,c,sink) || r->type==2) return IO_ERROR_INVALID_ARGUMENT;
	if (r->error!=0) return r->error;
	if (key==127 || (key==8 && st_big_number_signed(c->input.count)>0))
		c->input.count=(uint16_t)(c->input.count-1u);
	if (key>=32 && key<127 && st_big_number_signed(c->input.count)<129) {
		c->input.count=(uint16_t)(c->input.count+1u);
		/* DEL can wrap back to position0. Further escaped positions fail
		 * at the store after the original count mutation, not beforehand. */
		if (c->input.count>=ST_BIG_REPLY_CAPACITY) return fail(r,IO_ERROR_OUT_OF_RANGE);
		c->input.payload[c->input.count]=key;
		if (r->options!=0) {
			int status=echo(r,sink,c->input.payload[c->input.count]);
			if (status!=0) return status;
		}
	}
	if ((r->options!=0 && key==127) || key==8) {
		int status=echo(r,sink,' '); if (status!=0) return status;
		status=echo(r,sink,8); if (status!=0) return status;
	}
	if (key==13) r->type=0;
	if (st_big_number_signed(r->type)<=0) r->active=false;
	return 0;
}
int st_big_collect_zone(struct st_big_collect *r, struct st_big_control *c, uint16_t x, uint16_t y,
    const struct st_big_collect_sink *sink, bool *retry)
{
	if (retry!=NULL) *retry=false;
	if (!valid(r,c,sink) || r->type!=2 || retry==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (r->error!=0) return r->error;
	size_t zone;
	for (zone=0;zone<ST_BIG_ZONE_COUNT;++zone) {
		const uint16_t *b=c->zones.rectangles[zone];
		if (st_big_number_signed(x)>=st_big_number_signed(b[0]) && st_big_number_signed(x)<=st_big_number_signed(b[2])
		    && st_big_number_signed(y)>=st_big_number_signed(b[1]) && st_big_number_signed(y)<=st_big_number_signed(b[3])) break;
	}
	if (zone==ST_BIG_ZONE_COUNT) {
		if (c->zones.loopback!=0) {
			int status=echo(r,sink,7); if (status!=0) return status;
			*retry=true; return 0;
		}
		zone=ST_BIG_ZONE_COUNT-1;
	}
	size_t base=zone*ST_BIG_ZONE_BYTES;
	c->input.count=c->zones.payload[base];
	for (uint16_t i=1;i<=c->input.count;++i) {
		if (i>=ST_BIG_REPLY_CAPACITY || i>=ST_BIG_ZONE_DATA_SIZE-base) return fail(r,IO_ERROR_OUT_OF_RANGE);
		c->input.payload[i]=c->zones.payload[base+i];
	}
	r->type=0; r->active=false; return 0;
}
void st_big_collect_move(const struct st_big_control *c, uint16_t max_x, uint16_t max_y,
    unsigned direction, uint16_t *x, uint16_t *y)
{
	switch (direction) {
	case 1:
		*y=(uint16_t)(*y-c->pointer.step_y); if (st_big_number_signed(*y)<0) *y=0; break;
	case 2:
		*y=(uint16_t)(*y+c->pointer.step_y); if (st_big_number_signed(*y)>st_big_number_signed(max_y)) *y=max_y; break;
	case 3:
		*x=(uint16_t)(*x+c->pointer.step_x); if (st_big_number_signed(*x)>st_big_number_signed(max_x)) *x=max_x; break;
	case 4:
		*x=(uint16_t)(*x-c->pointer.step_x); if (st_big_number_signed(*x)<0) *x=0; break;
	default: break;
	}
}
