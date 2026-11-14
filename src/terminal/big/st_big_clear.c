/* Native indexed adaptation of original clear/blank write schedules. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_clear.h"
#include "st_big_draw.h"
#include "io_error.h"
#include <stddef.h>
static int write_byte(const struct st_big_memory_sink *sink, uint16_t address,
    uint8_t planes, uint8_t pen)
{
	const uint8_t pens[]={pen,pen,pen,pen,pen,pen,pen,pen};
	int status=sink->write(sink->context,address,255,planes,pens,0);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
int st_big_clear_device(struct st_big_state *state, const struct st_big_memory_sink *sink)
{
	if (state==NULL || sink==NULL || sink->write==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (state->bgi.state==0) { state->bgi.error=-1; return IO_ERROR_PROTOCOL; }
	const struct st_big_mode *mode=st_big_mode((unsigned)state->bgi.mode+1);
	if (mode==NULL) return IO_ERROR_OUT_OF_RANGE;
	uint16_t words=(uint16_t)((uint16_t)(mode->page_bytes/16)<<3);
	for (uint32_t address=0;address<(uint32_t)words*2u;++address) {
		int status=write_byte(sink,(uint16_t)address,15,0); if (status!=0) return status;
	}
	return st_big_draw_move(state,0,0);
}
int st_big_clear_console(const uint8_t bounds[4], uint8_t attribute,
    const struct st_big_mode *mode, uint8_t page, const struct st_big_memory_sink *sink)
{
	if (bounds==NULL || mode==NULL || sink==NULL || sink->write==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t columns=(uint16_t)mode->columns,points=(uint16_t)mode->cell_height;
	uint16_t packed=(uint16_t)(((uint16_t)bounds[3]<<8)|bounds[2]);
	packed=(uint16_t)(packed-(((uint16_t)bounds[1]<<8)|bounds[0])+0x101u);
	unsigned width=packed&255u;
	uint16_t scanlines=(uint16_t)((packed>>8)*points);
	uint16_t address=(uint16_t)((uint16_t)((uint32_t)bounds[1]*(columns&255u)*points)
	    +bounds[0]+(uint32_t)page*mode->visual_page_bytes);
	/* The declared native VGA profile captures stride/POINTS before the
	 * blank. Original LOOP is body-first: zero means a full WORD turn. */
	uint32_t count=scanlines==0?65536u:scanlines;
	for (uint32_t row=0;row<count;++row) {
		for (unsigned pass=0;pass<2;++pass) for (unsigned x=0;x<width;++x) {
			int status=write_byte(sink,(uint16_t)(address+x),pass==0?15:(uint8_t)(attribute&15u),pass==0?0:15);
			if (status!=0) return status;
		}
		address=(uint16_t)(address+columns);
	}
	return 0;
}
