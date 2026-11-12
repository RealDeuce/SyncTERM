/* BIG line and polymarker schedules, references12/22/51/57/58/64/83. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_draw.h"
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
	s->bgi.line_mask=style==4?pattern:st_bgi_line_patterns[style]; s->bgi.device_line_width=width;
	return 0;
}
int st_big_draw_color(struct st_big_state *s, uint16_t color)
{
	s->bgi.color=(uint8_t)color;
	int status=installed(s); if (status!=0) return status;
	s->bgi.device_color=(uint8_t)(color&15); return 0;
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
static int thin(struct st_big_state *s, const uint16_t *requested, const struct st_big_draw_sink *sink)
{
	uint16_t p[4], b[4]; memcpy(p,requested,sizeof(p)); bounds(s,b); bool visible;
	int status=st_big_draw_clip(b,p,&visible); if (status!=0 || !visible) return status;
	/* The shared host-int kernel is used only inside actual pixel geometry. */
	for (size_t i=0;i<4;++i) if (p[i]>=(i%2==0?s->bgi.width:s->bgi.height)) return IO_ERROR_OUT_OF_RANGE;
	uint16_t pattern=s->bgi.line_mask; int phase=0;
	struct stroke stroke={sink,s->bgi.device_color,s->bgi.write_mode!=0,0};
	st_bgi_thin_line(p[0],p[1],p[2],p[3],&pattern,&phase,pixel,&stroke);
	return stroke.status;
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
int st_big_draw_line(struct st_big_state *s, const uint16_t p[4], const struct st_big_draw_sink *sink)
{
	int status=installed(s); if (status!=0) return status;
	uint16_t q[4];
	for (size_t i=0;i<4;++i) q[i]=(uint16_t)(p[i]+s->bgi.viewport[i%2]);
	return strokes(s,q,sink);
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
