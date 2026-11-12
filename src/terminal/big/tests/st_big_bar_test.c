/* Independent literal bar clips/passes and indexed masks, BIG11/23/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_draw.h"
#include "st_big_fill.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct visit { uint16_t x, y; uint8_t pen; };
struct fixture {
	struct st_big_state state;
	uint8_t pixels[64*64];
	struct visit trace[8192];
	size_t count, fail_at;
	int fail_status;
	bool mutate;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64;
	f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.bgi.x=31; f->state.bgi.y=32;
	f->state.bgi.ddo_x=33; f->state.bgi.ddo_y=34;
	f->state.bgi.device_x=35; f->state.bgi.device_y=36;
	memset(f->pixels,3,sizeof(f->pixels));
	f->fail_at=SIZE_MAX; f->fail_status=IO_ERROR_IO;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context;
	CHECK(x<64 && y<64 && pen<16 && !xor_mode && f->count<8192);
	if (f->count==f->fail_at) return f->fail_status;
	f->trace[f->count++]=(struct visit){x,y,pen};
	f->pixels[y*64+x]=pen;
	if (f->mutate && f->count==1) {
		f->state.bgi.device_fill_color=7;
		f->state.bgi.fill_pattern[0]=f->state.bgi.fill_pattern[1]=0;
	}
	return 0;
}
static struct st_big_draw_sink sink(struct fixture *f) { return (struct st_big_draw_sink){f,pixel}; }
static void clipping(void)
{
	static const struct { uint16_t bounds[4], in[4], out[4]; bool visible; } cases[]={
		{{0,0,63,63},{9,8,2,3},{2,3,9,8},true},
		{{0,0,63,63},{65535,65535,2,3},{0,0,2,3},true},
		{{0,0,63,63},{32768,1,32767,1},{0,1,63,1},true},
		{{0,0,63,63},{32767,32767,32768,32768},{0,0,63,63},true},
		{{0,0,63,63},{65535,65535,65534,65533},{65534,65533,65535,65535},false},
		{{0,0,63,63},{65,21,64,20},{64,20,65,21},false},
		{{0,0,63,63},{2,64,3,65},{2,64,3,65},false},
		{{0,0,63,63},{63,63,63,63},{63,63,63,63},true},
		{{10,12,20,22},{0,30,30,0},{10,12,20,22},true},
		{{10,12,5,7},{0,0,20,20},{10,12,5,7},true},
		{{10,12,5,7},{8,8,9,9},{8,8,9,9},false},
		{{65534,65534,1,1},{65532,65532,2,2},{65534,65534,1,1},true},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		uint16_t p[4]; bool visible=false; memcpy(p,cases[i].in,sizeof(p));
		CHECK(st_big_draw_bar_clip(cases[i].bounds,p,&visible)==0);
		CHECK(visible==cases[i].visible && memcmp(p,cases[i].out,sizeof(p))==0);
	}
	uint16_t p[4]={0}, b[4]={0}; bool visible;
	CHECK(st_big_draw_bar_clip(NULL,p,&visible)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar_clip(b,NULL,&visible)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar_clip(b,p,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
static void trace_is(const struct fixture *f, const struct visit *expected, size_t n)
{
	CHECK(f->count==n);
	for (size_t i=0;i<n;++i)
		CHECK(f->trace[i].x==expected[i].x && f->trace[i].y==expected[i].y && f->trace[i].pen==expected[i].pen);
}
static void passes(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	const uint8_t pattern[8]={0x81,0x42,0,0,0,0,0,0};
	CHECK(st_big_fill_custom(&f.state,pattern,5)==0);
	const uint16_t single[]={2,0,7,0};
	CHECK(st_big_draw_bar(&f.state,single,&out)==0);
	static const struct visit one[]={{7,0,5},{2,0,0},{3,0,0},{4,0,0},{5,0,0},{6,0,0}};
	trace_is(&f,one,sizeof(one)/sizeof(one[0]));
	f.count=0; const uint16_t two[]={6,0,10,0};
	CHECK(st_big_draw_bar(&f.state,two,&out)==0);
	static const struct visit edges[]={{7,0,5},{6,0,0},{8,0,5},{9,0,0},{10,0,0}};
	trace_is(&f,edges,sizeof(edges)/sizeof(edges[0]));
	f.count=0; const uint16_t wide[]={6,0,25,0};
	CHECK(st_big_draw_bar(&f.state,wide,&out)==0);
	static const struct visit middle[]={
		{7,0,5},{6,0,0},
		{8,0,0},{9,0,0},{10,0,0},{11,0,0},{12,0,0},{13,0,0},{14,0,0},{15,0,0},
		{16,0,0},{17,0,0},{18,0,0},{19,0,0},{20,0,0},{21,0,0},{22,0,0},{23,0,0},
		{8,0,5},{15,0,5},{16,0,5},{23,0,5},{24,0,5},{25,0,0}
	};
	trace_is(&f,middle,sizeof(middle)/sizeof(middle[0]));
	f.count=0; memset(f.state.bgi.fill_pattern,255,8);
	CHECK(st_big_draw_bar(&f.state,wide,&out)==0 && f.count==20);
	for (size_t i=0;i<f.count;++i) CHECK(f.trace[i].x==6+i && f.trace[i].y==0 && f.trace[i].pen==5);
	f.count=0; memset(f.state.bgi.fill_pattern,0,8);
	CHECK(st_big_draw_bar(&f.state,wide,&out)==0 && f.count==20);
	for (size_t i=0;i<f.count;++i) CHECK(f.trace[i].x==6+i && f.trace[i].pen==0);
}
static void masks(void)
{
	/* Every row byte and both endpoint phases; one edge, two edges and
	 * multiple middle bytes. The expected image is a direct mask truth table. */
	struct fixture f;
	for (unsigned row=0;row<256;++row) for (unsigned left=0;left<8;++left)
	for (unsigned right=0;right<8;++right) for (unsigned bytes=0;bytes<4;++bytes) {
		if (bytes==0 && right<left) continue;
		init(&f); struct st_big_draw_sink out=sink(&f);
		memset(f.state.bgi.fill_pattern,(int)row,8); f.state.bgi.device_fill_color=5;
		const uint16_t p[]={(uint16_t)left,0,(uint16_t)(bytes*8+right),0};
		CHECK(st_big_draw_bar(&f.state,p,&out)==0);
		for (unsigned x=0;x<40;++x) {
			uint8_t expected=3;
			if (x>=left && x<=bytes*8+right) expected=((row>>(7-x%8))&1)!=0?5:0;
			CHECK(f.pixels[x]==expected);
			CHECK(f.pixels[64+x]==3);
		}
	}
	init(&f); struct st_big_draw_sink out=sink(&f);
	const uint8_t diagonal[]={128,64,32,16,8,4,2,1};
	CHECK(st_big_fill_custom(&f.state,diagonal,6)==0);
	const uint16_t p[]={5,5,19,19};
	CHECK(st_big_draw_bar(&f.state,p,&out)==0);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		uint8_t expected=3;
		if (x>=5 && x<=19 && y>=5 && y<=19) expected=x%8==y%8?6:0;
		CHECK(f.pixels[y*64+x]==expected);
	}
}
static void state_and_geometry(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	f.state.bgi.viewport[0]=10; f.state.bgi.viewport[1]=12;
	f.state.bgi.viewport[2]=20; f.state.bgi.viewport[3]=22;
	CHECK(st_big_fill_builtin(&f.state,1,5)==0);
	f.state.bgi.write_mode=1; f.state.bgi.line_mask=0; f.state.bgi.ddo_line_width=3;
	f.state.bgi.error=-11;
	struct st_big_state before=f.state;
	/* WORD wrap occurs before signed sorting: x -11+10 => -1,
	 * y -13+12 => -1, other corner becomes12,14. */
	const uint16_t p[]={65525,65523,2,2};
	CHECK(st_big_draw_bar(&f.state,p,&out)==0 && f.count==9);
	CHECK(memcmp(&before,&f.state,sizeof(before))==0);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x)
		CHECK(f.pixels[y*64+x]==(x>=10 && x<=12 && y>=12 && y<=14?5:3));
	/* Disable clipping without disabling translation. */
	f.count=0; f.state.bgi.clip=0;
	CHECK(st_big_draw_bar(&f.state,p,&out)==0 && f.count==13*15);
	CHECK(f.pixels[0]==5 && f.pixels[14*64+12]==5);
	/* A rejected request preserves every point and the pending error. */
	f.count=0; const uint16_t rejected[]={100,100,101,101}; before=f.state;
	CHECK(st_big_draw_bar(&f.state,rejected,&out)==0 && f.count==0);
	CHECK(memcmp(&before,&f.state,sizeof(before))==0);
	/* Signed sorting spans the entire accepted clip; line clipping's
	 * signed-delta overflow rejection does not apply to a rectangle. */
	init(&f); const uint16_t full[]={32768,32768,32767,32767};
	CHECK(st_big_draw_bar(&f.state,full,&out)==0 && f.count==4096);
	CHECK(f.pixels[0]==15 && f.pixels[4095]==15);
	/* Native geometry cannot represent an accepted foreign negative bound. */
	init(&f); f.state.bgi.viewport[0]=65534; f.state.bgi.viewport[1]=65534;
	const uint16_t escape[]={0,0,3,3};
	CHECK(st_big_draw_bar(&f.state,escape,&out)==IO_ERROR_OUT_OF_RANGE && f.count==0);
	/* Inverted bounds keep the original lower-first clamping followed by
	 * the raw UNSIGNED sort, rather than an invented empty-viewport check. */
	init(&f); f.state.bgi.viewport[0]=10; f.state.bgi.viewport[1]=12;
	f.state.bgi.viewport[2]=5; f.state.bgi.viewport[3]=7;
	const uint16_t inverted[]={65526,65524,10,8};
	CHECK(st_big_draw_bar(&f.state,inverted,&out)==0 && f.count==36);
	CHECK(f.pixels[7*64+5]==15 && f.pixels[12*64+10]==15);
}
static void current_fill(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	for (unsigned pen=0;pen<16;++pen) for (unsigned xor_mode=0;xor_mode<2;++xor_mode) {
		init(&f); f.state.bgi.device_fill_color=(uint8_t)pen;
		f.state.bgi.write_mode=(uint16_t)xor_mode;
		f.state.bgi.line_mask=0; f.state.bgi.ddo_line_width=3;
		const uint16_t dot[]={9,9,9,9};
		CHECK(st_big_draw_bar(&f.state,dot,&out)==0 && f.count==1 && f.pixels[9*64+9]==pen);
		CHECK(f.pixels[9*64+8]==3 && f.pixels[9*64+10]==3);
	}
	init(&f);
	CHECK(st_big_fill_builtin(&f.state,1,5)==0);
	f.state.bgi.fill_low=7;
	CHECK(st_big_draw_color(&f.state,9)==0 && f.state.bgi.device_fill_color==7 && f.state.bgi.fill_pixels[0]==5);
	const uint16_t p[]={0,0,7,0};
	CHECK(st_big_draw_bar(&f.state,p,&out)==0);
	for (unsigned x=0;x<8;++x) CHECK(f.pixels[x]==7);
	/* Rejected custom installation leaves active mask/current pen intact,
	 * independent of selected app slot and stale public fill colour. */
	CHECK(st_big_fill_custom(&f.state,(const uint8_t[8]){0},256)==0 && f.state.bgi.error==-11);
	f.count=0; CHECK(st_big_draw_bar(&f.state,p,&out)==0 && f.pixels[0]==7);
	init(&f); CHECK(st_big_fill_builtin(&f.state,1,5)==0); f.mutate=true;
	const uint16_t two[]={0,0,15,1};
	CHECK(st_big_draw_bar(&f.state,two,&out)==0 && f.count==32);
	for (unsigned x=0;x<16;++x) CHECK(f.pixels[x]==5 && f.pixels[64+x]==0);
}
static void failures(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	const uint8_t pattern[8]={0x81}; CHECK(st_big_fill_custom(&f.state,pattern,5)==0);
	const uint16_t p[]={6,0,25,0}; struct st_big_state before=f.state;
	f.fail_at=10;
	CHECK(st_big_draw_bar(&f.state,p,&out)==IO_ERROR_IO && f.count==10);
	CHECK(memcmp(&before,&f.state,sizeof(before))==0);
	CHECK(f.pixels[6]==0 && f.pixels[7]==5);
	for (unsigned x=8;x<16;++x) CHECK(f.pixels[x]==0);
	for (unsigned x=16;x<26;++x) CHECK(f.pixels[x]==3);
	f.count=0; f.fail_at=0; f.fail_status=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_draw_bar(&f.state,p,&out)==IO_ERROR_PROTOCOL && f.count==0);
	f.state.bgi.state=0; f.state.bgi.error=-11;
	CHECK(st_big_draw_bar(&f.state,p,&out)==IO_ERROR_PROTOCOL && f.state.bgi.error==-1 && f.count==0);
	init(&f); f.state.bgi.device_fill_color=16;
	CHECK(st_big_draw_bar(&f.state,p,&out)==IO_ERROR_OUT_OF_RANGE && f.count==0);
	CHECK(st_big_draw_bar(NULL,p,&out)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar(&f.state,NULL,&out)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar(&f.state,p,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar(&f.state,p,&(struct st_big_draw_sink){0})==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { clipping(); passes(); masks(); state_and_geometry(); current_fill(); failures(); return 0; }
