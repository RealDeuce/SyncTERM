/* Independent X0 sample/counter/point/helper traces, original BIG22/58. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_spray.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_state s; struct st_big_spray r; uint32_t random;
	uint8_t pixels[64*64]; uint16_t trace[256][2]; size_t calls,fail;
	int error,mutation;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->s); f->random=1;
	f->s.bgi.width=f->s.bgi.height=64; f->s.bgi.viewport[2]=f->s.bgi.viewport[3]=63;
	f->s.application.colors[0]=5; f->s.bgi.error=-11;
	f->r=(struct st_big_spray){.x=10,.y=10,.width=32,.height=32,.count=4,.counter=77};
	f->fail=SIZE_MAX; f->error=IO_ERROR_IO;
	CHECK(st_big_draw_color(&f->s,5)==0 && st_big_draw_move(&f->s,40,50)==0);
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<16);
	if (f->calls==f->fail) return f->error;
	if (f->calls<256) { f->trace[f->calls][0]=x; f->trace[f->calls][1]=y; }
	++f->calls;
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	if (f->calls==1) {
		if (f->mutation==1) { f->s.application.marker=2; f->s.application.colors[0]=9; f->r.x=30; f->r.y=30; }
		if (f->mutation==2) f->r.count=1;
		if (f->mutation==3) { f->r.counter=65534; f->r.count=65535; }
		if (f->mutation==4) f->s.application.colors[0]=9;
	}
	return 0;
}
static int draw(struct fixture *f) { const struct st_big_draw_sink sink={f,pixel}; return st_big_draw_spray(&f->s,&f->r,&f->random,&sink); }
static void samples_and_signed_counts(void)
{
	struct fixture f; init(&f); CHECK(draw(&f)==0 && f.calls==4 && f.r.counter==4 && f.random==UINT32_C(0x190f3a19));
	static const uint16_t expected[][2]={{10,10},{20,11},{21,16},{27,16}};
	CHECK(memcmp(f.trace,expected,sizeof(expected))==0 && f.s.bgi.x==40 && f.s.bgi.y==50);
	CHECK(f.r.dx==17 && f.r.dy==6 && f.s.bgi.error==-11);
	init(&f); f.r.x=f.r.y=40; f.r.width=f.r.height=65504;
	CHECK(draw(&f)==0 && f.calls==4);
	static const uint16_t negative[][2]={{40,40},{30,39},{29,34},{23,34}};
	CHECK(memcmp(f.trace,negative,sizeof(negative))==0 && f.random==UINT32_C(0x190f3a19));
	const uint16_t counts[]={0,65535,32768,32767};
	for (size_t i=0;i<4;++i) {
		init(&f); f.r.width=f.r.height=0; f.r.count=counts[i];
		CHECK(draw(&f)==0 && f.r.counter==(i==3?32767:0) && f.calls==(i==3?32767u:0u));
		if (i!=3) CHECK(f.random==1 && f.r.dx==0 && f.r.dy==0);
	}
	/* Full signed range domain against two fixed15-bit draws chosen by
	 * an inverse LCG predecessor: X32767, Y12747, no geometry to clip. */
	for (unsigned word=0;word<65536;++word) {
		init(&f); f.random=UINT32_C(0x42bdebe3); f.s.application.marker=9;
		f.r.width=f.r.height=(uint16_t)word; f.r.count=1;
		int range=word<32768?(int)word:(int)word-65536;
		/* Independent magnitude division spells truncation toward zero. */
		unsigned magnitude=(unsigned)(range<0?-range:range);
		int dx=(int)(32767u*magnitude/32768u),dy=(int)(12747u*magnitude/32768u);
		if (range<0) { dx=-dx; dy=-dy; }
		CHECK(draw(&f)==0 && f.calls==0 && f.r.counter==1 && f.r.dx==(uint16_t)dx && f.r.dy==(uint16_t)dy && f.random==UINT32_C(0x31cb0001));
	}
}
static void types_and_failures(void)
{
	for (unsigned type=0;type<=9;++type) {
		struct fixture f; init(&f); f.r.x=f.r.y=20; f.r.width=f.r.height=0; f.r.count=1; f.s.application.marker=(uint8_t)type;
		CHECK(draw(&f)==0 && f.r.counter==1 && f.random==UINT32_C(0x8082a52f));
		CHECK(f.s.bgi.x==(type==2?24:type>=1 && type<=7?20:40) && f.s.bgi.y==(type>=1 && type<=7?20:50));
		if (type==0) CHECK(f.calls==1 && f.pixels[20*64+20]==5);
		if (type>=8) CHECK(f.calls==0);
	}
	struct fixture f; init(&f); f.mutation=1; f.r.count=1; f.r.width=f.r.height=0;
	CHECK(draw(&f)==0 && f.pixels[10*64+10]==5 && f.pixels[30*64+30]==9 && f.s.bgi.x==34 && f.s.bgi.y==30);
	init(&f); f.mutation=2; CHECK(draw(&f)==0 && f.calls==1 && f.r.counter==1);
	init(&f); f.mutation=3; CHECK(draw(&f)==0 && f.calls==1 && f.r.counter==65535);
	init(&f); f.mutation=4; CHECK(draw(&f)==0 && f.pixels[10*64+10]==5 && f.pixels[11*64+20]==9 && f.s.bgi.color==5);
	for (size_t fail=0;fail<4;++fail) {
		init(&f); f.fail=fail; CHECK(draw(&f)==IO_ERROR_IO && f.calls==fail && f.r.counter==fail);
	}
	init(&f); f.fail=0; f.error=IO_ERROR_IN_PROGRESS; CHECK(draw(&f)==IO_ERROR_PROTOCOL && f.r.counter==0 && f.random==UINT32_C(0x8082a52f));
	init(&f); f.r.x=f.r.y=65535; f.r.width=f.r.height=0;
	CHECK(draw(&f)==0 && f.calls==0 && f.r.counter==4 && f.random==UINT32_C(0x190f3a19) && f.s.bgi.x==40);
	const struct st_big_draw_sink sink={&f,pixel};
	CHECK(st_big_draw_spray(NULL,&f.r,&f.random,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_spray(&f.s,NULL,&f.random,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_spray(&f.s,&f.r,NULL,&sink)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_draw_spray(&f.s,&f.r,&f.random,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { samples_and_signed_counts(); types_and_failures(); return 0; }
