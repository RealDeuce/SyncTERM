/* X4 zone producer: immediate stores and counted auxiliary reads (32/64). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "io_error.h"

int cterm_big_zone_field(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count<2) return 0;
	if (!view->arguments[view->count-1].has_value) return IO_ERROR_PROTOCOL;
	if (view->count==2) big->zone.id=st_big_zone_id((uint16_t)view->arguments[1].value);
	if (view->count>=3 && view->count<=6) {
		/* Fail at the actual store, after acquiring its coordinate. Negative
		 * original ids overwrite unrelated DOS state, outside our owners. */
		if (big->zone.id<0 || big->zone.id>=ST_BIG_ZONE_COUNT) return IO_ERROR_OUT_OF_RANGE;
		big->control.zones.rectangles[big->zone.id][view->count-3]=
		    (uint16_t)view->arguments[view->count-1].value;
	}
	return 0;
}
int cterm_big_zone_begin(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count<2 || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
	int32_t id=st_big_zone_id((uint16_t)view->arguments[1].value);
	if (id>=9997) {
		if (view->count!=2) return IO_ERROR_PROTOCOL;
		if (id==9999) {
			for (size_t i=0;i<ST_BIG_ZONE_COUNT;++i)
				for (size_t j=0;j<4;++j) big->control.zones.rectangles[i][j]=UINT16_MAX;
		} else big->control.zones.loopback=id==9998?1:0;
		return 0;
	}
	if (view->count!=7 || !view->arguments[6].has_value) return IO_ERROR_PROTOCOL;
	if (id<0 || id>=ST_BIG_ZONE_COUNT) return IO_ERROR_OUT_OF_RANGE;
	int32_t length=st_big_number_signed((uint16_t)view->arguments[6].value);
	big->control.zones.payload[(size_t)id*ST_BIG_ZONE_BYTES]=(uint8_t)(length<1?1:length>80?80:length);
	big->zone=(struct cterm_big_zone){.id=id,.index=1,.chained=view->chained,.phase=CTERM_BIG_ZONE_NEXT};
	if (big->numeric.loop.active==1) {
		st_big_flow_begin(&big->zone.flow,&big->control,true); big->zone.phase=CTERM_BIG_ZONE_FLOW;
	}
	return IO_ERROR_IN_PROGRESS;
}
int cterm_big_zone_poll(struct cterm_big *big, const uint8_t *bytes, size_t size,
    size_t *used, size_t *output_budget, size_t *work_budget, bool *complete)
{
	struct cterm_big_zone *z=&big->zone;
	*used=0; *complete=false; z->cooperative=false;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		if (z->id<0 || z->id>=ST_BIG_ZONE_COUNT) return IO_ERROR_OUT_OF_RANGE;
		size_t base=(size_t)z->id*ST_BIG_ZONE_BYTES;
		int status;
		switch (z->phase) {
		case CTERM_BIG_ZONE_FLOW:
			status=st_big_flow_poll(&z->flow,&big->control,&big->control_io,output_budget);
			if (status!=0) return status;
			z->phase=CTERM_BIG_ZONE_NEXT;
			break;
		case CTERM_BIG_ZONE_NEXT:
			if (*work_budget==0) { z->cooperative=true; return IO_ERROR_IN_PROGRESS; }
			/* Once admitted, the extra read stays a discard even if its
			 * waiting callback changes the live length. */
			z->extra=z->index>big->control.zones.payload[base];
			status=st_big_stream_aux(&z->stream,&big->numeric);
			if (status!=0) return status;
			z->phase=CTERM_BIG_ZONE_READ;
			break;
		case CTERM_BIG_ZONE_READ: {
			size_t accepted; bool done;
			status=st_big_stream_poll(&z->stream,&big->numeric,NULL,NULL,NULL,
			    *used<size?bytes+*used:NULL,size-*used,&accepted,NULL,&done);
			*used+=accepted;
			if (status!=0 || !done) return status;
			if (big->reset_pending) return IO_ERROR_INTERRUPTED;
			--*work_budget;
			if (z->extra) { z->phase=CTERM_BIG_ZONE_OFF; *complete=true; return 0; }
			/* Corrupt lengths may cross records inside the flat owned table.
			 * Reject only an actual escape, retaining the consumed prefix. */
			if (z->index>=ST_BIG_ZONE_DATA_SIZE-base) return IO_ERROR_OUT_OF_RANGE;
			big->control.zones.payload[base+z->index++]=z->stream.byte;
			z->phase=CTERM_BIG_ZONE_NEXT;
			break;
		}
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
