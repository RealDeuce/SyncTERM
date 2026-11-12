/* BIG indexed drawing; protocol schedules stay separate from the console. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "st_big_draw.h"
#include "cterm.h"
#include "io_error.h"

struct drawing {
	struct cterm_big *big;
	const struct st_pixel_buffer *pixels;
	struct st_console_rect *changed;
};
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct drawing *draw=context;
	if (draw->big->reset_pending) return IO_ERROR_INTERRUPTED;
	if (x>=draw->pixels->width || y>=draw->pixels->height) return IO_ERROR_OUT_OF_RANGE;
	size_t index=(size_t)y*draw->pixels->width+x;
	uint32_t a=CTERM_BIG_PALETTE_FIRST+pen, b=a;
	if (xor_mode) {
		uint32_t first=draw->pixels->primary[index], second=draw->pixels->alternate[index];
		if (first<CTERM_BIG_PALETTE_FIRST || first>=CTERM_BIG_PALETTE_FIRST+16
		    || second<CTERM_BIG_PALETTE_FIRST || second>=CTERM_BIG_PALETTE_FIRST+16) return IO_ERROR_OUT_OF_RANGE;
		a=CTERM_BIG_PALETTE_FIRST+((first-CTERM_BIG_PALETTE_FIRST)^pen);
		b=CTERM_BIG_PALETTE_FIRST+((second-CTERM_BIG_PALETTE_FIRST)^pen);
	}
	draw->pixels->primary[index]=a; draw->pixels->alternate[index]=b;
	struct st_console_rect *r=draw->changed;
	if (r->width==0) *r=(struct st_console_rect){x,y,1,1};
	else {
		uint32_t right=r->x+r->width, bottom=r->y+r->height;
		if (x<r->x) r->x=x;
		if (y<r->y) r->y=y;
		if (x>=right) right=x+1u;
		if (y>=bottom) bottom=y+1u;
		r->width=right-r->x; r->height=bottom-r->y;
	}
	return 0;
}
struct marker { struct cterm_big *big; uint16_t x,y; bool direct; };
static int marker(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct marker *marker=context; struct cterm_big *big=marker->big;
	struct drawing drawing={big,pixels,changed};
	const struct st_big_draw_sink sink={&drawing,pixel};
	if (marker->direct) return st_big_draw_marker(&big->state,marker->x,marker->y,&sink);
	int status=0;
	if (big->state.application.marker==0)
		status=st_big_draw_point(&big->state,marker->x,marker->y,big->state.application.colors[0],&sink);
	if (status==0 && big->state.application.marker!=0)
		status=st_big_draw_marker(&big->state,marker->x,marker->y,&sink);
	return status!=0?status:st_big_draw_move(&big->state,marker->x,marker->y);
}
int cterm_big_draw_p(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=2 || !view->arguments[0].has_value || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
	struct marker request={.big=big,.x=(uint16_t)view->arguments[0].value,.y=(uint16_t)view->arguments[1].value};
	int status=st_console_modify_pixels(big->terminal->screen.console,marker,&request);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
int cterm_big_pmarker(struct cterm_big *big, uint16_t x, uint16_t y)
{
	struct marker request={.big=big,.x=x,.y=y,.direct=true};
	int status=st_console_modify_pixels(big->terminal->screen.console,marker,&request);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
