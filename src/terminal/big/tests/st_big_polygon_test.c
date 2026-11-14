/* Independent original DDO0b03..0d67 expectations, BIG60/61. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_polygon.h"
#include "st_big_producer.h"
#include "st_big_fill.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct event { bool raw; uint16_t p[4]; };
struct fixture {
	struct st_big_state state; struct st_big_producer producer;
	struct st_big_producer_point records[128]; struct event spans[1024];
	uint16_t colors[4]; uint8_t pixels[64*64]; size_t calls,color_calls,fail_at,color_fail_at;
	int failure; bool render,mutate,hook,sticky,rebind;
};
static const struct st_big_producer_point square[]={{2,2},{6,2},{6,6},{2,6},{2,2},{123,0x8000}};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); memset(f->pixels,3,sizeof(f->pixels));
	st_big_state_init(&f->state); f->state.bgi.width=f->state.bgi.height=64;
	f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	CHECK(st_big_fill_builtin(&f->state,1,5)==0 && st_big_draw_color(&f->state,9)==0);
	f->state.bgi.x=11; f->state.bgi.y=12; f->state.bgi.ddo_x=13; f->state.bgi.ddo_y=14; f->state.bgi.device_x=15; f->state.bgi.device_y=16;
	f->state.bgi.error=-11; f->state.bgi.polygon_pending[0]=17; f->state.bgi.polygon_pending[1]=18; f->state.bgi.polygon_hook=0xabcd;
	CHECK(st_big_producer_bind(&f->producer,f->records,128,240)==0);
	f->producer.count=6; f->producer.mode=7; f->producer.run_length=19;
	for (size_t i=0;i<128;++i) f->records[i]=(struct st_big_producer_point){0xa5a5,0xa5a5};
	memcpy(f->records,square,sizeof(square)); f->failure=IO_ERROR_IO;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<=15);
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	return 0;
}
static int color(void *context, uint16_t tuple)
{
	struct fixture *f=context; CHECK(f->color_calls<4); f->colors[f->color_calls++]=tuple;
	return f->color_calls==f->color_fail_at?f->failure:0;
}
static int span(void *context, bool raw, const uint16_t p[4])
{
	struct fixture *f=context; CHECK(f->calls<1024); struct event *e=&f->spans[f->calls++]; e->raw=raw; memcpy(e->p,p,sizeof(e->p));
	if (f->calls==f->fail_at) return f->failure;
	if (f->mutate) { f->state.bgi.color=3; f->state.bgi.fill_low=7; f->state.bgi.fill_color=0x1234; }
	if (f->hook) f->state.bgi.polygon_hook=1;
	if (f->sticky) f->state.bgi.curve_fallback=255;
	if (f->rebind) f->producer.points=NULL;
	if (f->render) {
		const struct st_big_draw_sink sink={f,pixel}; return st_big_draw_polygon_span(&f->state,raw,p,&sink);
	}
	return 0;
}
static int fill(struct fixture *f)
{
	const struct st_big_polygon_sink sink={f,color,span}; return st_big_polygon_fill(&f->state,&f->producer,&sink);
}
static void event(const struct fixture *f, size_t i, uint16_t x1, uint16_t y, uint16_t x2, bool raw)
{
	CHECK(i<f->calls && f->spans[i].raw==raw && f->spans[i].p[0]==x1 && f->spans[i].p[1]==y && f->spans[i].p[2]==x2 && f->spans[i].p[3]==y);
}
static void owners(const struct fixture *f)
{
	CHECK(f->state.bgi.x==11 && f->state.bgi.y==12 && f->state.bgi.ddo_x==13 && f->state.bgi.ddo_y==14 && f->state.bgi.device_x==15 && f->state.bgi.device_y==16);
	CHECK(f->producer.mode==7 && f->producer.run_length==19 && f->state.bgi.error==-11);
}
static void square_and_color(void)
{
	struct fixture f; init(&f); f.render=true; CHECK(fill(&f)==0 && f.calls==4 && f.color_calls==2);
	for (size_t i=0;i<4;++i) event(&f,i,2,(uint16_t)(2+i),6,false);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(x>=2 && x<=6 && y>=2 && y<6?5:3));
	CHECK(f.colors[0]==0x0505 && f.colors[1]==0x0509 && f.state.bgi.color==9 && f.state.bgi.fill_low==5 && f.state.bgi.device_color==9);
	CHECK(f.state.bgi.polygon_pending[0]==6 && f.state.bgi.polygon_pending[1]==5 && f.state.bgi.polygon_hook==0);
	CHECK(f.records[6].x==2 && f.records[6].y==6 && f.records[7].x==0xa5a5 && f.records[7].y==0xa5a5 && f.producer.count==6); owners(&f);
	init(&f); f.mutate=true; CHECK(fill(&f)==0 && f.state.bgi.color==9 && f.state.bgi.fill_low==5 && f.state.bgi.fill_color==0x1234 && f.state.bgi.device_color==9 && f.state.bgi.device_fill_color==5); owners(&f);
	init(&f); f.state.bgi.viewport[0]=1; f.state.bgi.viewport[1]=1; f.render=true; CHECK(fill(&f)==0); event(&f,0,2,2,6,false); CHECK(f.pixels[2*64+2]==5 && f.pixels[6*64+2]==3); owners(&f);
	for (unsigned pen=0;pen<16;++pen) {
		init(&f); const uint8_t rows[]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa}; CHECK(st_big_fill_custom(&f.state,rows,(uint16_t)pen)==0); f.render=true;
		CHECK(fill(&f)==0 && f.calls==4);
		for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(x>=2 && x<=6 && y>=2 && y<6?(x%2==0?pen:0):3));
	}
	/* Sticky polygon spans bypass the viewport and DDO thickness. Their
	 * raw line restarts bit0 per row and uses the polygon's installed pen. */
	init(&f); f.render=true; f.state.bgi.curve_fallback=1; f.state.bgi.write_mode=1;
	f.state.bgi.viewport[0]=10; f.state.bgi.viewport[1]=12;
	CHECK(st_big_draw_style(&f.state,4,0xcccc,3)==0 && fill(&f)==0 && f.calls==4 && f.color_calls==2);
	for (size_t i=0;i<4;++i) event(&f,i,2,(uint16_t)(2+i),6,true);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(y>=2 && y<6 && (x==4 || x==5)?6:3));
	CHECK(f.state.bgi.device_color==9 && f.state.bgi.device_fill_color==5 && f.state.bgi.line_mask==0xcccc); owners(&f);
}
static void markers_capacity_and_aliases(void)
{
	struct fixture f; init(&f); f.producer.count=64; CHECK(fill(&f)==0 && f.calls==4 && f.records[64].x==2 && f.records[64].y==6); event(&f,3,2,5,6,false);
	/* Each first intersection replaces the later upper-left X with6;
	 * reloading that edge gives left X2/3/4/5 on rows2/3/4/5. */
	init(&f); f.records[3].x=10; f.producer.count=3; CHECK(fill(&f)==0 && f.calls==4 && f.records[3].x==5 && f.records[3].y==6); event(&f,3,5,5,6,false);
	for (unsigned low=0;low<256;++low) if (low!=1) {
		init(&f); f.records[5]=(struct st_big_producer_point){77,(uint16_t)(0x8000u+low)}; CHECK(fill(&f)==0 && f.calls==4);
	}
	init(&f); const struct st_big_producer_point contours[]={{2,2},{4,2},{4,4},{2,4},{2,2},{99,0x8001},{8,2},{10,2},{10,4},{8,4},{8,2},{88,0x8000}};
	memcpy(f.records,contours,sizeof(contours)); f.producer.count=12; CHECK(fill(&f)==0 && f.calls==4);
	event(&f,0,2,2,4,false); event(&f,1,8,2,10,false); event(&f,2,2,3,4,false); event(&f,3,8,3,10,false);
	init(&f); f.records[0].x=f.records[3].x=f.records[4].x=0x8000; CHECK(fill(&f)==0 && f.calls==4); event(&f,0,6,2,0x8000,false);
	init(&f); f.producer.capacity=0; CHECK(fill(&f)==0 && f.calls==0 && f.state.bgi.polygon_pending[0]==17 && f.state.bgi.error==-11);
	init(&f); f.producer.capacity=1; CHECK(fill(&f)==0 && f.calls==0 && f.state.bgi.polygon_pending[0]==6 && f.state.bgi.polygon_pending[1]==5 && f.state.bgi.error==-11);
	init(&f); const struct st_big_producer_point odd[]={{2,10},{2,12},{0,0x8001},{5,10},{5,12},{0,0x8001},{8,10},{8,12},{0,0x8000}};
	memcpy(f.records,odd,sizeof(odd)); f.producer.count=9; CHECK(fill(&f)==0 && f.calls==2); event(&f,0,2,10,5,false); event(&f,1,2,11,5,false);
	CHECK(f.state.bgi.polygon_pending[0]==8 && f.state.bgi.polygon_pending[1]==11 && f.records[9].x==2 && f.records[9].y==5 && f.records[10].x==8);
	init(&f); f.rebind=true; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==1 && f.color_calls==1 && f.state.bgi.device_color==5);
}
static void rows_and_hook(void)
{
	static const uint16_t r1[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19},r2[]={0,2,4,6,14,16,18},r3[]={0,10},r4[]={0,1,2,3,4,5,6,7,8,9,19},r5[]={0,5,15},r8[]={0,17},r9[]={0,1,2,3,4,9,10,11,12,13,18,19},r11[]={0,2,4,6,8,18};
	static const struct { const uint16_t *ys; size_t size; } expected[]={{r1,20},{r2,7},{r3,2},{r4,11},{r5,3},{r5,3},{r3,2},{r8,2},{r9,12},{r4,11},{r11,6},{r1,20}};
	for (unsigned style=1;style<=12;++style) for (unsigned fallback=0;fallback<2;++fallback) for (unsigned orientation=0;orientation<2;++orientation) {
		struct fixture f; init(&f); f.records[0].y=f.records[1].y=f.records[4].y=0; f.records[2].y=f.records[3].y=20;
		f.state.bgi.device_fill_style=(uint8_t)style; f.state.bgi.curve_fallback=(uint8_t)fallback; f.state.bgi.orientation=(uint8_t)orientation;
		const size_t index=fallback?style-1:0; CHECK(fill(&f)==0 && f.calls==expected[index].size);
		for (size_t i=0;i<f.calls;++i) event(&f,i,2,expected[index].ys[i],6,fallback!=0);
	}
	struct fixture f; init(&f); f.state.bgi.device_fill_style=0; f.state.bgi.curve_metric=180;
	CHECK(fill(&f)==0 && f.calls==0 && f.color_calls==1 && f.colors[0]==0x0509 && f.state.bgi.curve_fallback==255 && f.state.bgi.polygon_hook==0);
	init(&f); f.state.bgi.device_fill_style=129; CHECK(fill(&f)==0 && f.calls==0 && f.color_calls==1);
	init(&f); f.state.bgi.device_fill_style=128; CHECK(fill(&f)==0 && f.calls==4);
	init(&f); f.state.bgi.curve_fallback=1; f.state.bgi.device_fill_style=13; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.color_calls==0 && f.state.bgi.polygon_hook==0);
	/* BYTE(5*(53-1))==4, a valid misaligned five-byte table view. */
	init(&f); f.records[0].y=f.records[1].y=f.records[4].y=0; f.records[2].y=f.records[3].y=20; f.state.bgi.curve_fallback=1; f.state.bgi.device_fill_style=53;
	CHECK(fill(&f)==0 && f.calls==5); for (size_t i=0;i<5;++i) event(&f,i,2,(uint16_t)(i*4),6,true);
	init(&f); for (size_t i=0;i<5;++i) f.records[i].y=10; CHECK(fill(&f)==0 && f.calls==0 && f.state.bgi.polygon_pending[0]==17);
	init(&f); f.hook=true; f.records[0].x=f.records[3].x=f.records[4].x=0x0123; f.records[1].x=f.records[2].x=0x0127;
	CHECK(fill(&f)==0 && f.calls==4); event(&f,0,0x0123,2,0x0127,false); event(&f,1,0x0100,3,0x0100,false); CHECK(f.state.bgi.polygon_hook==1);
	init(&f); f.sticky=true; CHECK(fill(&f)==0 && f.calls==4); event(&f,0,2,2,6,false); event(&f,1,2,3,6,true);
	init(&f); for (size_t i=0;i<5;++i) f.records[i].y=32767; CHECK(fill(&f)==0 && f.calls==0 && f.color_calls==2);
	init(&f); for (size_t i=0;i<5;++i) f.records[i].y=10; f.state.bgi.line_distance=0; CHECK(fill(&f)==0 && f.calls==0);
	init(&f); f.state.bgi.line_distance=0; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==1 && f.color_calls==1);
	init(&f); f.state.bgi.curve_fallback=1; f.state.bgi.device_fill_style=2; f.state.bgi.line_distance=0; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==4 && f.color_calls==1);
	init(&f); f.state.bgi.line_distance=2; for (size_t i=0;i<5;++i) f.records[i].y=(i==2 || i==3)?32767:32766;
	CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==1 && f.color_calls==1); event(&f,0,2,32766,6,false);
}
static void arithmetic_and_failures(void)
{
	struct fixture f; init(&f); const struct st_big_producer_point triangle[]={{5,1},{0,4},{0,1},{5,1},{0,0x8000}};
	memcpy(f.records,triangle,sizeof(triangle)); f.producer.count=5; CHECK(fill(&f)==0 && f.calls==3);
	event(&f,0,0,1,5,false); event(&f,1,0,2,4,false); event(&f,2,0,3,2,false);
	init(&f); const struct st_big_producer_point fault[]={{0,0x8100},{32767,32767},{0,0x8100},{0,0x8000}};
	memcpy(f.records,fault,sizeof(fault)); f.producer.count=4;
	CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==258 && f.color_calls==1 && f.state.bgi.device_color==5);
	event(&f,0,0,0x8100,0,false); event(&f,257,0x8001,0x8201,0x8001,false);
	init(&f); f.producer.storage_count=5; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==0 && f.color_calls==1);
	init(&f); f.producer.storage_count=6; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.calls==0 && f.color_calls==1);
	init(&f); f.producer.count=2; f.state.bgi.curve_metric=180; CHECK(fill(&f)==IO_ERROR_OUT_OF_RANGE && f.color_calls==0 && f.state.bgi.polygon_hook==0xabcd && f.state.bgi.curve_fallback==255);
	init(&f); f.fail_at=2; f.render=true; CHECK(fill(&f)==IO_ERROR_IO && f.calls==2 && f.color_calls==1 && f.state.bgi.device_color==5 && f.state.bgi.polygon_pending[1]==3);
	CHECK(f.pixels[2*64+2]==5 && f.pixels[3*64+2]==3); owners(&f);
	init(&f); f.fail_at=1; f.failure=IO_ERROR_IN_PROGRESS; CHECK(fill(&f)==IO_ERROR_PROTOCOL && f.color_calls==1);
	init(&f); f.color_fail_at=1; CHECK(fill(&f)==IO_ERROR_IO && f.calls==0 && f.state.bgi.device_color==9);
	init(&f); f.color_fail_at=2; f.mutate=true; CHECK(fill(&f)==IO_ERROR_IO && f.calls==4 && f.state.bgi.color==9 && f.state.bgi.fill_low==5 && f.state.bgi.device_color==5);
	init(&f); f.state.bgi.fill_low=0xa5; CHECK(fill(&f)==0 && f.colors[0]==0xa5a5 && f.colors[1]==0xa509 && f.state.bgi.fill_low==0xa5 && f.state.bgi.fill_color==5);
	init(&f); f.state.bgi.device_fill_style=0; f.state.bgi.device_color=3; const struct st_big_polygon_sink native={.context=&f};
	CHECK(st_big_polygon_fill(&f.state,&f.producer,&native)==0 && f.state.bgi.device_color==9 && f.state.bgi.polygon_hook==0);
	init(&f); CHECK(st_big_polygon_fill(&f.state,&f.producer,&native)==IO_ERROR_INVALID_ARGUMENT && f.state.bgi.device_color==5 && f.state.bgi.polygon_pending[0]==6);
	const struct st_big_polygon_sink sink={&f,color,span}; CHECK(st_big_polygon_fill(NULL,&f.producer,&sink)==IO_ERROR_INVALID_ARGUMENT && st_big_polygon_fill(&f.state,NULL,&sink)==IO_ERROR_INVALID_ARGUMENT && st_big_polygon_fill(&f.state,&f.producer,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { square_and_color(); markers_capacity_and_aliases(); rows_and_hook(); arithmetic_and_failures(); return 0; }
