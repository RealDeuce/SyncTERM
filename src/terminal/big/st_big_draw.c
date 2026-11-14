/* BIG drawing/producer adapters, references12/13/22/23/51/57/58/59/64/83. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_draw.h"
#include "st_big_producer.h"
#include "st_big_bar3d.h"
#include "st_big_number.h"
#include "st_bgi_line.h"
#include "st_bgi_data.h"
#include "io_error.h"
#include <string.h>

static int32_t signed_word(uint16_t n) { return st_big_number_signed(n); }
static bool inside(const uint16_t *b, uint16_t x, uint16_t y)
{
	return x<=b[2] && y<=b[3] && signed_word(x)>=signed_word(b[0]) && signed_word(y)>=signed_word(b[1]);
}
static unsigned outcode(const uint16_t *b, const uint16_t *p)
{
	unsigned code=0;
	if (signed_word(p[0])<signed_word(b[0])) code=1;
	if (signed_word(p[0])>signed_word(b[2])) code=2;
	if (signed_word(p[1])<signed_word(b[1])) code+=4;
	if (signed_word(p[1])>signed_word(b[3])) code+=8;
	return code;
}
static void swap_ends(uint16_t *p)
{
	uint16_t x=p[0], y=p[1]; p[0]=p[2]; p[1]=p[3]; p[2]=x; p[3]=y;
}
/* One pure clip iteration also permits constant-storage cycle detection.
 * The initial slope remains fixed even across rounded intersections/swaps. */
