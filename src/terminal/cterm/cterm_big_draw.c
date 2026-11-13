/* BIG indexed drawing; protocol schedules stay separate from the console. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "st_big_draw.h"
#include "st_big_video.h"
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
struct line { struct cterm_big *big; uint8_t code; uint16_t words[4]; };
static int line(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct line *request=context; struct st_big_state *state=&request->big->state;
	struct drawing drawing={request->big,pixels,changed};
	const struct st_big_draw_sink sink={&drawing,pixel};
	const uint16_t *words=request->words;
	if (request->code=='L') {
		int status=st_big_draw_line(state,words,&sink);
		return status!=0?status:st_big_draw_move(state,words[2],words[3]);
	}
	if (request->code=='D') return st_big_draw_to(state,words[0],words[1],&sink);
	/* The original d negates its Y flag, leaving the Y displacement intact. */
	uint16_t dx=words[0]!=0?(uint16_t)(0u-words[1]):words[1];
	return st_big_draw_relative(state,dx,words[3],&sink);
}
int cterm_big_draw_line(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->code!='L' && view->code!='D' && view->code!='d') return IO_ERROR_PROTOCOL;
	if (view->count!=(view->code=='D'?2u:4u)) return IO_ERROR_PROTOCOL;
	struct line request={.big=big,.code=view->code};
	for (size_t i=0;i<view->count;++i) {
		if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
		request.words[i]=(uint16_t)view->arguments[i].value;
	}
	int status=st_console_modify_pixels(big->terminal->screen.console,line,&request);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	const struct drawing *draw=context;
	if (draw->big->reset_pending) return IO_ERROR_INTERRUPTED;
	if (plane>3 || x>draw->pixels->width || draw->pixels->width-x<8 || y>=draw->pixels->height) return IO_ERROR_OUT_OF_RANGE;
	uint8_t value=0;
	for (unsigned i=0;i<8;++i) {
		uint32_t pen=draw->pixels->primary[(size_t)y*draw->pixels->width+x+i];
		if (pen<CTERM_BIG_PALETTE_FIRST || pen>=CTERM_BIG_PALETTE_FIRST+16) return IO_ERROR_OUT_OF_RANGE;
		if (((pen-CTERM_BIG_PALETTE_FIRST)>>plane)&1u) value|=(uint8_t)(0x80u>>i);
	}
	*byte=value; return 0;
}
struct bar { struct cterm_big *big; bool depth; uint16_t size,top; };
static int bar(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct bar *request=context; struct cterm_big *big=request->big;
	struct drawing drawing={big,pixels,changed};
	const struct st_big_producer_sink sink={.drawing={&drawing,pixel},.read_context=&drawing,.read_byte=read_byte};
	if (!request->depth) return st_big_draw_bar(&big->state,big->bar.points,&sink.drawing);
	if (big->producer.mode==2) {
		const struct st_big_mode *mode=st_big_mode((unsigned)big->state.bgi.mode+1);
		if (mode==NULL) return IO_ERROR_OUT_OF_RANGE;
		big->producer.video_segment=st_big_video_active_segment((uint8_t)big->state.bgi.active_page,(uint16_t)(mode->page_bytes/16));
	}
	return st_big_draw_bar3d(&big->state,big->bar.points,request->size,request->top,&big->producer,&sink);
}
static int bar_geometry(struct cterm_big *big, bool depth, uint16_t size, uint16_t top)
{
	struct bar request={big,depth,size,top};
	int status=st_console_modify_pixels(big->terminal->screen.console,bar,&request);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
int cterm_big_bar_begin(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=5) return IO_ERROR_PROTOCOL;
	big->bar=(struct cterm_big_bar){.active=true,.chained=view->chained};
	for (size_t i=0;i<5;++i) {
		if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
		if (i<4) big->bar.points[i]=(uint16_t)view->arguments[i].value;
		else big->bar.kind=(uint16_t)view->arguments[i].value;
	}
	int status=st_big_draw_color(&big->state,big->state.application.colors[1]);
	if (status==0) status=st_big_fill_apply(&big->state);
	return status==0?IO_ERROR_IN_PROGRESS:status;
}
int cterm_big_bar_poll(struct cterm_big *big, const uint8_t *bytes, size_t size, size_t *used, bool *complete)
{
	*used=0; *complete=false;
	struct cterm_big_bar *b=&big->bar;
	const struct st_graphics_command_view view={.code='B',.count=5};
	const struct st_graphics_parameter scalar={.kind=ST_GRAPHICS_NUMBER};
	int status;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		switch (b->phase) {
		case 0:
			if (b->kind==0) { status=bar_geometry(big,false,0,0); if (status!=0) return status; }
			b->phase=1; break;
		case 1:
			if (b->kind==1) { status=bar_geometry(big,true,0,0); if (status!=0) return status; }
			b->phase=2; break;
		case 2:
			if (st_big_number_signed(b->kind)<=1) { b->active=false; *complete=true; return 0; }
			status=st_big_numeric_field(&big->numeric,&view,&scalar); if (status!=0) return status;
			b->phase=3; break;
		case 3: {
			bool done=false; int top=0;
			if (!b->wire) {
				status=st_big_numeric_number(&big->numeric,&view,&scalar,&top,&done);
				if (status!=0) return status;
				if (!done) b->wire=true;
			}
			if (b->wire) {
				status=st_big_numeric_read_number(&big->numeric,&view,&scalar,bytes,size,used,&top,&done);
				if (status!=0 || !done) return status;
			}
			if (big->reset_pending) return IO_ERROR_INTERRUPTED;
			status=bar_geometry(big,true,b->kind,(uint16_t)top); if (status!=0) return status;
			b->active=false; *complete=true; return 0;
		}
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
static int flood_address(const struct drawing *draw, uint16_t address, size_t *offset)
{
	*offset=(size_t)address*8;
	if (draw->big->reset_pending) return IO_ERROR_INTERRUPTED;
	size_t count=(size_t)draw->pixels->width*draw->pixels->height;
	return *offset>count || count-*offset<8?IO_ERROR_OUT_OF_RANGE:0;
}
static int flood_read(void *context, uint16_t address, uint8_t pens[8])
{
	const struct drawing *draw=context; size_t offset;
	int status=flood_address(draw,address,&offset); if (status!=0) return status;
	for (unsigned i=0;i<8;++i) {
		uint32_t pen=draw->pixels->primary[offset+i];
		if (pen<CTERM_BIG_PALETTE_FIRST || pen>=CTERM_BIG_PALETTE_FIRST+16) return IO_ERROR_OUT_OF_RANGE;
		pens[i]=(uint8_t)(pen-CTERM_BIG_PALETTE_FIRST);
	}
	return 0;
}
static int flood_write(void *context, uint16_t address, uint8_t mask, uint8_t planes, const uint8_t pens[8])
{
	struct drawing *draw=context; size_t offset;
	int status=flood_address(draw,address,&offset); if (status!=0) return status;
	for (unsigned i=0;i<8;++i) if (mask&(0x80u>>i)) {
		uint32_t a=pens[i], b=a;
		if (planes!=15) {
			a=draw->pixels->primary[offset+i]; b=draw->pixels->alternate[offset+i];
			if (a<CTERM_BIG_PALETTE_FIRST || a>=CTERM_BIG_PALETTE_FIRST+16
			    || b<CTERM_BIG_PALETTE_FIRST || b>=CTERM_BIG_PALETTE_FIRST+16) return IO_ERROR_OUT_OF_RANGE;
			a=((a-CTERM_BIG_PALETTE_FIRST)&(15u^planes))|(pens[i]&planes);
			b=((b-CTERM_BIG_PALETTE_FIRST)&(15u^planes))|(pens[i]&planes);
		}
		size_t index=offset+i;
		status=pixel(draw,(uint16_t)(index%draw->pixels->width),(uint16_t)(index/draw->pixels->width),(uint8_t)a,false);
		if (status!=0) return status;
		draw->pixels->alternate[index]=CTERM_BIG_PALETTE_FIRST+b;
	}
	return 0;
}
struct flood_request { struct cterm_big *big; uint16_t x,y,boundary; };
static int fill_region(struct drawing *drawing, uint16_t x, uint16_t y, uint16_t boundary)
{
	const struct st_big_flood_sink sink={drawing,flood_read,flood_write};
	return st_big_flood_fill(&drawing->big->flood_work,&drawing->big->state,&sink,x,y,boundary);
}
static int flood_draw(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct flood_request *request=context;
	struct drawing drawing={request->big,pixels,changed};
	return fill_region(&drawing,request->x,request->y,request->boundary);
}
int cterm_big_draw_flood(struct cterm_big *big, uint16_t x, uint16_t y, uint16_t boundary)
{
	struct flood_request request={big,x,y,boundary};
	int status=st_console_modify_pixels(big->terminal->screen.console,flood_draw,&request);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
struct rectangle { struct cterm_big *big; uint16_t points[4]; };
static int rectangle(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct rectangle *request=context; struct cterm_big *big=request->big;
	struct drawing drawing={big,pixels,changed};
	const struct st_big_draw_sink sink={&drawing,pixel};
	int status=st_big_draw_rectangle(&big->state,request->points,&sink);
	if (status!=0 || big->state.application.hollow!=0) return status;
	uint16_t boundary=big->state.application.colors[1];
	uint16_t y=(uint16_t)(request->points[1]+1u), x=(uint16_t)(request->points[0]+1u);
	return fill_region(&drawing,x,y,boundary);
}
int cterm_big_draw_z(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=4) return IO_ERROR_PROTOCOL;
	struct rectangle request={.big=big};
	for (size_t i=0;i<4;++i) {
		if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
		request.points[i]=(uint16_t)view->arguments[i].value;
	}
	int status=st_big_draw_color(&big->state,big->state.application.colors[1]);
	if (status==0) status=st_big_fill_apply(&big->state);
	if (status==0) status=st_console_modify_pixels(big->terminal->screen.console,rectangle,&request);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
int cterm_big_flood_begin(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=3) return IO_ERROR_PROTOCOL;
	uint16_t words[3];
	for (size_t i=0;i<3;++i) {
		if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
		words[i]=(uint16_t)view->arguments[i].value;
		if (st_big_number_signed(words[i])<0) words[i]=0;
	}
	if (words[2]>3) words[2]=3;
	big->flood=(struct cterm_big_flood){.x=words[0],.y=words[1],.selector=words[2],.active=true,.chained=view->chained};
	int status=st_big_fill_apply(&big->state);
	return status==0?IO_ERROR_IN_PROGRESS:status;
}
int cterm_big_flood_poll(struct cterm_big *big, size_t *budget, bool *complete)
{
	struct cterm_big_flood *f=&big->flood; *complete=false;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		int status;
		switch (f->phase) {
		case 0:
			if (big->numeric.loop.active==0) { st_big_flow_begin(&f->flow,&big->control,false); f->phase=1; }
			else f->phase=2;
			break;
		case 1:
			status=st_big_flow_poll(&f->flow,&big->control,&big->control_io,budget);
			if (status!=0) return status;
			f->phase=2; break;
		case 2:
			if (f->selector>=4) return IO_ERROR_OUT_OF_RANGE;
			status=cterm_big_draw_flood(big,f->x,f->y,big->state.application.colors[f->selector]);
			if (status!=0) return status;
			f->phase=3; break;
		case 3:
			if (big->numeric.loop.active==0) { st_big_flow_begin(&f->flow,&big->control,true); f->phase=4; }
			else { f->active=false; *complete=true; return 0; }
			break;
		case 4:
			status=st_big_flow_poll(&f->flow,&big->control,&big->control_io,budget);
			if (status!=0) return status;
			f->active=false; *complete=true; return 0;
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
