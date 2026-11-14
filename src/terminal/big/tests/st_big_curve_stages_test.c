/* Independent DDO0541..059e/09af..09e8 stage/polygon composition, BIG13/60/61. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_curve.h"
#include "st_big_polygon.h"
#include "st_big_producer.h"
#include "st_big_fill.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_state state; struct st_big_producer producer;
	struct st_big_producer_point records[128]; uint8_t pixels[64*64];
	char order[1024]; size_t events,colors,spans,lines;
	bool in_span,fail_span,fail_line,change_mode,change_cap,clear_count,flip;
	uint16_t returned_count; bool change_returned_count;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); memset(f->pixels,3,sizeof(f->pixels)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64; f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	CHECK(st_big_fill_builtin(&f->state,1,5)==0 && st_big_draw_color(&f->state,9)==0);
	f->state.bgi.x=11; f->state.bgi.y=12; f->state.bgi.ddo_x=13; f->state.bgi.ddo_y=14; f->state.bgi.device_x=15; f->state.bgi.device_y=16; f->state.bgi.error=-11;
	CHECK(st_big_producer_bind(&f->producer,f->records,128,240)==0);
	const struct st_big_producer_point points[]={{2,2},{6,2},{6,6},{2,6},{2,2}};
	memcpy(f->records,points,sizeof(points)); f->producer.count=5; f->producer.run_length=5; f->producer.first_x=2; f->producer.first_y=2; f->producer.mode=1;
}
static void log_event(struct fixture *f, char c) { CHECK(f->events<sizeof(f->order)); f->order[f->events++]=c; }
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64);
	if (!f->in_span) {
		log_event(f,'L'); ++f->lines;
		if (f->change_cap) f->producer.capacity=6;
		if (f->clear_count) f->producer.count=0;
		if (f->flip) f->state.bgi.curve_fallback=0;
		if (f->fail_line) return IO_ERROR_IO;
	}
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen;
	return 0;
}
static int color(void *context, uint16_t tuple)
{
	struct fixture *f=context; CHECK(f->colors!=0 || f->producer.mode==0); ++f->colors;
	if (f->state.bgi.fill_low==0) { CHECK(tuple==0x0009); log_event(f,'R'); }
	else { CHECK(tuple==(f->colors==1?0x0505:0x0509)); log_event(f,f->colors==1?'C':'R'); }
	if (f->colors==2 && f->change_returned_count) f->producer.count=f->returned_count;
	return 0;
}
static int span(void *context, bool raw, const uint16_t points[4])
{
	struct fixture *f=context; log_event(f,raw?'r':'F'); ++f->spans;
	if (f->change_mode) f->producer.mode=7;
	if (f->fail_span) return IO_ERROR_IO;
	CHECK(points[1]==points[3] && points[0]<=points[2]);
	const struct st_big_draw_sink sink={f,pixel}; f->in_span=true;
	int status=st_big_draw_polygon_span(&f->state,raw,points,&sink);
	f->in_span=false; return status;
}
static int stage(struct fixture *f, unsigned operation)
{
	const struct st_big_producer_sink sink={.drawing={f,pixel}};
	const struct st_big_polygon_sink polygon={f,color,span};
	if (operation==3) return st_big_curve_polygon_stage(&f->state,&f->producer,&sink,&polygon);
	if (operation==2) return st_big_curve_sector_stage(&f->state,&f->producer,&sink,&polygon);
	return st_big_curve_close_piece(&f->state,&f->producer,&sink,&polygon);
}
static void canvas(const struct fixture *f, unsigned kind)
{
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		uint8_t pen=3;
		if (kind==1 && x>=2 && x<=6 && y>=2 && y<6) pen=5;
		if (kind==2 || kind==3 || kind==4) {
			if (x>=2 && x<=6 && y>=2 && y<6 && kind!=4) pen=5;
			if ((x>=2 && x<=6 && (y==2 || y==6)) || (y>=2 && y<=6 && (x==2 || x==6))) pen=9;
			if (kind==3 && x>=2 && x<=6 && y>=2 && y<6) pen=5;
		}
		if (kind==5 && y==2 && x>=2 && x<=6) pen=9;
		if ((kind==6 || kind==7) && y>=2 && y<6 && x>=y && x<=6) pen=5;
		if (kind==6 && x==6 && y>=2 && y<=6) pen=9;
		CHECK(f->pixels[y*64+x]==pen);
	}
}
static void square_order_and_current_state(void)
{
	struct fixture f; init(&f); CHECK(stage(&f,3)==0 && f.colors==2 && f.spans==4 && f.lines==0 && f.producer.mode==0 && f.producer.count==6); canvas(&f,1);
	CHECK(f.records[5].x==0x8000 && f.records[5].y==0x8000 && f.records[6].x==2 && f.records[6].y==6);
	CHECK(f.state.bgi.ddo_x==13 && f.state.bgi.ddo_y==14 && f.state.bgi.device_x==15 && f.state.bgi.device_y==16);
	init(&f); f.change_mode=true; CHECK(stage(&f,3)==0 && f.producer.mode==0 && f.colors==2); canvas(&f,1);
	init(&f); CHECK(stage(&f,2)==0 && f.events==26 && f.colors==2 && f.spans==4 && f.lines==20); canvas(&f,2);
	CHECK(memcmp(f.order,"CFFFFR",6)==0 && f.order[6]=='L' && f.producer.count==6 && f.producer.mode==0);
	CHECK(f.state.bgi.ddo_x==13 && f.state.bgi.ddo_y==14 && f.state.bgi.device_x==2 && f.state.bgi.device_y==2 && f.state.bgi.x==11 && f.state.bgi.y==12);
	init(&f); f.state.bgi.curve_fallback=1; CHECK(stage(&f,2)==0 && f.events==26 && f.colors==2 && f.spans==4 && f.lines==20); canvas(&f,3);
	CHECK(f.order[0]=='L' && memcmp(f.order+20,"CrrrrR",6)==0 && f.producer.count==6 && f.producer.mode==0);
	init(&f); f.state.bgi.curve_metric=180; CHECK(stage(&f,2)==0 && f.state.bgi.curve_fallback==255 && f.order[0]=='L' && f.order[20]=='C'); canvas(&f,3);
	for (unsigned sticky=0;sticky<2;++sticky) {
		init(&f); CHECK(st_big_fill_builtin(&f.state,0,0)==0); f.state.bgi.curve_fallback=(uint8_t)sticky;
		CHECK(stage(&f,2)==0 && f.colors==1 && f.spans==0 && f.lines==20 && f.order[sticky?20:0]=='R' && f.producer.count==6 && f.producer.mode==0);
		CHECK(f.state.bgi.fill_style==0 && f.state.bgi.fill_low==0 && f.state.bgi.polygon_hook==0); canvas(&f,4);
	}
	init(&f); f.state.bgi.curve_fallback=1; f.change_cap=true; CHECK(stage(&f,2)==0 && f.colors==0 && f.producer.capacity==6 && f.state.bgi.error==-11); canvas(&f,4);
	init(&f); f.state.bgi.curve_fallback=1; f.clear_count=true; CHECK(stage(&f,2)==0 && f.colors==0 && f.producer.count==0 && f.state.bgi.ddo_x==13 && f.state.bgi.ddo_y==14); canvas(&f,4);
	init(&f); f.state.bgi.curve_fallback=1; f.flip=true; CHECK(stage(&f,2)==0 && memcmp(f.order+20,"CFFFFR",6)==0 && f.producer.count==6); canvas(&f,3);
}
static void admission_and_termination(void)
{
	struct fixture f; init(&f); f.producer.capacity=6; CHECK(stage(&f,3)==0 && f.colors==0 && f.producer.count==6 && f.state.bgi.error==-11 && f.producer.mode==0); canvas(&f,0);
	init(&f); f.producer.capacity=6; CHECK(stage(&f,2)==0 && f.colors==0 && f.state.bgi.error==-6 && f.producer.count==6); canvas(&f,0);
	init(&f); f.producer.count=6; f.producer.capacity=6; CHECK(stage(&f,3)==0 && f.state.bgi.error==-6 && f.producer.run_length==6 && f.colors==0);
	init(&f); f.producer.count=2; f.producer.run_length=2; CHECK(stage(&f,3)==0 && f.colors==0 && f.producer.count==3 && f.producer.mode==0); canvas(&f,0);
	init(&f); f.producer.count=2; f.producer.run_length=2; CHECK(stage(&f,2)==0 && f.colors==0 && f.producer.count==3); canvas(&f,5);
	init(&f); f.producer.count=0; f.producer.capacity=0; f.producer.run_length=0; CHECK(stage(&f,3)==0 && f.colors==0 && f.state.bgi.error==-6 && f.producer.run_length==1 && f.producer.mode==0);
	init(&f); f.producer.mode=0; f.producer.count=0; f.producer.capacity=0; f.producer.points=NULL;
	CHECK(stage(&f,3)==0 && f.colors==0 && f.state.bgi.error==-11 && f.state.bgi.ddo_x==0x8000 && f.state.bgi.device_x==0x8000);
	for (unsigned mode=0;mode<=2;mode+=2) {
		init(&f); f.producer.count=6; f.records[5]=(struct st_big_producer_point){0x8000,0x8000}; f.producer.mode=(uint8_t)mode; f.producer.batch=true; f.producer.read_plane=4;
		CHECK(stage(&f,3)==0 && f.producer.count==6 && f.producer.mode==0 && f.state.bgi.ddo_x==0x8000 && f.state.bgi.ddo_y==0x8000 && f.state.bgi.device_x==0x8000 && f.producer.batch); canvas(&f,1);
	}
	init(&f); f.producer.count=6; f.records[5]=(struct st_big_producer_point){0,0x8000}; f.producer.run_length=1; f.producer.first_x=f.producer.first_y=0x8000;
	CHECK(stage(&f,3)==0 && f.producer.count==6 && f.producer.run_length==1 && f.colors==2); canvas(&f,1);
}
static void close_piece_and_failure_prefixes(void)
{
	struct fixture f; init(&f); f.producer.count=2; f.producer.run_length=2; CHECK(stage(&f,9)==0 && f.colors==0 && f.producer.count==3 && f.producer.mode==0); canvas(&f,5);
	init(&f); f.state.bgi.curve_filled=255; f.state.bgi.ddo_x=f.state.bgi.ddo_y=2; f.records[0]=(struct st_big_producer_point){6,2}; f.records[1]=(struct st_big_producer_point){6,6}; f.producer.count=f.producer.run_length=2; f.producer.first_x=6; f.producer.first_y=2;
	CHECK(stage(&f,9)==0 && f.producer.count==2 && f.colors==2 && f.spans==4 && f.lines==5); canvas(&f,6);
	CHECK(f.records[2].x==2 && f.records[2].y==2 && f.records[3].x==6 && f.records[3].y==2 && f.records[4].y==0x8000);
	CHECK(f.state.bgi.ddo_x==2 && f.state.bgi.ddo_y==2 && f.state.bgi.device_x==6 && f.state.bgi.device_y==2 && f.state.bgi.x==11 && f.state.bgi.y==12);
	for (unsigned capacity=2;capacity<=3;++capacity) {
		init(&f); f.state.bgi.curve_filled=1; f.producer.count=f.producer.run_length=0; f.producer.capacity=(uint16_t)capacity;
		CHECK(stage(&f,9)==0 && f.producer.count==(capacity==2?2:0) && f.producer.mode==0 && f.colors==0 && f.lines==0 && f.state.bgi.error==(capacity==2?-6:-11)); canvas(&f,0);
	}
	for (unsigned returned=2;returned<=3;++returned) {
		init(&f); f.state.bgi.curve_filled=1; f.state.bgi.ddo_x=f.state.bgi.ddo_y=2; f.records[0]=(struct st_big_producer_point){6,2}; f.records[1]=(struct st_big_producer_point){6,6}; f.producer.count=f.producer.run_length=2; f.producer.first_x=6; f.producer.first_y=2; f.change_returned_count=true; f.returned_count=(uint16_t)returned;
		CHECK(stage(&f,9)==0 && f.producer.count==(returned==2?2:0) && f.lines==0 && f.colors==2); canvas(&f,7);
	}
	init(&f); f.state.bgi.curve_filled=1; f.producer.count=0; f.producer.run_length=0; f.producer.capacity=0; f.producer.points=NULL;
	CHECK(stage(&f,9)==IO_ERROR_OUT_OF_RANGE && f.producer.mode==1 && f.producer.run_length==1 && f.state.bgi.error==-6);
	init(&f); f.fail_span=true; f.change_mode=true; CHECK(stage(&f,3)==IO_ERROR_IO && f.colors==1 && f.spans==1 && f.producer.mode==7 && f.producer.count==6); canvas(&f,0);
	init(&f); f.fail_span=true; CHECK(stage(&f,2)==IO_ERROR_IO && f.lines==0 && f.colors==1); canvas(&f,0);
	init(&f); f.state.bgi.curve_fallback=1; f.fail_span=true; CHECK(stage(&f,2)==IO_ERROR_IO && f.lines==20 && f.colors==1); canvas(&f,4);
	init(&f); f.fail_line=true; CHECK(stage(&f,2)==IO_ERROR_IO && f.colors==2 && f.state.bgi.ddo_x==6 && f.state.bgi.ddo_y==2 && f.state.bgi.device_x==2); canvas(&f,1);
	init(&f); f.state.bgi.curve_fallback=1; f.fail_line=true; CHECK(stage(&f,2)==IO_ERROR_IO && f.colors==0 && f.state.bgi.ddo_x==6 && f.state.bgi.ddo_y==2); canvas(&f,0);
	init(&f); f.producer.storage_count=5; CHECK(stage(&f,3)==IO_ERROR_OUT_OF_RANGE && f.producer.mode==1 && f.producer.count==5 && f.producer.run_length==6 && f.colors==0);
	const struct st_big_producer_sink sink={.drawing={&f,pixel}}; const struct st_big_polygon_sink polygon={&f,color,span};
	CHECK(st_big_curve_polygon_stage(NULL,&f.producer,&sink,&polygon)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_sector_stage(&f.state,NULL,&sink,&polygon)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_close_piece(&f.state,&f.producer,NULL,&polygon)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { square_order_and_current_state(); admission_and_termination(); close_piece_and_failure_prefixes(); return 0; }
