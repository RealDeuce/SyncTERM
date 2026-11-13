/* Literal line/marker/rectangle expectations from BIG12/22/23/57/58/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_draw.h"
#include "st_bgi_line.h"
#include "st_bgi_data.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_state state;
	uint8_t pixels[64*64];
	uint16_t trace[4096][2]; size_t count, fail_at;
	unsigned mutate; int fail_status;
	uint16_t *rectangle_args;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); f->fail_at=SIZE_MAX; f->fail_status=IO_ERROR_IO; st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64; f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.application.colors[0]=5;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<16 && f->count<4096);
	if (f->count==f->fail_at) return f->fail_status;
	f->trace[f->count][0]=x; f->trace[f->count][1]=y; ++f->count;
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	if (f->count==1) {
		if (f->mutate==1) f->state.application.marker=2;
		if (f->mutate==2) { f->state.bgi.x=20; f->state.bgi.y=30; }
		if (f->mutate==3) f->state.bgi.line_distance=2;
		if (f->mutate==4) f->state.application.marker_size=2;
		if (f->mutate==5) {
			f->rectangle_args[0]=f->rectangle_args[1]=20;
			f->rectangle_args[2]=f->rectangle_args[3]=12;
		}
	}
	return 0;
}
static struct st_big_draw_sink sink(struct fixture *f) { return (struct st_big_draw_sink){f,pixel}; }
static void raw_pixel(void *context, int x, int y)
{
	struct fixture *f=context; CHECK(f->count<4096);
	f->trace[f->count][0]=(uint16_t)x; f->trace[f->count++][1]=(uint16_t)y;
}
static void shared_lines(void)
{
	static const uint16_t tie[][2]={{0,0},{1,1},{2,1},{3,2},{4,2}};
	static const uint16_t steep[][2]={{0,0},{1,1},{1,2},{2,3},{2,4}};
	for (unsigned reverse=0;reverse<2;++reverse) {
		struct fixture f; init(&f); int phase=0; uint16_t pattern=65535;
		st_bgi_thin_line(reverse?4:0,reverse?2:0,reverse?0:4,reverse?0:2,&pattern,&phase,raw_pixel,&f);
		CHECK(f.count==5 && phase==5 && memcmp(f.trace,tie,sizeof(tie))==0);
		f.count=0; phase=0; st_bgi_thin_line(0,0,2,4,&pattern,&phase,raw_pixel,&f);
		CHECK(f.count==5 && memcmp(f.trace,steep,sizeof(steep))==0);
	}
	static const uint16_t falling[][2]={{4,0},{3,1},{2,1},{1,2},{0,2}};
	static const uint16_t falling_steep[][2]={{2,0},{1,1},{1,2},{0,3},{0,4}};
	for (unsigned reverse=0;reverse<2;++reverse) {
		struct fixture f; init(&f); int phase=0; uint16_t pattern=65535;
		st_bgi_thin_line(reverse?0:4,reverse?2:0,reverse?4:0,reverse?0:2,&pattern,&phase,raw_pixel,&f);
		CHECK(f.count==5 && memcmp(f.trace,falling,sizeof(falling))==0);
		f.count=0; phase=0;
		st_bgi_thin_line(reverse?0:2,reverse?4:0,reverse?2:0,reverse?0:4,&pattern,&phase,raw_pixel,&f);
		CHECK(f.count==5 && memcmp(f.trace,falling_steep,sizeof(falling_steep))==0);
	}
	struct fixture f; init(&f); uint16_t pattern=0xcccc; int phase=0;
	st_bgi_thin_line(0,0,7,0,&pattern,&phase,raw_pixel,&f);
	CHECK(f.count==4 && f.trace[0][0]==2 && f.trace[1][0]==3 && f.trace[2][0]==6 && f.trace[3][0]==7 && phase==8);
	f.count=0; phase=0; st_bgi_thin_line(7,0,0,0,&pattern,&phase,raw_pixel,&f);
	CHECK(f.count==4 && f.trace[0][0]==5 && f.trace[1][0]==4 && f.trace[2][0]==1 && f.trace[3][0]==0);
	f.count=0; phase=15; pattern=0x8001; st_bgi_thin_line(0,0,2,0,&pattern,&phase,raw_pixel,&f);
	CHECK(f.count==2 && f.trace[0][0]==0 && f.trace[1][0]==1 && phase==2);
	f.count=0; pattern=0; st_bgi_thin_line(2,2,2,2,&pattern,&phase,raw_pixel,&f); CHECK(f.count==0 && phase==3);
	static const uint16_t words[]={0xffff,0xcccc,0xfc78,0xf8f8}; CHECK(memcmp(st_bgi_line_patterns,words,sizeof(words))==0);
}
static void clipping(void)
{
	static const uint16_t b[]={0,0,639,349};
	static const struct { uint16_t in[4], out[4]; bool visible; } cases[]={
		{{65535,65535,2,0},{3,0,2,0},true},{{2,0,65535,65535},{2,0,3,0},true},
		{{32768,1,32767,1},{65535,65535,65535,65535},false},
		{{65535,0,3,0},{0,0,3,0},true},{{0,65535,0,3},{0,0,0,3},true},
		{{65535,0,65535,3},{65535,65535,65535,65535},false},
		{{65535,65535,3,3},{0,0,3,3},true},{{4,350,0,346},{3,349,0,346},true},
		{{65535,65535,65535,65535},{65535,65535,65535,65535},false},
		{{639,349,0,0},{639,349,0,0},true},{{640,350,638,348},{639,349,638,348},true},
	};
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		uint16_t p[4]; bool visible; memcpy(p,cases[i].in,sizeof(p));
		CHECK(st_big_draw_clip(b,p,&visible)==0 && visible==cases[i].visible && memcmp(p,cases[i].out,sizeof(p))==0);
	}
}
/* Rows x6..14, y6..18, bit0 at x6. Each is a hand-authored shape mask. */
static const uint16_t masks[7][13]={
	{0,0,0,0,0x10,0x38,0x54,0x92,0x111,0x10,0x10,0x10,0x10},
	{0,0x10,0x10,0x10,0x1ff,0x10,0x10,0x10,0},
	{0x10,0x92,0x54,0x38,0x1ff,0x38,0x54,0x92,0x10},
	{0x1ff,0x101,0x101,0x101,0x101,0x101,0x101,0x101,0x1ff},
	{0x101,0x82,0x44,0x28,0x10,0x28,0x44,0x82,0x101},
	{0x10,0x28,0x44,0x82,0x101,0x82,0x44,0x28,0x10},
	{0x1ff,0x183,0x145,0x129,0x111,0x129,0x145,0x183,0x1ff},
};
static void markers(void)
{
	static const size_t visits[]={19,18,36,36,20,20,56};
	for (unsigned type=1;type<=7;++type) for (unsigned xor_mode=0;xor_mode<2;++xor_mode) {
		struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
		f.state.application.marker=(uint8_t)type; f.state.bgi.write_mode=(uint16_t)xor_mode;
		CHECK(st_big_draw_marker(&f.state,10,10,&out)==0 && f.count==visits[type-1]);
		CHECK(f.state.bgi.x==(type==2?14:10) && f.state.bgi.y==10);
		CHECK(f.state.bgi.ddo_x==f.state.bgi.x && f.state.bgi.device_x==f.state.bgi.x);
		CHECK(f.state.bgi.color==5 && f.state.bgi.device_color==5);
		for (size_t y=0;y<64;++y) for (size_t x=0;x<64;++x) {
			bool selected=x>=6 && x<=14 && y>=6 && y<=18 && (masks[type-1][y-6]&(1u<<(x-6)));
			if (xor_mode) {
				if ((type==2 || type==3 || type==5 || type==7) && x==10 && y==10) selected=false;
				if (type==4 && (x==6 || x==14) && (y==6 || y==14)) selected=false;
				if (type==6 && ((x==10 && (y==6 || y==14)) || (y==10 && (x==6 || x==14)))) selected=false;
			}
			CHECK(f.pixels[y*64+x]==(selected?5:0));
		}
	}
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	f.state.application.marker=4; f.state.bgi.line_mask=0xcccc;
	CHECK(st_big_draw_marker(&f.state,10,10,&out)==0);
	static const uint16_t patterned[]={0x66,0,0x101,0x101,0,0,0x101,0x101,0xcc};
	for (size_t y=0;y<9;++y) for (size_t x=0;x<9;++x)
		CHECK(f.pixels[(y+6)*64+x+6]==((patterned[y]&(1u<<x))?5:0));
	init(&f); f.state.application.marker=1; f.state.application.marker_size=0; f.state.bgi.write_mode=1;
	CHECK(st_big_draw_marker(&f.state,10,10,&out)==0 && f.count==3 && f.pixels[10*64+10]==5);
	init(&f); CHECK(st_big_draw_move(&f.state,3,4)==0);
	CHECK(st_big_draw_marker(&f.state,10,10,&out)==0 && f.count==0 && f.state.bgi.x==3 && f.state.bgi.y==4);
	f.state.application.marker=9;
	CHECK(st_big_draw_marker(&f.state,10,10,&out)==0 && f.count==0 && f.state.bgi.x==3);
}
static void points_and_lines(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	f.state.bgi.error=-11; CHECK(st_big_draw_color(&f.state,0x123)==0);
	CHECK(f.state.bgi.color==0x23 && f.state.bgi.device_color==3 && f.state.bgi.error==-11);
	f.state.bgi.write_mode=1; f.state.bgi.line_mask=0; f.state.bgi.line_width=3; f.state.bgi.ddo_line_width=3;
	CHECK(st_big_draw_point(&f.state,9,2,0x125,&out)==0 && f.pixels[2*64+9]==5 && f.state.bgi.x==0);
	CHECK(st_big_draw_point(&f.state,9,2,5,&out)==0 && f.pixels[2*64+9]==5); /* Point copies even under XOR. */
	CHECK(st_big_draw_point(&f.state,65535,10,5,&out)==0 && f.count==2);
	init(&f); f.state.bgi.line_width=3; f.state.bgi.ddo_line_width=3; CHECK(st_big_draw_color(&f.state,5)==0);
	const uint16_t edge[]={0,65535,3,65535}; CHECK(st_big_draw_line(&f.state,edge,&out)==0 && f.count==4);
	for (size_t x=0;x<4;++x) CHECK(f.pixels[x]==5);
	init(&f); CHECK(st_big_draw_color(&f.state,5)==0); f.state.bgi.line_mask=0xcccc;
	CHECK(st_big_draw_move(&f.state,0,0)==0 && st_big_draw_to(&f.state,7,0,&out)==0);
	CHECK(f.count==4 && f.trace[0][0]==5 && f.trace[3][0]==0 && f.state.bgi.x==7 && f.state.bgi.ddo_x==7);
	init(&f); f.fail_at=2; CHECK(st_big_draw_color(&f.state,5)==0);
	CHECK(st_big_draw_to(&f.state,4,0,&out)==IO_ERROR_IO && f.count==2);
	CHECK(f.state.bgi.x==0 && f.state.bgi.ddo_x==4 && f.state.bgi.device_x==0);
	init(&f); f.fail_at=0; f.fail_status=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_draw_point(&f.state,1,1,5,&out)==IO_ERROR_PROTOCOL);
	CHECK(st_big_draw_to(&f.state,4,0,&out)==IO_ERROR_PROTOCOL && f.state.bgi.x==0 && f.state.bgi.ddo_x==4);
	init(&f); f.mutate=2;
	CHECK(st_big_draw_relative(&f.state,3,4,&out)==0 && f.state.bgi.x==23 && f.state.bgi.y==34);
	init(&f); f.state.bgi.line_width=3; f.state.bgi.ddo_line_width=3; f.mutate=3;
	const uint16_t wide[]={10,10,13,10}; CHECK(st_big_draw_line(&f.state,wide,&out)==0 && f.count==12);
	CHECK(f.trace[0][1]==10 && f.trace[4][1]==8 && f.trace[8][1]==12);
	init(&f); f.state.bgi.viewport[0]=2; f.state.bgi.viewport[1]=3;
	CHECK(st_big_draw_move(&f.state,4,5)==0 && f.state.bgi.x==4 && f.state.bgi.y==5 && f.state.bgi.ddo_x==6 && f.state.bgi.device_y==8);
	CHECK(st_big_draw_point(&f.state,0,0,5,&out)==0 && f.pixels[3*64+2]==5);
	f.state.bgi.state=0; CHECK(st_big_draw_color(&f.state,0x127)==IO_ERROR_PROTOCOL && f.state.bgi.color==0x27);
}
static void styles(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	for (uint16_t style=0;style<5;++style) for (uint16_t width=0;width<=3;++width) {
		CHECK(st_big_draw_style(&f.state,style,0x1234,width)==0);
		CHECK(f.state.bgi.line_style==style && f.state.bgi.line_user==0x1234 && f.state.bgi.line_width==width);
		CHECK(f.state.bgi.line_mask==(style==4?0x1234:st_bgi_line_patterns[style]));
		CHECK(f.state.bgi.ddo_line_width==width && f.state.bgi.device_line_width==width);
		CHECK(f.state.bgi.ddo_line_style==style && f.state.bgi.ddo_line_user==0x1234);
	}
	CHECK(st_big_draw_style(&f.state,0,17,1)==0 && st_big_draw_style(&f.state,4,0,9)==0);
	CHECK(f.state.bgi.error==-11 && f.state.bgi.line_style==0 && f.state.bgi.line_width==1 && f.state.bgi.line_mask==65535);
	CHECK(st_big_draw_to(&f.state,4,0,&out)==0 && f.count==5);
	CHECK(st_big_draw_style(&f.state,1,7,2)==0 && f.state.bgi.error==-11 && f.state.bgi.line_mask==0xcccc && f.state.bgi.ddo_line_width==2);
	f.state.bgi.state=0;
	CHECK(st_big_draw_style(&f.state,2,9,3)==IO_ERROR_PROTOCOL);
	CHECK(f.state.bgi.line_style==2 && f.state.bgi.line_user==9 && f.state.bgi.line_width==3 && f.state.bgi.line_mask==0xcccc && f.state.bgi.ddo_line_width==2);
	CHECK(f.state.bgi.ddo_line_style==1 && f.state.bgi.ddo_line_user==7);
}
static void live_markers(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	f.state.application.marker=1; f.mutate=1;
	/* The first block still finishes, then independent type2 also runs. */
	CHECK(st_big_draw_marker(&f.state,10,10,&out)==0 && f.count==19+18 && f.state.bgi.x==14);
	init(&f); f.state.application.marker=1; f.mutate=4;
	CHECK(st_big_draw_marker(&f.state,10,10,&out)==0 && f.state.application.marker_size==2);
	CHECK(f.pixels[14*64+14]==5 && f.pixels[18*64+2]==5 && f.pixels[26*64+10]==5);
}
static void rectangles(void)
{
	struct fixture f; init(&f); struct st_big_draw_sink out=sink(&f);
	f.state.bgi.device_color=5; f.state.bgi.x=1; f.state.bgi.y=2; f.state.bgi.ddo_x=3; f.state.bgi.ddo_y=4;
	f.state.bgi.device_x=5; f.state.bgi.device_y=6; f.state.bgi.error=-11;
	struct st_big_state expected; memcpy(&expected,&f.state,sizeof(expected));
	uint16_t p[]={6,6,14,14}; f.state.bgi.line_mask=0xcccc; expected.bgi.line_mask=0xcccc;
	static const uint16_t trace[][2]={{8,6},{9,6},{12,6},{13,6},{14,8},{14,9},{14,12},{14,13},
	    {12,14},{11,14},{8,14},{7,14},{6,8},{6,9},{6,12},{6,13}};
	int status=st_big_draw_rectangle(&f.state,p,&out);
	if (status!=0 || f.count!=16 || memcmp(f.trace,trace,sizeof(trace))!=0) {
		fprintf(stderr,"rectangle trace status%d count%zu:",status,f.count);
		for (size_t i=0;i<f.count;++i) fprintf(stderr," %u,%u",f.trace[i][0],f.trace[i][1]);
		fputc('\n',stderr);
	}
	CHECK(status==0 && f.count==16 && memcmp(f.trace,trace,sizeof(trace))==0);
	CHECK(memcmp(&expected,&f.state,sizeof(expected))==0);
	init(&f); out=sink(&f); f.state.bgi.device_color=5; f.state.bgi.write_mode=1;
	memset(f.pixels,3,sizeof(f.pixels)); const uint16_t box[]={10,10,12,12};
	CHECK(st_big_draw_rectangle(&f.state,box,&out)==0 && f.count==12);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		bool middle=(x==11 && (y==10 || y==12)) || (y==11 && (x==10 || x==12));
		CHECK(f.pixels[y*64+x]==(middle?6:3));
	}
	f.count=0; const uint16_t dot[]={10,10,10,10};
	CHECK(st_big_draw_rectangle(&f.state,dot,&out)==0 && f.count==4 && f.pixels[10*64+10]==3);
	for (size_t i=0;i<4;++i) CHECK(f.trace[i][0]==10 && f.trace[i][1]==10);
	/* Later arguments stay live, while entry X1/Y1 are captured. */
	init(&f); out=sink(&f); p[0]=p[1]=6; p[2]=p[3]=14; f.mutate=5; f.rectangle_args=p;
	CHECK(st_big_draw_rectangle(&f.state,p,&out)==0 && f.count==30);
	CHECK(f.trace[8][0]==14 && f.trace[8][1]==6 && f.trace[9][0]==12 && f.trace[9][1]==6);
	CHECK(f.trace[15][0]==12 && f.trace[15][1]==12 && f.trace[16][0]==12 && f.trace[16][1]==12);
	/* Chapter57 orders the final vertical edge upper-Y first. */
	CHECK(f.trace[22][0]==6 && f.trace[22][1]==12 && f.trace[23][0]==6 && f.trace[23][1]==6);
	CHECK(f.trace[29][0]==6 && f.trace[29][1]==12);
	init(&f); out=sink(&f); f.fail_at=3;
	CHECK(st_big_draw_rectangle(&f.state,box,&out)==IO_ERROR_IO && f.count==3);
	f.count=0; f.fail_status=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_draw_rectangle(&f.state,box,&out)==IO_ERROR_PROTOCOL && f.count==3);
	f.count=0; f.fail_at=SIZE_MAX; f.state.bgi.state=0;
	CHECK(st_big_draw_rectangle(&f.state,box,&out)==IO_ERROR_PROTOCOL && f.count==0 && f.state.bgi.error==-1);
	CHECK(st_big_draw_rectangle(NULL,box,&out)==IO_ERROR_INVALID_ARGUMENT && st_big_draw_rectangle(&f.state,NULL,&out)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { shared_lines(); clipping(); markers(); points_and_lines(); styles(); live_markers(); rectangles(); return 0; }
