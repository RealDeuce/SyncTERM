/* Literal shapes, visits and point/producer prefixes, original BIG23/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_bar3d.h"
#include "st_big_fill.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct visit { uint16_t x,y; uint8_t pen; bool xor_mode; };
struct fixture {
	struct st_big_state s;
	struct st_big_producer producer;
	struct st_big_producer_point points[32];
	uint8_t pixels[64*64];
	struct visit trace[8192];
	size_t count, fail_at, reads;
	bool mutate;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s);
	f->s.bgi.width=f->s.bgi.height=64; f->s.bgi.viewport[2]=f->s.bgi.viewport[3]=63;
	f->s.bgi.x=31; f->s.bgi.y=32; f->s.bgi.ddo_x=33; f->s.bgi.ddo_y=34;
	f->s.bgi.device_x=35; f->s.bgi.device_y=36;
	CHECK(st_big_draw_color(&f->s,9)==0 && st_big_fill_builtin(&f->s,1,5)==0);
	CHECK(st_big_producer_bind(&f->producer,f->points,32,192)==0);
	f->producer.video_segment=0xa000; f->fail_at=SIZE_MAX;
	memset(f->pixels,3,sizeof(f->pixels));
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<16 && f->count<8192);
	if (f->count==f->fail_at) return IO_ERROR_IO;
	f->trace[f->count++]=(struct visit){x,y,pen,xor_mode};
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	if (f->mutate && f->count==1) { f->s.bgi.color=0x23; f->s.bgi.fill_low=7; f->s.bgi.device_color=1; }
	return 0;
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && plane==0); ++f->reads; *byte=0x5a; return 0;
}
static struct st_big_producer_sink sink(struct fixture *f)
{
	return (struct st_big_producer_sink){.drawing={f,pixel},.read_context=f,.read_byte=read_byte};
}
static void points(const struct fixture *f, uint16_t dx, uint16_t dy, uint16_t vx, uint16_t vy)
{
	CHECK(f->s.bgi.x==31 && f->s.bgi.y==32);
	CHECK(f->s.bgi.ddo_x==dx && f->s.bgi.ddo_y==dy && f->s.bgi.device_x==vx && f->s.bgi.device_y==vy);
}
static void front(void)
{
	const uint16_t p[]={14,14,10,10}; struct fixture f;
	for (unsigned xor_mode=0;xor_mode<2;++xor_mode) {
		init(&f); struct st_big_producer_sink io=sink(&f); f.s.bgi.write_mode=(uint16_t)xor_mode;
		CHECK(st_big_draw_bar3d(&f.s,p,0,0,&f.producer,&io)==0 && f.count==29);
		points(&f,14,14,14,14);
		for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
			uint8_t expected=3;
			if (x>10 && x<14 && y>10 && y<14) expected=5;
			if (x>=10 && x<=14 && y>=10 && y<=14 && (x==10 || x==14 || y==10 || y==14))
				expected=xor_mode?((x==10 || x==14) && (y==10 || y==14)?3:10):9;
			CHECK(f.pixels[y*64+x]==expected);
		}
	}
	init(&f); struct st_big_producer_sink io=sink(&f); f.s.bgi.line_mask=0xcccc;
	CHECK(st_big_draw_bar3d(&f.s,p,0,0,&f.producer,&io)==0 && f.count==17);
	static const uint16_t patterned[][2]={{10,12},{10,13},{12,10},{13,10},{14,12},{14,13},{12,14},{11,14}};
	for (size_t i=0;i<8;++i) CHECK(f.trace[9+i].x==patterned[i][0] && f.trace[9+i].y==patterned[i][1]);
	/* Inverted inset sorts again, expanding a nominal one-column face. */
	init(&f); f.s.bgi.line_mask=0; const uint16_t narrow[]={10,10,10,20};
	CHECK(st_big_draw_bar3d(&f.s,narrow,0,0,&f.producer,&io)==0 && f.count==27);
	CHECK(f.pixels[11*64+9]==5 && f.pixels[19*64+11]==5 && f.pixels[10*64+10]==3);
	points(&f,10,20,10,20);
}
static void projection(void)
{
	const uint16_t p[]={10,10,14,14}; struct fixture f;
	for (unsigned orientation=0;orientation<2;++orientation) for (unsigned top=0;top<2;++top) {
		init(&f); struct st_big_producer_sink io=sink(&f); f.s.bgi.orientation=(uint8_t)orientation;
		CHECK(st_big_draw_bar3d(&f.s,p,4,(uint16_t)(256+top),&f.producer,&io)==0);
		points(&f,orientation?11:18,7,orientation && !top?10:14,top || orientation?10:14);
		CHECK(f.s.bgi.device_color==9 && f.s.bgi.device_fill_color==5 && f.s.bgi.write_mode==0);
		if (!orientation) {
			static const uint16_t side[][2]={{18,11},{17,12},{16,13},{15,13},{14,14},{18,7},{18,8},{18,9},{18,10},{18,11}};
			for (size_t i=0;i<10;++i) CHECK(f.trace[29+i].x==side[i][0] && f.trace[29+i].y==side[i][1]);
		}
	}
	init(&f); struct st_big_producer_sink io=sink(&f); f.s.bgi.line_mask=0;
	CHECK(st_big_draw_bar3d(&f.s,p,30000,0,&f.producer,&io)==0);
	points(&f,30014,59430,14,14);
	init(&f); f.s.bgi.viewport[0]=2; f.s.bgi.viewport[1]=3;
	CHECK(st_big_draw_bar3d(&f.s,p,0,0,&f.producer,&io)==0);
	points(&f,16,17,16,17);
	static const uint16_t corners[]={12,17,16,13}; CHECK(memcmp(f.s.bgi.bar_corners,corners,sizeof(corners))==0);
}
static void collectors(void)
{
	struct fixture f; init(&f); struct st_big_producer_sink io=sink(&f); const uint16_t p[]={10,10,14,14};
	CHECK(st_big_producer_start(&f.producer)==0);
	CHECK(st_big_draw_bar3d(&f.s,p,4,1,&f.producer,&io)==0 && f.count==9 && f.producer.count==12);
	static const struct st_big_producer_point expected[]={
		{10,10},{14,10},{14,14},{10,14},{14,14},{18,11},{18,7},{14,7},{10,10},{0x8001,0x8001},{14,10},{18,7}
	};
	CHECK(memcmp(f.points,expected,sizeof(expected))==0 && f.producer.run_length==2);
	points(&f,10,14,10,14);
	init(&f); CHECK(st_big_producer_start(&f.producer)==0); f.producer.capacity=2;
	CHECK(st_big_draw_bar3d(&f.s,p,4,1,&f.producer,&io)==0 && f.count==9 && f.producer.count==2 && f.s.bgi.error==-6);
	points(&f,10,14,10,14);
	static const struct { uint16_t depth,x,y; } wrapped[]={
		{21845,21859,49163},{21846,21860,10},{65535,13,49163}
	};
	for (size_t i=0;i<sizeof(wrapped)/sizeof(wrapped[0]);++i) {
		init(&f); CHECK(st_big_producer_start(&f.producer)==0);
		CHECK(st_big_draw_bar3d(&f.s,p,wrapped[i].depth,0,&f.producer,&io)==0 && f.producer.count==7);
		CHECK(f.points[6].x==wrapped[i].x && f.points[6].y==wrapped[i].y);
	}
}
static void point_producers(void)
{
	struct fixture f; const uint16_t p[]={10,10,14,14};
	for (unsigned batch=0;batch<2;++batch) for (unsigned top=0;top<2;++top) {
		init(&f); struct st_big_producer_sink io=sink(&f);
		f.producer.mode=2; f.producer.batch=batch!=0;
		CHECK(st_big_draw_bar3d(&f.s,p,4,(uint16_t)top,&f.producer,&io)==0);
		points(&f,top?535:batch?0xa05a:0xf02,top?1:2,batch?0xa05a:0xf02,6);
		CHECK(f.producer.count==0 && f.producer.run_length==0 && f.s.bgi.device_color==9);
		if (batch) CHECK(f.reads==(top?9u:7u));
	}
}
static void failures_and_tail(void)
{
	struct fixture f; init(&f); struct st_big_producer_sink io=sink(&f); const uint16_t p[]={10,10,14,14};
	f.fail_at=5; f.mutate=true;
	CHECK(st_big_draw_bar3d(&f.s,p,4,1,&f.producer,&io)==IO_ERROR_IO && f.count==5);
	points(&f,10,14,10,14); CHECK(f.s.bgi.device_color==1 && f.s.bgi.device_fill_color==5 && f.s.bgi.error==0);
	init(&f); f.mutate=true;
	CHECK(st_big_draw_bar3d(&f.s,p,0,0,&f.producer,&io)==0);
	CHECK(f.s.bgi.device_color==3 && f.s.bgi.device_fill_color==7 && f.s.bgi.fill_pixels[0]==5 && f.s.bgi.fill_color==5);
	CHECK(f.pixels[10*64+10]==1); points(&f,14,14,14,14);
	init(&f); const uint16_t rejected[]={100,100,104,104}; f.s.bgi.error=-11;
	CHECK(st_big_draw_bar3d(&f.s,rejected,0,0,&f.producer,&io)==0 && f.count==0 && f.s.bgi.error==-11);
	points(&f,104,104,104,104);
	init(&f); f.s.bgi.state=0;
	CHECK(st_big_draw_bar3d(&f.s,p,0,0,&f.producer,&io)==IO_ERROR_PROTOCOL && f.s.bgi.error==-1 && f.count==0);
	CHECK(st_big_draw_bar3d(NULL,p,0,0,&f.producer,&io)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar3d(&f.s,NULL,0,0,&f.producer,&io)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar3d(&f.s,p,0,0,NULL,&io)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_bar3d(&f.s,p,0,0,&f.producer,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { front(); projection(); collectors(); point_producers(); failures_and_tail(); return 0; }
