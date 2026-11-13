/* X8's inclusive setter/getter walks, reference26/55 and palette caller. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_palette.h"
#include "st_big_number.h"
#include "io_error.h"

static uint16_t signed_byte(uint8_t value)
{
	return (uint16_t)((value&0x80)?0xff00u|value:value);
}
static int local_byte(const struct st_big_state *s, uint16_t index, uint16_t *value)
{
	if (index>17) return IO_ERROR_OUT_OF_RANGE;
	*value=signed_byte(index==17?s->palette_padding:s->local_palette[index]);
	return 0;
}
int st_big_palette_rotation_begin(struct st_big_palette_rotation *r, struct st_big_state *s,
	uint16_t first, uint16_t last, uint16_t count, uint16_t delay)
{
	if (r==NULL || s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	r->first=first; r->last=last; r->count=count; r->delay=delay;
	if (first==1 && last==1 && count==1 && delay==1) {
		st_big_state_palette_snapshot(s);
		for (size_t i=0;i<16;++i) s->saved_palette[i]=signed_byte(s->local_palette[i+1]);
	}
	if (r->first>15) r->first=15;
	if (r->last>15) r->last=15;
	return 0;
}
int st_big_palette_rotation_turn(struct st_big_palette_rotation *r, struct st_big_state *s,
	const struct st_big_state_sink *sink)
{
	if (r==NULL || s==NULL || sink==NULL || sink->palette==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status=0;
	if (st_big_number_signed(r->first)<st_big_number_signed(r->last)) {
		status=local_byte(s,(uint16_t)(r->last+1u),&r->saved);
		if (status!=0) return status;
		r->index=r->last;
		while (st_big_number_signed(r->index)>=st_big_number_signed(r->first)) {
			uint16_t value;
			status=local_byte(s,r->index,&value);
			if (status==0) status=st_big_state_palette(s,r->index,value,sink);
			if (status!=0) return status;
			st_big_state_palette_snapshot(s);
			r->index=(uint16_t)(r->index-1u);
		}
		status=st_big_state_palette(s,r->first,r->saved,sink);
		if (status!=0) return status;
	}
	/* The original uses two tests, so an owner mutation may reach both. */
	if (st_big_number_signed(r->first)>st_big_number_signed(r->last)) {
		status=local_byte(s,(uint16_t)(r->last+1u),&r->saved);
		if (status!=0) return status;
		r->index=r->last;
		while (st_big_number_signed(r->index)<=st_big_number_signed(r->first)) {
			uint16_t value;
			status=local_byte(s,(uint16_t)(r->index+2u),&value);
			if (status==0) status=st_big_state_palette(s,r->index,value,sink);
			if (status!=0) return status;
			st_big_state_palette_snapshot(s);
			r->index=(uint16_t)(r->index+1u);
		}
		status=st_big_state_palette(s,r->first,r->saved,sink);
	}
	return status;
}
int st_big_palette_rotation_restore(struct st_big_palette_rotation *r, struct st_big_state *s,
	const struct st_big_state_sink *sink)
{
	if (r==NULL || s==NULL || sink==NULL || sink->palette==NULL) return IO_ERROR_INVALID_ARGUMENT;
	for (r->index=0;st_big_number_signed(r->index)<=15;r->index=(uint16_t)(r->index+1u)) {
		if (r->index>=16) return IO_ERROR_OUT_OF_RANGE;
		int status=st_big_state_palette(s,r->index,s->saved_palette[r->index],sink);
		if (status!=0) return status;
	}
	return 0;
}
