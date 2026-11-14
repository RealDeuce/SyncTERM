/* Independent physical polygon span oracles, BIG12/57/59/60. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_draw.h"
#include "st_big_fill.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct visit { uint16_t x,y; uint8_t pen; bool xor_mode; };
struct fixture { struct st_big_state state; uint8_t pixels[64*64]; struct visit trace[8192]; size_t writes,fail_at; int error; };
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); memset(f->pixels,3,sizeof(f->pixels)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64;
	f->state.bgi.viewport[0]=10; f->state.bgi.viewport[1]=12; f->state.bgi.viewport[2]=20; f->state.bgi.viewport[3]=22;
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=31; f->state.bgi.ddo_y=32; f->state.bgi.device_x=41; f->state.bgi.device_y=42;
	CHECK(st_big_fill_builtin(&f->state,1,5)==0 && st_big_draw_color(&f->state,9)==0);
	f->state.bgi.error=-11; f->fail_at=SIZE_MAX; f->error=IO_ERROR_IO;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<=15 && f->writes<8192);
	if (f->writes==f->fail_at) return f->error;
	f->trace[f->writes++]=(struct visit){x,y,pen,xor_mode};
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	return 0;
}
static int span(struct fixture *f, bool raw, const uint16_t p[4])
{
	const struct st_big_draw_sink sink={f,pixel}; return st_big_draw_polygon_span(&f->state,raw,p,&sink);
}
static void physical_bar(void)
{
	struct fixture f; init(&f); f.state.bgi.write_mode=1; CHECK(st_big_draw_style(&f.state,4,0,3)==0);
	const struct st_big_state before=f.state; const uint16_t p[]={8,14,13,14};
	CHECK(span(&f,false,p)==0 && f.writes==4 && memcmp(&before,&f.state,sizeof(before))==0);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(y==14 && x>=10 && x<=13?5:3));
	f.writes=0; const uint16_t reversed[]={13,14,8,14}; CHECK(span(&f,false,reversed)==0 && f.writes==4);
	init(&f); f.state.bgi.clip=0; CHECK(span(&f,false,p)==0 && f.writes==6 && f.pixels[14*64+8]==5);
	init(&f); const uint16_t invisible[]={1,1,3,1}; CHECK(span(&f,false,invisible)==0 && f.writes==0);
	init(&f); const uint8_t pattern[]={0,0,0,0,0,0,0x81,0}; CHECK(st_big_fill_custom(&f.state,pattern,5)==0);
	const uint16_t patterned[]={10,14,19,14}; CHECK(span(&f,false,patterned)==0 && f.writes==10);
	static const struct visit expected[]={
		{15,14,5,false},{10,14,0,false},{11,14,0,false},{12,14,0,false},{13,14,0,false},{14,14,0,false},
		{16,14,5,false},{17,14,0,false},{18,14,0,false},{19,14,0,false}
	};
	for (size_t i=0;i<10;++i) CHECK(f.trace[i].x==expected[i].x && f.trace[i].y==expected[i].y && f.trace[i].pen==expected[i].pen && !f.trace[i].xor_mode);
}
static void raw_line(void)
{
	struct fixture f; init(&f); CHECK(st_big_draw_style(&f.state,4,0xcccc,3)==0); f.state.bgi.write_mode=1;
	struct st_big_state before=f.state; const uint16_t p[]={0,2,7,2};
	CHECK(span(&f,true,p)==0 && f.writes==4 && memcmp(&before,&f.state,sizeof(before))==0);
	static const uint16_t xs[]={2,3,6,7};
	for (size_t i=0;i<4;++i) CHECK(f.trace[i].x==xs[i] && f.trace[i].y==2 && f.trace[i].pen==9 && f.trace[i].xor_mode);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(y==2 && (x==2 || x==3 || x==6 || x==7)?10:3));
	f.writes=0; const uint16_t reversed[]={7,2,0,2}; CHECK(span(&f,true,reversed)==0 && f.writes==4);
	static const uint16_t reverse_x[]={5,4,1,0}; for (size_t i=0;i<4;++i) CHECK(f.trace[i].x==reverse_x[i]);
	init(&f); const uint16_t diagonal[]={0,0,4,2}; CHECK(span(&f,true,diagonal)==0 && f.writes==5);
	static const uint16_t candidates[][2]={{0,0},{1,1},{2,1},{3,2},{4,2}};
	for (size_t i=0;i<5;++i) CHECK(f.trace[i].x==candidates[i][0] && f.trace[i].y==candidates[i][1]);
	/* Each raw request starts at pattern bit0, without viewport clipping,
	 * thickness expansion, origin addition or public/DDO/device stores. */
	init(&f); CHECK(st_big_draw_style(&f.state,4,1,3)==0); before=f.state;
	CHECK(span(&f,true,p)==0 && span(&f,true,p)==0 && f.writes==2 && f.trace[0].x==0 && f.trace[1].x==0);
	CHECK(memcmp(&before,&f.state,sizeof(before))==0);
	init(&f); CHECK(st_big_draw_style(&f.state,4,0,3)==0); CHECK(span(&f,true,p)==0 && f.writes==0);
}
static void failures(void)
{
	struct fixture f; const uint16_t p[]={10,14,13,14};
	for (unsigned raw=0;raw<2;++raw) for (unsigned busy=0;busy<2;++busy) {
		init(&f); f.fail_at=2; if (busy) f.error=IO_ERROR_IN_PROGRESS; struct st_big_state before=f.state;
		CHECK(span(&f,raw!=0,p)==(busy?IO_ERROR_PROTOCOL:IO_ERROR_IO) && f.writes==2);
		CHECK(f.pixels[14*64+10]==(raw?9:5) && f.pixels[14*64+11]==(raw?9:5) && f.pixels[14*64+12]==3);
		CHECK(memcmp(&before,&f.state,sizeof(before))==0);
	}
	static const uint16_t escaped[][4]={{63,2,64,2},{0,64,1,64},{0xffff,2,1,2},{0,0x8000,1,0}};
	for (size_t i=0;i<sizeof(escaped)/sizeof(escaped[0]);++i) {
		init(&f); CHECK(span(&f,true,escaped[i])==IO_ERROR_OUT_OF_RANGE && f.writes==0);
	}
	init(&f); f.state.bgi.state=0; CHECK(span(&f,true,p)==IO_ERROR_PROTOCOL && f.state.bgi.error==-1 && f.writes==0);
	init(&f); const struct st_big_draw_sink sink={&f,pixel},missing={0};
	CHECK(st_big_draw_polygon_span(NULL,false,p,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_polygon_span(&f.state,false,NULL,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_polygon_span(&f.state,false,p,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_polygon_span(&f.state,true,p,&missing)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { physical_bar(); raw_line(); failures(); return 0; }
