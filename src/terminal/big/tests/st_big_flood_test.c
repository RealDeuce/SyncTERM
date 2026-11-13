/* Independent span/traversal/pattern expectations, original BIG24/59. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_flood.h"
#include "st_big_draw.h"
#include "st_big_fill.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct event { uint16_t address; uint8_t mask, planes; };
struct fixture {
	struct st_big_state state;
	struct st_big_flood flood;
	struct st_big_flood_record records[32];
	uint8_t pixels[32*8];
	struct event events[4096];
	size_t writes, reads, fail_write, fail_read;
	int failure;
	unsigned mutate;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=32; f->state.bgi.height=8;
	f->state.bgi.viewport[2]=31; f->state.bgi.viewport[3]=7;
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=3; f->state.bgi.ddo_y=4;
	f->state.bgi.device_x=5; f->state.bgi.device_y=6;
	CHECK(st_big_fill_builtin(&f->state,1,5)==0);
	CHECK(st_big_flood_bind(&f->flood,f->records,32,32)==0);
	f->fail_read=f->fail_write=SIZE_MAX; f->failure=IO_ERROR_IO;
	memset(f->pixels,15,sizeof(f->pixels));
}
static int read_byte(void *context, uint16_t address, uint8_t pens[8])
{
	struct fixture *f=context;
	if (f->reads++==f->fail_read) return f->failure;
	if (address>=sizeof(f->pixels)/8) {
		if (f->mutate<5) return IO_ERROR_OUT_OF_RANGE;
		memset(pens,0,8); /* Remaining full-segment test backing is zero. */
	} else memcpy(pens,f->pixels+(size_t)address*8,8);
	if (f->mutate==3 && f->writes==5 && address==8) {
		f->pixels[2*32+2]=3; f->mutate=0;
	}
	if (f->mutate==4 && f->flood.count==2 && address==16) {
		f->pixels[4*32+4]=15; f->mutate=0;
	}
	if (f->mutate==5 && f->flood.count==1) {
		f->records[0].x=65535; f->records[0].bit=7; f->records[0].address=0; f->mutate=6;
	}
	if (f->mutate==7 && f->flood.count==1) { f->records[0].bit=8; f->mutate=0; }
	return 0;
}
static int write_byte(void *context, uint16_t address, uint8_t mask, uint8_t planes, const uint8_t pens[8], uint8_t mode)
{
	struct fixture *f=context;
	CHECK(mode==0);
	if (f->writes==f->fail_write) return f->failure;
	CHECK(address<sizeof(f->pixels)/8 && f->writes<4096 && planes>0 && planes<16);
	f->events[f->writes++]=(struct event){address,mask,planes};
	for (unsigned x=0;x<8;++x) if (mask&(0x80u>>x)) {
		CHECK(pens[x]<16);
		uint8_t *pixel=&f->pixels[(size_t)address*8+x];
		*pixel=(uint8_t)((*pixel&~planes)|(pens[x]&planes));
	}
	if (f->writes==1) {
		if (f->mutate==1) memset(f->state.bgi.fill_pixels,0,sizeof(f->state.bgi.fill_pixels));
		if (f->mutate==2) f->state.bgi.device_fill_color=7;
	}
	return 0;
}
static struct st_big_memory_sink sink(struct fixture *f) { return (struct st_big_memory_sink){f,read_byte,write_byte}; }
static int fill(struct fixture *f, uint16_t x, uint16_t y, uint16_t boundary)
{
	struct st_big_memory_sink io=sink(f);
	return st_big_flood_fill(&f->flood,&f->state,&io,x,y,boundary);
}
static void region(struct fixture *f)
{
	for (unsigned y=2;y<=4;++y) for (unsigned x=2;x<=6;++x) f->pixels[y*32+x]=3;
}
static void simple_and_order(void)
{
	{
		struct fixture f; init(&f); region(&f);
		f.state.bgi.palette[3]=f.state.bgi.palette[15]; struct st_big_state before=f.state;
		CHECK(fill(&f,4,4,0x010f)==0 && !f.flood.overflow);
		CHECK(memcmp(&before,&f.state,sizeof(before))==0 && f.writes==15);
		static const uint8_t bits[]={8,4,2,16,32};
		for (size_t i=0;i<5;++i) CHECK(f.events[i].address==16 && f.events[i].mask==bits[i] && f.events[i].planes==15);
		for (unsigned y=0;y<8;++y) for (unsigned x=0;x<32;++x)
			CHECK(f.pixels[y*32+x]==(x>=2 && x<=6 && y>=2 && y<=4?5:15));
	}
	struct fixture f; init(&f); region(&f);
	CHECK(fill(&f,4,3,15)==0);
	/* UP is queued before DOWN; LIFO processes row4 before row2. */
	CHECK(f.events[0].address==12 && f.events[5].address==16 && f.events[10].address==8);
	init(&f); region(&f); for (unsigned x=2;x<=6;++x) f.pixels[3*32+x]=5;
	CHECK(fill(&f,4,4,15)==0 && f.writes==5);
	for (unsigned x=2;x<=6;++x) CHECK(f.pixels[2*32+x]==3 && f.pixels[3*32+x]==5 && f.pixels[4*32+x]==5);
}
static void byte_passes(void)
{
	struct fixture f; init(&f);
	for (unsigned x=1;x<=30;++x) f.pixels[32+x]=3;
	CHECK(fill(&f,4,1,15)==0);
	CHECK(f.writes==22);
	for (unsigned i=0;i<4;++i) CHECK(f.events[i].address==4 && f.events[i].mask==(8u>>i) && f.events[i].planes==15);
	for (unsigned plane=0;plane<4;++plane) for (unsigned byte=0;byte<2;++byte) {
		const struct event *e=&f.events[4+plane*2+byte];
		CHECK(e->address==5+byte && e->mask==255 && e->planes==(1u<<plane));
	}
	CHECK(f.events[12].address==7 && f.events[12].mask==128 && f.events[12].planes==15);
	CHECK(f.events[19].address==4 && f.events[19].mask==16 && f.events[21].mask==64);
	for (unsigned x=0;x<32;++x) CHECK(f.pixels[32+x]==(x>0 && x<31?5:15));
	/* Each completed plane pass has a distinct non-final pen over old10. */
	static const uint8_t partial[]={11,9,13};
	for (size_t plane=0;plane<3;++plane) {
		init(&f); for (unsigned x=1;x<=30;++x) f.pixels[32+x]=10;
		f.fail_write=6+plane*2;
		CHECK(fill(&f,4,1,15)==IO_ERROR_IO && f.writes==f.fail_write);
		for (unsigned x=8;x<=23;++x) CHECK(f.pixels[32+x]==partial[plane]);
		CHECK(f.pixels[32+4]==5 && f.pixels[32+24]==10 && f.state.bgi.error==0);
	}
	/* Full pattern truth table through the byte shortcut, with opaque holes. */
	for (unsigned mask=0;mask<256;++mask) for (unsigned pen=0;pen<16;++pen) {
		init(&f); for (unsigned x=1;x<=30;++x) f.pixels[32+x]=10;
		uint8_t rows[8]; memset(rows,(int)mask,sizeof(rows));
		CHECK(st_big_fill_custom(&f.state,rows,(uint16_t)pen)==0);
		CHECK(fill(&f,4,1,15)==0);
		for (unsigned x=1;x<=30;++x) CHECK(f.pixels[32+x]==((mask&(0x80u>>(x%8)))?pen:0));
		CHECK(f.pixels[32]==15 && f.pixels[63]==15);
	}
}
static void edges_and_fresh_reads(void)
{
	struct fixture f; init(&f);
	f.state.bgi.viewport[1]=2; f.state.bgi.viewport[2]=7; f.state.bgi.viewport[3]=4;
	for (unsigned y=3;y<=4;++y) for (unsigned x=0;x<=7;++x) f.pixels[y*32+x]=3;
	f.pixels[2*32]=f.pixels[2*32+7]=3;
	CHECK(fill(&f,3,1,15)==0 && f.writes==16);
	CHECK(f.pixels[2*32]==3 && f.pixels[2*32+7]==3);
	for (unsigned y=3;y<=4;++y) for (unsigned x=0;x<=7;++x) CHECK(f.pixels[y*32+x]==5);
	/* Inverted1..0 adjacency still probes x1, so the next row is reached. */
	init(&f); f.state.bgi.viewport[1]=2; f.state.bgi.viewport[2]=1; f.state.bgi.viewport[3]=3;
	for (unsigned y=2;y<=3;++y) for (unsigned x=0;x<=1;++x) f.pixels[y*32+x]=3;
	CHECK(fill(&f,0,0,15)==0 && f.writes==4);
	CHECK(f.pixels[3*32]==5 && f.pixels[3*32+1]==5);
	/* A one-column raw scan is not clamped to the viewport or fabricated
	 * as empty: it eventually reaches the actual backend storage boundary. */
	init(&f); f.state.bgi.viewport[1]=2; f.state.bgi.viewport[2]=0; f.state.bgi.viewport[3]=3;
	f.pixels[2*32]=f.pixels[3*32]=3;
	CHECK(fill(&f,0,0,15)==IO_ERROR_OUT_OF_RANGE);
	CHECK(f.writes==1 && f.pixels[2*32]==5 && f.pixels[3*32]==3 && f.state.bgi.error==0);
	/* Boundary and expected-pen tests are separate fresh source reads. */
	init(&f); region(&f); for (unsigned x=2;x<=6;++x) { f.pixels[2*32+x]=5; f.pixels[4*32+x]=15; }
	f.mutate=3;
	CHECK(fill(&f,4,3,15)==0 && f.writes==10);
	CHECK(f.pixels[2*32+2]==5 && f.mutate==0);
	/* A pending seed that became a boundary still executes its left span. */
	init(&f); region(&f); f.pixels[4*32+2]=f.pixels[4*32+3]=5;
	f.mutate=4;
	CHECK(fill(&f,4,3,15)==0 && f.writes==12 && f.mutate==0);
	CHECK(f.events[5].address==16 && f.events[5].mask==16 && f.events[6].mask==32);
	CHECK(f.pixels[4*32+5]==3 && f.pixels[4*32+4]==15);
}
static void patterns_and_cache(void)
{
	struct fixture f; init(&f); region(&f);
	const uint8_t pattern[]={0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5};
	CHECK(st_big_fill_custom(&f.state,pattern,5)==0);
	f.state.bgi.write_mode=1; f.state.bgi.fill_low=7;
	CHECK(st_big_draw_color(&f.state,9)==0 && f.state.bgi.device_fill_color==7 && f.state.bgi.fill_pixels[2]==5);
	CHECK(fill(&f,4,4,15)==0);
	for (unsigned y=2;y<=4;++y) for (unsigned x=2;x<=6;++x)
		CHECK(f.pixels[y*32+x]==(x==2 || x==5?5:0));
	/* Constant solid style uses the current device pen, not that stale tile. */
	init(&f); region(&f); f.state.bgi.fill_low=7;
	CHECK(st_big_draw_color(&f.state,9)==0);
	CHECK(fill(&f,4,4,15)==0);
	CHECK(f.pixels[4*32+4]==7);
	/* Empty patterns/zero pens propagate through already-zero rows. */
	for (unsigned style=0;style<3;++style) {
		init(&f); region(&f); for (unsigned x=2;x<=6;++x) f.pixels[3*32+x]=0;
		if (style==0) CHECK(st_big_fill_builtin(&f.state,0,5)==0);
		if (style==1) CHECK(st_big_fill_builtin(&f.state,1,0)==0);
		if (style==2) CHECK(st_big_fill_custom(&f.state,(const uint8_t[8]){0},5)==0);
		CHECK(fill(&f,4,4,15)==0 && f.flood.found_directional);
		for (unsigned y=2;y<=4;++y) for (unsigned x=2;x<=6;++x) CHECK(f.pixels[y*32+x]==0);
	}
	/* Pattern row is captured once per span; later rows see cache changes. */
	init(&f); region(&f); CHECK(st_big_fill_custom(&f.state,pattern,5)==0); f.mutate=1;
	CHECK(fill(&f,4,4,15)==0);
	CHECK(f.pixels[4*32+2]==5 && f.pixels[4*32+5]==5 && f.pixels[3*32+2]==0 && f.pixels[2*32+2]==0);
	init(&f); region(&f); f.mutate=2;
	CHECK(fill(&f,4,4,15)==0);
	CHECK(f.pixels[4*32+4]==5 && f.pixels[2*32+4]==5);
	/* The row OR includes bits outside the span. A nonzero x7 mask
	 * suppresses propagation through already-correct zero pixels x2..6. */
	init(&f); region(&f); for (unsigned x=2;x<=6;++x) f.pixels[3*32+x]=0;
	CHECK(st_big_fill_custom(&f.state,(const uint8_t[8]){0,0,0,1,0,0,0,0},5)==0);
	CHECK(fill(&f,4,4,15)==0 && f.writes==5);
	CHECK(f.pixels[4*32+4]==0 && f.pixels[2*32+4]==3);
	/* Both axes use absolute phase after viewport origin translation. */
	init(&f); f.state.bgi.viewport[0]=8; f.state.bgi.viewport[1]=1;
	f.state.bgi.viewport[2]=15; f.state.bgi.viewport[3]=6;
	for (unsigned y=2;y<=4;++y) for (unsigned x=10;x<=14;++x) f.pixels[y*32+x]=3;
	CHECK(st_big_fill_custom(&f.state,(const uint8_t[8]){128,64,32,16,8,4,2,1},5)==0);
	CHECK(fill(&f,4,2,15)==0);
	for (unsigned y=2;y<=4;++y) for (unsigned x=10;x<=14;++x) CHECK(f.pixels[y*32+x]==(x%8==y?5:0));
}
static void overflow_and_errors(void)
{
	struct fixture f; init(&f); region(&f); f.flood.capacity=0;
	CHECK(fill(&f,4,3,15)==0);
	CHECK(f.flood.overflow && f.state.bgi.error==-7 && f.writes==5 && f.flood.count==0);
	CHECK(f.pixels[2*32+4]==3 && f.pixels[3*32+4]==5 && f.pixels[4*32+4]==3);
	f.flood.capacity=32;
	CHECK(fill(&f,4,4,15)==0 && !f.flood.overflow && f.state.bgi.error==-7 && f.writes==10);
	init(&f); region(&f); CHECK(st_big_fill_builtin(&f.state,0,5)==0); f.flood.capacity=0;
	CHECK(fill(&f,4,3,15)==0 && f.flood.found_directional);
	CHECK(f.flood.overflow && f.state.bgi.error==-7);
	init(&f); region(&f); f.flood.capacity=1;
	CHECK(fill(&f,4,3,15)==0);
	CHECK(f.flood.overflow && f.state.bgi.error==-7 && f.pixels[2*32+4]==5 && f.pixels[4*32+4]==3);
	/* After refusal, the accepted upper-left branch can still push and
	 * process row0. The refused upper-right/down branches remain old. */
	init(&f); f.flood.capacity=1;
	for (unsigned y=2;y<=4;++y) for (unsigned x=2;x<=10;++x) f.pixels[y*32+x]=3;
	for (unsigned x=2;x<=4;++x) f.pixels[x]=f.pixels[32+x]=3;
	for (unsigned x=8;x<=10;++x) f.pixels[32+x]=3;
	CHECK(fill(&f,6,2,15)==0 && f.writes==15 && f.flood.overflow && f.state.bgi.error==-7);
	CHECK(f.records[0].inverse_y==7 && f.flood.count==0);
	for (unsigned y=0;y<8;++y) for (unsigned x=0;x<32;++x) {
		unsigned pen=15;
		if ((y==0 || y==1) && x>=2 && x<=4) pen=5;
		if (y==1 && x>=8 && x<=10) pen=3;
		if (y>=2 && y<=4 && x>=2 && x<=10) pen=y==2?5:3;
		CHECK(f.pixels[y*32+x]==pen);
	}
	init(&f); region(&f); f.state.bgi.error=-11;
	CHECK(fill(&f,4,4,15)==0 && f.state.bgi.error==-11);
	init(&f); region(&f); f.fail_read=0;
	CHECK(fill(&f,4,4,15)==IO_ERROR_IO && f.writes==0);
	f.fail_read=SIZE_MAX; f.fail_write=2;
	CHECK(fill(&f,4,4,15)==IO_ERROR_IO && f.writes==2);
	CHECK(f.pixels[4*32+4]==5 && f.pixels[4*32+5]==5 && f.pixels[4*32+6]==3);
	init(&f); region(&f); f.failure=IO_ERROR_IN_PROGRESS; f.fail_write=0;
	CHECK(fill(&f,4,4,15)==IO_ERROR_PROTOCOL);
	init(&f); region(&f); f.failure=IO_ERROR_IN_PROGRESS; f.fail_read=0;
	CHECK(fill(&f,4,4,15)==IO_ERROR_PROTOCOL && f.writes==0);
	init(&f); region(&f); f.pixels[4*32+7]=16; f.state.bgi.error=-11;
	CHECK(fill(&f,4,4,15)==IO_ERROR_OUT_OF_RANGE && f.writes==0 && f.state.bgi.error==-11);
	/* Native failure before return does not publish an overflow BGI tail. */
	init(&f); region(&f); f.flood.capacity=1; f.fail_write=5; f.state.bgi.error=-11;
	CHECK(fill(&f,4,3,15)==IO_ERROR_IO && f.flood.overflow && f.state.bgi.error==-11);
	init(&f); region(&f); f.flood.storage_count=0;
	CHECK(fill(&f,4,3,15)==IO_ERROR_OUT_OF_RANGE && f.writes==5);
	CHECK(!f.flood.overflow && f.state.bgi.error==0);
	init(&f); region(&f); f.flood.records=NULL;
	CHECK(fill(&f,4,3,15)==IO_ERROR_OUT_OF_RANGE && f.writes==5 && f.flood.count==0);
	init(&f); region(&f); f.mutate=7;
	CHECK(fill(&f,4,3,15)==IO_ERROR_OUT_OF_RANGE && f.writes==10 && f.pixels[2*32+4]==3);
	/* A malformed pending seed can make the right-byte WORD limit
	 * unreachable. Full-segment reads must stop on a repeated cursor. */
	init(&f); memset(f.pixels,0,sizeof(f.pixels)); f.mutate=5;
	f.state.bgi.viewport[1]=2; f.state.bgi.viewport[2]=0; f.state.bgi.viewport[3]=3;
	CHECK(fill(&f,0,0,15)==IO_ERROR_OUT_OF_RANGE && f.writes==2 && f.mutate==6);
	CHECK(f.reads>65536 && f.state.bgi.error==0 && f.pixels[2*32]==5 && f.pixels[7]==5);
}
static void clipping_and_interrupt(void)
{
	struct fixture f; init(&f); region(&f); struct st_big_memory_sink io=sink(&f);
	CHECK(fill(&f,65535,4,15)==0 && f.reads==0 && f.writes==0);
	CHECK(fill(&f,1,1,0x010f)==0 && f.reads==1 && f.writes==0);
	/* Origin translation remains enabled when clipping is disabled. */
	f.state.bgi.viewport[0]=65535; f.state.bgi.viewport[1]=0; f.state.bgi.clip=0;
	CHECK(fill(&f,3,4,15)==0 && f.pixels[4*32+2]==5);
	/* A reset/error is a synchronous callback result, with no yield API. */
	init(&f); region(&f); f.fail_write=2; f.failure=IO_ERROR_INTERRUPTED;
	CHECK(fill(&f,4,4,15)==IO_ERROR_INTERRUPTED && f.writes==2);
	CHECK(f.pixels[4*32+4]==5 && f.pixels[4*32+5]==5 && f.pixels[4*32+6]==3);
	f.fail_write=SIZE_MAX;
	CHECK(fill(&f,4,4,15)==0 && f.pixels[4*32+6]==5);
	init(&f); region(&f); f.fail_write=5; f.failure=IO_ERROR_INTERRUPTED;
	CHECK(fill(&f,4,3,15)==IO_ERROR_INTERRUPTED && f.writes==5 && f.flood.count==1);
	init(&f); f.state.bgi.state=0;
	CHECK(fill(&f,0,0,15)==IO_ERROR_PROTOCOL && f.state.bgi.error==-1);
	CHECK(st_big_flood_bind(NULL,NULL,0,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_flood_fill(NULL,&f.state,&io,0,0,15)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_flood_fill(&f.flood,NULL,&io,0,0,15)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_flood_fill(&f.flood,&f.state,NULL,0,0,15)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { simple_and_order(); byte_passes(); edges_and_fresh_reads(); patterns_and_cache(); overflow_and_errors(); clipping_and_interrupt(); return 0; }