static int clip_step(const uint16_t *b, uint16_t *p, int32_t dx, int32_t dy, bool *done, bool *visible)
{
	unsigned first=outcode(b,p), second=outcode(b,p+2);
	*done=true; *visible=false;
	if ((first|second)==0) { *visible=true; return 0; }
	if (first&second) return 0;
	bool swapped=first==0;
	if (swapped) swap_ends(p);
	if (dx==0) {
		if (signed_word(p[1])<signed_word(b[1])) p[1]=b[1];
		if (signed_word(p[1])>signed_word(b[3])) p[1]=b[3];
	} else if (dy==0) {
		if (signed_word(p[0])<signed_word(b[0])) p[0]=b[0];
		if (signed_word(p[0])>signed_word(b[2])) p[0]=b[2];
	} else {
		unsigned axis; uint16_t edge;
		if (signed_word(p[0])<signed_word(b[0])) { axis=0; edge=b[0]; }
		else if (signed_word(p[0])>signed_word(b[2])) { axis=0; edge=b[2]; }
		else if (signed_word(p[1])<signed_word(b[1])) { axis=1; edge=b[1]; }
		else { axis=1; edge=b[3]; }
		int32_t distance=signed_word((uint16_t)(edge-p[axis]));
		int32_t numerator=distance*(axis==0?dy:dx), divisor=axis==0?dx:dy;
		/* Signed16x16 products fit int32. Check the original IDIV
		 * quotient before narrowing it to a WORD. */
		int64_t quotient=(int64_t)numerator/divisor;
		if (quotient<-32768 || quotient>32767) return IO_ERROR_OUT_OF_RANGE;
		p[1-axis]=(uint16_t)(p[1-axis]+quotient); p[axis]=edge;
	}
	if (swapped) swap_ends(p);
	*done=false; return 0;
}
int st_big_draw_clip(const uint16_t b[4], uint16_t p[4], bool *visible)
{
	if (b==NULL || p==NULL || visible==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*visible=false;
	if (inside(b,p[0],p[1]) && inside(b,p[2],p[3])) { *visible=true; return 0; }
	unsigned a=outcode(b,p), z=outcode(b,p+2);
	if ((a|z)==0) { *visible=true; return 0; }
	int32_t dx=signed_word(p[2])-signed_word(p[0]), dy=signed_word(p[3])-signed_word(p[1]);
	if (dx<-32768 || dx>32767 || dy<-32768 || dy>32767) goto rejected;
	uint16_t fast[4]; memcpy(fast,p,sizeof(fast)); bool tracking=true;
	for (;;) {
		bool done; int status=clip_step(b,p,dx,dy,&done,visible);
		if (status!=0) return status;
		if (done) { if (!*visible) goto rejected; return 0; }
		if (tracking) {
			for (unsigned i=0;i<2 && tracking;++i) {
				bool end, accepted;
				status=clip_step(b,fast,dx,dy,&end,&accepted);
				if (status!=0 || end) tracking=false;
			}
			if (tracking && memcmp(p,fast,sizeof(fast))==0) return IO_ERROR_OUT_OF_RANGE;
		}
	}
rejected:
	for (size_t i=0;i<4;++i) p[i]=UINT16_MAX;
	return 0;
}
int st_big_draw_bar_clip(const uint16_t b[4], uint16_t p[4], bool *visible)
{
	if (b==NULL || p==NULL || visible==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*visible=false;
	for (size_t axis=0;axis<2;++axis) {
		if (signed_word(p[axis])>=signed_word(p[axis+2])) {
			uint16_t temp=p[axis]; p[axis]=p[axis+2]; p[axis+2]=temp;
		}
	}
	/* DDO1309 rejects before clamping. Keep the paired tests, including
	 * their behavior with inverted bounds; line clipping is different. */
	for (size_t axis=0;axis<2;++axis) {
		if (signed_word(p[axis])<signed_word(b[axis]) && signed_word(p[axis+2])<signed_word(b[axis])) return 0;
		if (signed_word(p[axis])>signed_word(b[axis+2]) && signed_word(p[axis+2])>signed_word(b[axis+2])) return 0;
	}
	for (size_t i=0;i<4;++i) {
		if (signed_word(p[i])<signed_word(b[i%2])) p[i]=b[i%2];
		else if (signed_word(p[i])>signed_word(b[i%2+2])) p[i]=b[i%2+2];
	}
	*visible=true; return 0;
}
static int installed(struct st_big_state *s)
{
	if (s->bgi.state!=0) return 0;
	s->bgi.error=-1; return IO_ERROR_PROTOCOL;
}
int st_big_draw_style(struct st_big_state *s, uint16_t style, uint16_t pattern, uint16_t width)
{
	if (style>4 || width>3) { s->bgi.error=-11; return 0; }
	s->bgi.line_style=style; s->bgi.line_user=pattern; s->bgi.line_width=width;
	int status=installed(s); if (status!=0) return status;
	s->bgi.ddo_line_width=(uint8_t)width;
	s->bgi.ddo_line_style=(uint8_t)style; s->bgi.ddo_line_user=pattern;
	s->bgi.line_mask=style==4?pattern:st_bgi_line_patterns[style]; s->bgi.device_line_width=width;
	return 0;
}
int st_big_draw_color(struct st_big_state *s, uint16_t color)
{
	s->bgi.color=(uint8_t)color;
	int status=installed(s); if (status!=0) return status;
	s->bgi.device_color=(uint8_t)(color&15);
	s->bgi.device_fill_color=(uint8_t)(s->bgi.fill_low&15);
	return 0;
}
int st_big_draw_move(struct st_big_state *s, uint16_t x, uint16_t y)
{
	int status=installed(s); if (status!=0) return status;
	s->bgi.ddo_x=(uint16_t)(x+s->bgi.viewport[0]); s->bgi.ddo_y=(uint16_t)(y+s->bgi.viewport[1]);
	s->bgi.device_x=s->bgi.ddo_x; s->bgi.device_y=s->bgi.ddo_y;
	s->bgi.x=x; s->bgi.y=y; return 0;
}
static void bounds(const struct st_big_state *s, uint16_t *b)
{
	if (s->bgi.clip!=0) memcpy(b,s->bgi.viewport,4*sizeof(*b));
	else { b[0]=b[1]=0; b[2]=(uint16_t)(s->bgi.width-1); b[3]=(uint16_t)(s->bgi.height-1); }
}
static int output_pixel(const struct st_big_draw_sink *sink, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	int status=sink->pixel(sink->context,x,y,pen,xor_mode);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
int st_big_producer_bind(struct st_big_producer *p, struct st_big_producer_point *points, size_t storage_count, uint16_t bytes)
{
	if (p==NULL) return IO_ERROR_INVALID_ARGUMENT;
	p->points=points; p->storage_count=storage_count; p->capacity=(uint16_t)(bytes/6);
	return 0;
}
int st_big_producer_start(struct st_big_producer *p)
{
	if (p==NULL) return IO_ERROR_INVALID_ARGUMENT;
	p->mode=1; p->count=p->run_length=0;
	return 0;
}
static int producer_append(struct st_big_producer *p, struct st_big_state *s, const struct st_big_producer_registers *r)
{
	/* The original increments the WORD run length even on overflow. */
	p->run_length=(uint16_t)(p->run_length+1u);
	if (p->count>=p->capacity) { s->bgi.error=-6; return 0; }
	if (p->points==NULL || p->count>=p->storage_count) return IO_ERROR_OUT_OF_RANGE;
	p->points[p->count].x=r->ax;
	p->points[p->count].y=r->bx;
	p->count=(uint16_t)(p->count+1u);
	return 0;
}
int st_big_producer_emit(struct st_big_producer *p, struct st_big_state *s,
    struct st_big_producer_registers *r, const struct st_big_producer_sink *sink, bool *suppressed)
{
	if (p==NULL || s==NULL || r==NULL || suppressed==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*suppressed=false;
	int status=installed(s); if (status!=0) return status;
	if (p->mode==0) return 0;
	if (p->mode!=2) {
		*suppressed=true;
		if (p->run_length==0) { p->first_x=r->ax; p->first_y=r->bx; }
		else if (r->ax==p->first_x && r->bx==p->first_y) {
			if (p->run_length==1) return 0;
			status=producer_append(p,s,r); if (status!=0) return status;
			r->ax=r->bx=0x8001;
			status=producer_append(p,s,r); if (status!=0) return status;
			p->run_length=0; return 0;
		}
		return producer_append(p,s,r);
	}
	/* Producer2 loads DL even when point clipping rejects the request.
	 * Both rejected and returning builtin point paths leave carry clear. */
	uint8_t color=(uint8_t)s->bgi.color;
	r->dx=(uint16_t)((r->dx&0xff00u)|color);
	uint16_t b[4]; bounds(s,b);
	if (!inside(b,r->ax,r->bx)) return 0;
	uint16_t x=r->ax, y=r->bx;
	if (x>=s->bgi.width || y>=s->bgi.height) return IO_ERROR_OUT_OF_RANGE;
	if (sink==NULL || sink->drawing.pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	/* The only retained hardware values here are the point's observable
	 * returned WORDs. Pixel storage remains indexed; a batched pre-read
	 * byte is supplied at that boundary, not kept in a planar shadow. */
	bool batch=p->batch;
	r->ax=p->video_segment;
	r->bx=(uint16_t)(x&7);
	r->cx=(uint16_t)(((0x80u>>(x&7))<<8)|color);
	if (batch) {
		if (p->read_plane>3) return IO_ERROR_OUT_OF_RANGE;
		if (sink->read_byte==NULL) return IO_ERROR_INVALID_ARGUMENT;
		uint8_t byte;
		status=sink->read_byte(sink->read_context,(uint16_t)(x&0xfff8u),y,p->read_plane,&byte);
		if (status!=0) return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
		r->ax=(uint16_t)((r->ax&0xff00u)|byte);
	}
	status=output_pixel(&sink->drawing,x,y,(uint8_t)(color&15),false);
	if (status!=0) return status;
	if (!batch) r->ax=0x0f02;
	return 0;
}
/* A native indexed write has no planar latches or observable zero-mask
 * exchange. Expand each original byte pass left-to-right; failures retain
 * the accepted pixel prefix, including cleared middle spans. */
static int bar_byte(const struct st_big_draw_sink *sink, uint16_t x, uint16_t y, uint8_t mask, uint8_t pen)
{
	for (unsigned bit=0;bit<8;++bit) if ((mask&(0x80u>>bit))!=0) {
		int status=output_pixel(sink,(uint16_t)(x+bit),y,pen,false);
		if (status!=0) return status;
	}
	return 0;
}
static int bar_edge(const struct st_big_draw_sink *sink, uint16_t x, uint16_t y, uint8_t edge, uint8_t row, uint8_t pen)
{
	int status=bar_byte(sink,x,y,(uint8_t)(edge&row),pen);
	return status!=0?status:bar_byte(sink,x,y,(uint8_t)(edge&~row),0);
}
static int bar_translated(struct st_big_state *s, const uint16_t translated[4], const struct st_big_draw_sink *sink)
{
	int status=installed(s); if (status!=0) return status;
	uint16_t p[4], b[4]; memcpy(p,translated,sizeof(p));
	bounds(s,b); bool visible;
	status=st_big_draw_bar_clip(b,p,&visible); if (status!=0 || !visible) return status;
	/* The raw bar sorts UNSIGNED, after the signed DDO clip. */
	for (size_t axis=0;axis<2;++axis) if (p[axis]>p[axis+2]) {
		uint16_t temp=p[axis]; p[axis]=p[axis+2]; p[axis+2]=temp;
	}
	for (size_t i=0;i<4;++i) if (p[i]>=(i%2==0?s->bgi.width:s->bgi.height)) return IO_ERROR_OUT_OF_RANGE;
	uint8_t pen=s->bgi.device_fill_color;
	if (pen>15) return IO_ERROR_OUT_OF_RANGE;
	unsigned first=p[0]/8, last=p[2]/8;
	uint8_t left=(uint8_t)(0xffu>>(p[0]&7)), right=(uint8_t)(0xffu<<(7-(p[2]&7)));
	for (uint32_t y=p[1];y<=p[3];++y) {
		/* EGAVGA latches the pen once per bar, the mask once per row. */
		uint8_t row=s->bgi.fill_pattern[y&7];
		status=bar_edge(sink,(uint16_t)(first*8),(uint16_t)y,first==last?(uint8_t)(left&right):left,row,pen);
		if (status!=0) return status;
		if (first==last) continue;
		/* Nonsolid middles clear the WHOLE span before any foreground
		 * exchanges. A row-major final-colour loop loses that prefix. */
		for (unsigned byte=first+1;byte<last;++byte) {
			status=bar_byte(sink,(uint16_t)(byte*8),(uint16_t)y,255,row==255?pen:0);
			if (status!=0) return status;
		}
		if (row!=255) for (unsigned byte=first+1;byte<last;++byte) {
			status=bar_byte(sink,(uint16_t)(byte*8),(uint16_t)y,row,pen);
			if (status!=0) return status;
		}
		status=bar_edge(sink,(uint16_t)(last*8),(uint16_t)y,right,row,pen);
		if (status!=0) return status;
	}
	return 0;
}
int st_big_draw_bar(struct st_big_state *s, const uint16_t requested[4], const struct st_big_draw_sink *sink)
{
	if (s==NULL || requested==NULL || sink==NULL || sink->pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t p[4];
	for (size_t i=0;i<4;++i) p[i]=(uint16_t)(requested[i]+s->bgi.viewport[i%2]);
	return bar_translated(s,p,sink);
}
int st_big_draw_point(struct st_big_state *s, uint16_t x, uint16_t y, uint16_t color, const struct st_big_draw_sink *sink)
{
	int status=installed(s); if (status!=0) return status;
	x=(uint16_t)(x+s->bgi.viewport[0]); y=(uint16_t)(y+s->bgi.viewport[1]);
	uint16_t b[4]; bounds(s,b);
	return inside(b,x,y)?output_pixel(sink,x,y,(uint8_t)(color&15),false):0;
}
struct stroke { const struct st_big_draw_sink *sink; uint8_t pen; bool xor_mode; int status; };
static void pixel(void *context, int x, int y)
{
	struct stroke *stroke=context;
	if (stroke->status==0) stroke->status=output_pixel(stroke->sink,(uint16_t)x,(uint16_t)y,stroke->pen,stroke->xor_mode);
}
static int raw_line(struct st_big_state *s, const uint16_t *p, const struct st_big_draw_sink *sink)
{
	/* The shared host-int kernel is used only inside actual pixel geometry. */
	for (size_t i=0;i<4;++i) if (p[i]>=(i%2==0?s->bgi.width:s->bgi.height)) return IO_ERROR_OUT_OF_RANGE;
	uint16_t pattern=s->bgi.line_mask; int phase=0;
	struct stroke stroke={sink,s->bgi.device_color,s->bgi.write_mode!=0,0};
	st_bgi_thin_line(p[0],p[1],p[2],p[3],&pattern,&phase,pixel,&stroke);
	return stroke.status;
}
static int thin(struct st_big_state *s, const uint16_t *requested, const struct st_big_draw_sink *sink)
{
	uint16_t p[4], b[4]; memcpy(p,requested,sizeof(p)); bounds(s,b); bool visible;
	int status=st_big_draw_clip(b,p,&visible); if (status!=0 || !visible) return status;
	return raw_line(s,p,sink);
}
int st_big_draw_polygon_span(struct st_big_state *s, bool raw,
    const uint16_t p[4], const struct st_big_draw_sink *sink)
{
	if (s==NULL || p==NULL || sink==NULL || sink->pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status=installed(s); if (status!=0) return status;
	return raw?raw_line(s,p,sink):bar_translated(s,p,sink);
}
static uint16_t word_abs_difference(uint16_t a, uint16_t b)
{
	uint16_t d=(uint16_t)(a-b); return signed_word(d)<0?(uint16_t)(0u-d):d;
}
static int strokes(struct st_big_state *s, const uint16_t *p, const struct st_big_draw_sink *sink)
{
	bool thick=s->bgi.ddo_line_width>=3;
	int status=thin(s,p,sink); if (status!=0 || !thick) return status;
	unsigned axis=word_abs_difference(p[0],p[2])>=word_abs_difference(p[1],p[3])?1:0;
	uint16_t offset=s->bgi.line_distance, side[4]; memcpy(side,p,sizeof(side));
	side[axis]=(uint16_t)(side[axis]-offset); side[axis+2]=(uint16_t)(side[axis+2]-offset);
	status=thin(s,side,sink); if (status!=0) return status;
	offset=(uint16_t)(offset*2u);
	side[axis]=(uint16_t)(side[axis]+offset); side[axis+2]=(uint16_t)(side[axis+2]+offset);
	return thin(s,side,sink);
}
int st_big_producer_stroke(struct st_big_state *s, struct st_big_producer *producer,
    struct st_big_producer_registers *r, const struct st_big_producer_sink *sink)
{
	if (s==NULL || producer==NULL || r==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	bool suppressed; int status=st_big_producer_emit(producer,s,r,sink,&suppressed);
	if (status!=0 || suppressed) return status;
	if (sink->drawing.pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	/*0450 consumes the producer's actual returned registers, without the
	 * DDO/device Move stores used by0378 or the endpoint stores of03b4. */
	const uint16_t points[]={r->ax,r->bx,r->cx,r->dx};
	return strokes(s,points,&sink->drawing);
}
/* The bar3d caller observes CX/DX from the producer before the stroke's
 * saved-register bracket. Its later AX/BX uses are explicitly reloaded
 * or saved below, so no unused line-return register emulation is needed. */
static int producer_line(struct st_big_state *s, struct st_big_producer *producer,
    struct st_big_producer_registers *r, const struct st_big_producer_sink *sink)
{
	bool suppressed;
	int status=st_big_producer_emit(producer,s,r,sink,&suppressed);
	if (status!=0 || suppressed) return status;
	if (sink==NULL || sink->drawing.pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t p[]={s->bgi.ddo_x,s->bgi.ddo_y,r->ax,r->bx};
	s->bgi.ddo_x=r->ax; s->bgi.ddo_y=r->bx;
	return strokes(s,p,&sink->drawing);
}
int st_big_producer_move(struct st_big_state *s, struct st_big_producer *producer,
    struct st_big_producer_registers *r, const struct st_big_producer_sink *sink)
{
	bool suppressed;
	int status=st_big_producer_emit(producer,s,r,sink,&suppressed);
	if (status!=0 || suppressed) return status;
	s->bgi.ddo_x=s->bgi.device_x=r->ax; s->bgi.ddo_y=s->bgi.device_y=r->bx;
	return 0;
}
static int bar3d_top(struct st_big_state *s, struct st_big_producer *producer,
    struct st_big_producer_registers *r, const struct st_big_producer_sink *sink)
{
	r->ax=(uint16_t)(s->bgi.bar_corners[0]+r->cx);
	int status=producer_line(s,producer,r,sink); if (status!=0) return status;
	r->ax=s->bgi.bar_corners[0]; r->bx=s->bgi.bar_corners[3]; uint16_t top=r->bx;
	status=producer_line(s,producer,r,sink); if (status!=0) return status;
	r->ax=s->bgi.bar_corners[2]; r->bx=top; uint16_t right=r->ax;
	status=st_big_producer_move(s,producer,r,sink); if (status!=0) return status;
	r->ax=(uint16_t)(right+r->cx); r->bx=(uint16_t)(top-r->dx);
	return producer_line(s,producer,r,sink);
}
int st_big_draw_bar3d(struct st_big_state *s, const uint16_t p[4], uint16_t depth,
    uint16_t top, struct st_big_producer *producer, const struct st_big_producer_sink *sink)
{
	if (s==NULL || p==NULL || producer==NULL || sink==NULL || sink->drawing.pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status=installed(s); if (status!=0) return status;
	uint16_t left=p[0]<p[2]?p[0]:p[2], right=p[0]<p[2]?p[2]:p[0];
	uint16_t bottom=p[1]>=p[3]?p[1]:p[3], top_y=p[1]>=p[3]?p[3]:p[1];
	/* Public selector08 bypasses the producer and leaves library CP alone. */
	s->bgi.ddo_x=s->bgi.device_x=(uint16_t)(left+s->bgi.viewport[0]);
	s->bgi.ddo_y=s->bgi.device_y=(uint16_t)(bottom+s->bgi.viewport[1]);
	s->bgi.bar_corners[2]=(uint16_t)(right+s->bgi.viewport[0]);
	s->bgi.bar_corners[3]=(uint16_t)(top_y+s->bgi.viewport[1]);
	s->bgi.bar_corners[0]=s->bgi.ddo_x; s->bgi.bar_corners[1]=s->bgi.ddo_y;
	left=s->bgi.bar_corners[0]; bottom=s->bgi.bar_corners[1];
	right=s->bgi.bar_corners[2]; top_y=s->bgi.bar_corners[3];
	if (signed_word(left)>=signed_word(right)) { uint16_t temp=left; left=right; right=temp; }
	if (signed_word(bottom)<signed_word(top_y)) { uint16_t temp=bottom; bottom=top_y; top_y=temp; }
	uint16_t inset[]={(uint16_t)(left+1u),(uint16_t)(bottom-1u),(uint16_t)(right-1u),(uint16_t)(top_y+1u)};
	status=bar_translated(s,inset,&sink->drawing); if (status!=0) return status;
	/* Front calls use current saved corners, not the independently sorted
	 * inset. Each saved BX/AX below mirrors a value the caller reuses. */
	struct st_big_producer_registers r={.ax=s->bgi.bar_corners[0],.bx=s->bgi.bar_corners[3]};
	top_y=r.bx;
	status=producer_line(s,producer,&r,sink); if (status!=0) return status;
	r.ax=s->bgi.bar_corners[2]; r.bx=top_y; right=r.ax;
	status=producer_line(s,producer,&r,sink); if (status!=0) return status;
	r.ax=right; r.bx=s->bgi.bar_corners[1]; bottom=r.bx;
	status=producer_line(s,producer,&r,sink); if (status!=0) return status;
	r.ax=s->bgi.bar_corners[0]; r.bx=bottom;
	status=producer_line(s,producer,&r,sink); if (status!=0) return status;
	r.ax=s->bgi.bar_corners[2]; r.bx=s->bgi.bar_corners[1]; right=r.ax;
	status=st_big_producer_move(s,producer,&r,sink); if (status!=0) return status;
	r.ax=right;
	if (depth!=0) {
		r.cx=depth; r.dx=(uint16_t)((uint16_t)(depth*3u)>>2);
		if (s->bgi.orientation!=0) r.cx=(uint16_t)(0u-r.dx);
		if (s->bgi.orientation!=0 && (uint8_t)top==0) {
			r.ax=s->bgi.bar_corners[0]; r.bx=s->bgi.bar_corners[3]; left=r.ax; top_y=r.bx;
			status=st_big_producer_move(s,producer,&r,sink); if (status!=0) return status;
			r.ax=(uint16_t)(left+r.cx); r.bx=(uint16_t)(top_y-r.dx); top_y=r.bx;
			status=producer_line(s,producer,&r,sink); if (status!=0) return status;
			r.ax=(uint16_t)(s->bgi.bar_corners[2]+r.cx); r.bx=top_y;
			status=producer_line(s,producer,&r,sink); if (status!=0) return status;
		} else {
			r.ax=(uint16_t)(r.ax+r.cx); r.bx=(uint16_t)(r.bx-r.dx); right=r.ax;
			status=producer_line(s,producer,&r,sink); if (status!=0) return status;
			r.ax=right; r.bx=(uint16_t)(s->bgi.bar_corners[3]-r.dx); top_y=r.bx;
			status=producer_line(s,producer,&r,sink); if (status!=0) return status;
			r.bx=top_y;
			if ((uint8_t)top!=0) { status=bar3d_top(s,producer,&r,sink); if (status!=0) return status; }
		}
	}
	return st_big_draw_color(s,s->bgi.color);
}
int st_big_draw_physical_line(struct st_big_state *s, const uint16_t p[4], const struct st_big_draw_sink *sink)
{
	if (s==NULL || p==NULL || sink==NULL || sink->pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status=installed(s); return status!=0?status:thin(s,p,sink);
}
int st_big_draw_line(struct st_big_state *s, const uint16_t p[4], const struct st_big_draw_sink *sink)
{
	int status=installed(s); if (status!=0) return status;
	uint16_t q[4];
	for (size_t i=0;i<4;++i) q[i]=(uint16_t)(p[i]+s->bgi.viewport[i%2]);
	return strokes(s,q,sink);
}
int st_big_draw_rectangle(struct st_big_state *s, const uint16_t p[4], const struct st_big_draw_sink *sink)
{
	if (s==NULL || p==NULL || sink==NULL || sink->pixel==NULL) return IO_ERROR_INVALID_ARGUMENT;
	const uint16_t x=p[0],y=p[1];
	uint16_t edge[]={x,y,p[2],y};
	int status=st_big_draw_line(s,edge,sink); if (status!=0) return status;
	edge[0]=p[2]; edge[1]=y; edge[2]=p[2]; edge[3]=p[3];
	status=st_big_draw_line(s,edge,sink); if (status!=0) return status;
	edge[0]=p[2]; edge[1]=p[3]; edge[2]=x; edge[3]=p[3];
	status=st_big_draw_line(s,edge,sink); if (status!=0) return status;
	edge[0]=x; edge[1]=p[3]; edge[2]=x; edge[3]=y;
	return st_big_draw_line(s,edge,sink);
}
static int lower_to(struct st_big_state *s, uint16_t x, uint16_t y, const struct st_big_draw_sink *sink)
{
	int status=installed(s); if (status!=0) return status;
	uint16_t p[]={0,0,s->bgi.ddo_x,s->bgi.ddo_y};
	s->bgi.ddo_x=p[0]=(uint16_t)(x+s->bgi.viewport[0]); s->bgi.ddo_y=p[1]=(uint16_t)(y+s->bgi.viewport[1]);
	status=strokes(s,p,sink); if (status!=0) return status;
	s->bgi.device_x=p[0]; s->bgi.device_y=p[1]; return 0;
}
int st_big_draw_to(struct st_big_state *s, uint16_t x, uint16_t y, const struct st_big_draw_sink *sink)
{
	int status=lower_to(s,x,y,sink); return status!=0?status:st_big_draw_move(s,x,y);
}
int st_big_draw_relative(struct st_big_state *s, uint16_t dx, uint16_t dy, const struct st_big_draw_sink *sink)
{
	uint16_t y=(uint16_t)(s->bgi.y+dy), x=(uint16_t)(s->bgi.x+dx);
	int status=lower_to(s,x,y,sink); if (status!=0) return status;
	y=(uint16_t)(s->bgi.y+dy); x=(uint16_t)(s->bgi.x+dx);
	return st_big_draw_move(s,x,y);
}
static int relative(struct st_big_state *s, int dx, int dy, const struct st_big_draw_sink *sink, bool draw)
{
	/* Separate live size reads, Y then X. Zero components have no load. */
	uint16_t y=(uint16_t)(dy==0?0:dy*s->application.marker_size);
	uint16_t x=(uint16_t)(dx==0?0:dx*s->application.marker_size);
	if (draw) return st_big_draw_relative(s,x,y,sink);
	y=(uint16_t)(s->bgi.y+y); x=(uint16_t)(s->bgi.x+x);
	return st_big_draw_move(s,x,y);
}
int st_big_draw_marker(struct st_big_state *s, uint16_t x, uint16_t y, const struct st_big_draw_sink *sink)
{
	static const int rays1[][2]={{4,4},{-4,4},{0,8}};
	static const int rays2[][2]={{0,-3},{0,3},{-4,0},{4,0}};
	static const int rays3[][2]={{0,-4},{0,4},{-4,0},{4,0},{3,-3},{3,3},{-3,-3},{-3,3}};
	static const int rays5[][2]={{4,-4},{4,4},{-4,-4},{-4,4}};
	static const int square[][2]={{8,0},{0,8},{-8,0},{0,-8}};
	static const int diamond[][2]={{4,4},{-4,4},{-4,-4},{4,-4}};
	int status=st_big_draw_color(s,s->application.colors[0]); if (status!=0) return status;
	for (unsigned type=1;type<=7;++type) {
		if (s->application.marker!=type) continue;
		if (type==4 || type==6 || type==7) {
			status=st_big_draw_move(s,x,y); if (status!=0) return status;
			status=relative(s,type==6?0:-4,-4,sink,false); if (status!=0) return status;
			const int (*edges)[2]=type==6?diamond:square;
			for (size_t i=0;i<4;++i) { status=relative(s,edges[i][0],edges[i][1],sink,true); if (status!=0) return status; }
			status=st_big_draw_move(s,x,y); if (status!=0) return status;
			if (type!=7) continue;
		}
		const int (*rays)[2]=type==1?rays1:type==2?rays2:type==3?rays3:rays5;
		size_t count=type==1?3:type==3?8:4;
		for (size_t i=0;i<count;++i) {
			/* Type7 already moved to the anchor after its square. */
			if (type!=7 || i!=0) { status=st_big_draw_move(s,x,y); if (status!=0) return status; }
			status=relative(s,rays[i][0],rays[i][1],sink,true); if (status!=0) return status;
		}
		if (type!=2) { status=st_big_draw_move(s,x,y); if (status!=0) return status; }
	}
	return 0;
}

struct producer_reader {
	const struct st_big_producer_point *points;
	size_t extent, index;
	uint16_t remaining;
};
static int producer_next(struct producer_reader *reader, struct st_big_producer_registers *r, uint16_t *last_y)
{
	r->bx=0x8000;
	if (reader->remaining!=0) {
		if (reader->points==NULL || reader->index>=reader->extent) return IO_ERROR_OUT_OF_RANGE;
		r->ax=reader->points[reader->index].x;
		r->bx=reader->points[reader->index++].y;
		--reader->remaining;
	}
	*last_y=r->bx; return 0;
}
int st_big_producer_flush(struct st_big_state *s, struct st_big_producer *producer,
    const struct st_big_producer_sink *sink, uint16_t *last_y)
{
	if (s==NULL || producer==NULL || sink==NULL || last_y==NULL) return IO_ERROR_INVALID_ARGUMENT;
	producer->mode=0;
	struct producer_reader reader={producer->points,producer->storage_count,0,producer->count};
	if (reader.remaining>=producer->capacity) { s->bgi.error=-6; return 0; }
	if (reader.remaining<2) return 0;
	uint16_t saved_x=s->bgi.ddo_x,saved_y=s->bgi.ddo_y;
	struct st_big_producer_registers r={0};
	int status=producer_next(&reader,&r,last_y);
	if (status==0) status=st_big_producer_move(s,producer,&r,sink);
	if (status!=0) return status;
	/* The initial record is always a Move. Only subsequent Y words are
	 * markers; any80xx with a nonzero low byte reads/moves the next record. */
	for (;;) {
		status=producer_next(&reader,&r,last_y); if (status!=0) return status;
		if ((r.bx&0xff00u)==0x8000u) {
			if ((r.bx&255u)==0) break;
			status=producer_next(&reader,&r,last_y);
			if (status==0) status=st_big_producer_move(s,producer,&r,sink);
		} else status=producer_line(s,producer,&r,sink);
		if (status!=0) return status;
		if (reader.remaining==0) break;
	}
	s->bgi.ddo_x=saved_x; s->bgi.ddo_y=saved_y;
	return 0;
}
