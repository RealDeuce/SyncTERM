/* Ordinary RIP3 TTY placement (1.4 terminal/ANSI view). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_text_window.h"
#include "io_error.h"
#include <stddef.h>

int
st_rip3_text_window_normal(struct st_rip3_text_window_layout *result,
    const struct st_rip3_frame *device, uint32_t x0, uint32_t y0,
    uint32_t x1, uint32_t y1, unsigned wrap, unsigned font)
{
	static const unsigned columns[]={80,91,80,91,40},rows[]={43,43,25,25,25};
	static const unsigned reference_width[]={8,7,8,7,16},reference_height[]={8,8,14,14,14};
	if (result==NULL || device==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_frame checked=*device;
	int status=st_rip3_frame_world(&checked,device->world_width,device->world_height);
	if (status!=0) return status;
	if (font>=5 || wrap>1 || x0>x1 || y0>y1
	    || x1>=columns[font] || y1>=rows[font]) return IO_ERROR_OUT_OF_RANGE;
	uint32_t cw=device->device_width/columns[font],ch=device->device_height/rows[font];
	if (cw==0 || ch==0) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_text_window_layout value={.x0=x0,.y0=y0,.x1=x1,.y1=y1,
	    .font=font,.wrap=wrap!=0,.columns=x1-x0+1,.rows=y1-y0+1,
	    .cell_width=cw,.cell_height=ch,
	    .x=(uint32_t)((uint64_t)x0*reference_width[font]*device->device_width/640),
	    .y=(uint32_t)((uint64_t)y0*reference_height[font]*device->device_height/350)};
	*result=value;return 0;
}

bool
st_rip3_text_window_same(const struct st_rip3_text_window_layout *left,
    const struct st_rip3_text_window_layout *right)
{
	return left!=NULL && right!=NULL && left->x0==right->x0 && left->y0==right->y0
	    && left->x1==right->x1 && left->y1==right->y1 && left->font==right->font;
}
