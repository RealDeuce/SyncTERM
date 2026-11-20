/* Ordinary RIP3 TTY placement (1.4 terminal/ANSI view). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_text_window.h"
#include "st_rip3_surface.h"
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
	struct st_rip3_frame checked;
	int status=st_rip3_frame_init_device(&checked,device->device_width,device->device_height);
	if (status!=0) return status;
	if (font>=5 || wrap>1) return IO_ERROR_OUT_OF_RANGE;
	/* Normalize before ordinary-definition identity is tested by the owner.
	 * Out-of-grid coordinates adjust, rather than reject, the definition. */
	if (x0>x1) {uint32_t saved=x0;x0=x1;x1=saved;}
	if (y0>y1) {uint32_t saved=y0;y0=y1;y1=saved;}
	if (x0>=columns[font]) x0=columns[font]-1;
	if (x1>=columns[font]) x1=columns[font]-1;
	if (y0>=rows[font]) y0=rows[font]-1;
	if (y1>=rows[font]) y1=rows[font]-1;
	uint32_t cw=device->device_width/columns[font],ch=device->device_height/rows[font];
	if (cw==0 || ch==0) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_text_window_layout value={.x0=x0,.y0=y0,.x1=x1,.y1=y1,
	    .font=font,.wrap=wrap!=0,.columns=x1-x0+1,.rows=y1-y0+1,
	    .cell_width=cw,.cell_height=ch,
	    .x=(uint32_t)((uint64_t)x0*reference_width[font]*device->device_width/640),
	    .y=(uint32_t)((uint64_t)y0*reference_height[font]*device->device_height/350)};
	value.bound_x=value.x;value.bound_y=value.y;
	value.bound_width=(uint64_t)value.columns*cw;value.bound_height=(uint64_t)value.rows*ch;
	*result=value;return 0;
}

bool
st_rip3_text_window_same(const struct st_rip3_text_window_layout *left,
    const struct st_rip3_text_window_layout *right)
{
	return left!=NULL && right!=NULL && !left->extended && !right->extended
	    && left->x0==right->x0 && left->y0==right->y0
	    && left->x1==right->x1 && left->y1==right->y1 && left->font==right->font;
}

int
st_rip3_text_window_extended(struct st_rip3_text_window_layout *result,
    const struct st_rip3_frame *device, const struct st_rip3_text_cell cells[5],
    uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
    uint32_t columns, uint32_t rows, unsigned font, unsigned flags)
{
	static const int next[5][3]={{1,-1,-1},{-1,-1,-1},{3,0,1},{-1,1,-1},{2,0,0}};
	if (result==NULL || device==NULL || cells==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_frame checked=*device;
	int status=st_rip3_frame_world(&checked,device->world_width,device->world_height);
	if (status!=0) return status;
	if (flags>63 || columns==0 || rows==0 || (!(flags&8) && font>4)) return IO_ERROR_OUT_OF_RANGE;
	uint64_t left=(uint64_t)x0*device->device_width/device->world_width;
	uint64_t top=(uint64_t)y0*device->device_height/device->world_height;
	uint64_t right=(uint64_t)x1*device->device_width/device->world_width;
	uint64_t bottom=(uint64_t)y1*device->device_height/device->world_height;
	if (left>right) {uint64_t saved=left;left=right;right=saved;}
	if (top>bottom) {uint64_t saved=top;top=bottom;bottom=saved;}
	if (left==right || top==bottom) return IO_ERROR_OUT_OF_RANGE;
	uint64_t width=right-left,height=bottom-top;
	if (flags&8) font=4;
	for (;;) {
		if (cells[font].width==0 || cells[font].height==0) return IO_ERROR_NOT_SUPPORTED;
		bool wide=(uint64_t)columns*cells[font].width>width;
		bool tall=(uint64_t)rows*cells[font].height>height;
		if (!wide && !tall) break;
		int candidate=next[font][wide ? tall ? 2 : 0 : 1];
		if (candidate<0) break;
		font=(unsigned)candidate;
	}
	uint32_t cw=cells[font].width,ch=cells[font].height;
	if ((uint64_t)columns>width/cw) columns=(uint32_t)(width/cw);
	if ((uint64_t)rows>height/ch) rows=(uint32_t)(height/ch);
	if (columns==0) {columns=1;width=cw;}
	if (rows==0) {rows=1;height=ch;}
	struct st_rip3_text_window_layout value={.font=font,.wrap=(flags&1)!=0,
	    .extended=true,.flags=flags,.bound_x=left,.bound_y=top,.bound_width=width,.bound_height=height,
	    .columns=columns,.rows=rows,.cell_width=cw,.cell_height=ch,
	    .x=left+(width-(uint64_t)columns*cw)/2,.y=top+(height-(uint64_t)rows*ch)/2};
	*result=value;return 0;
}

int
st_rip3_text_window_bounds(const struct st_rip3_text_window_layout *layout,
    uint32_t width, uint32_t height, struct st_rip3_rect *result)
{
	if (layout==NULL || result==NULL || width==0 || height==0 || width>INT32_MAX || height>INT32_MAX
	    || layout->bound_width==0 || layout->bound_height==0) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_rect value={0,0,-1,-1};
	if (layout->bound_x<width && layout->bound_y<height) {
		uint64_t w=width-layout->bound_x,h=height-layout->bound_y;
		if (layout->bound_width<w) w=layout->bound_width;
		if (layout->bound_height<h) h=layout->bound_height;
		value=(struct st_rip3_rect){(int32_t)layout->bound_x,(int32_t)layout->bound_y,
		    (int32_t)(layout->bound_x+w-1),(int32_t)(layout->bound_y+h-1)};
	}
	*result=value;return 0;
}
