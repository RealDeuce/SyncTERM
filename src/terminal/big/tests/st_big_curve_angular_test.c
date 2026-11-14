/* Independent DDO angular collector/flush/style/three-point expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_curve.h"
#include "st_big_producer.h"
#include "io_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_state state; struct st_big_producer producer;
	struct st_big_producer_point records[1024];
	uint8_t pixels[64*64]; uint16_t points[32][2];
	size_t writes,attempts,fail_at; int failure; bool rebind,restyle;
};
static void init(struct fixture *f, uint16_t width)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64; f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.bgi.x=1; f->state.bgi.y=2;
	f->state.bgi.ddo_x=f->state.bgi.device_x=10; f->state.bgi.ddo_y=f->state.bgi.device_y=10;
	CHECK(st_big_draw_color(&f->state,5)==0 && st_big_draw_style(&f->state,0,0,width)==0);
	f->state.bgi.write_mode=1; f->state.bgi.error=-11;
	for (size_t i=0;i<4;++i) f->state.bgi.curve_offsets[i]=(uint16_t)((i+1)*10);
	CHECK(st_big_producer_bind(&f->producer,f->records,1024,6144)==0);
	f->producer.video_segment=0xa000; f->failure=IO_ERROR_IO;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen==5 && f->attempts<4096);
	if (f->attempts<32) { f->points[f->attempts][0]=x; f->points[f->attempts][1]=y; }
	++f->attempts;
	if (f->attempts==f->fail_at) return f->failure;
	++f->writes;
	if (f->writes==1 && f->rebind) { f->producer.points=NULL; f->producer.storage_count=0; f->producer.count=0; f->producer.capacity=0; }
	if (f->writes==1 && f->restyle) f->state.bgi.ddo_line_style=2;
	if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen; return 0;
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	struct fixture *f=context; CHECK(x%8==0 && x<64 && y<64 && plane==0); *byte=0;
	for (unsigned bit=0;bit<8;++bit) if ((f->pixels[y*64+x+bit]&1)!=0) *byte|=(uint8_t)(128u>>bit);
	return 0;
}
static struct st_big_producer_sink sink(struct fixture *f)
{
	return (struct st_big_producer_sink){.drawing={f,pixel},.read_context=f,.read_byte=read_byte};
}
static int draw(struct fixture *f, uint16_t start, uint16_t end, uint16_t rx, uint16_t ry)
{
	const struct st_big_producer_sink io=sink(f);
	return st_big_curve_outline(&f->state,&f->producer,start,end,rx,ry,&io,NULL);
}
static void rectangle_scene(struct fixture *f, unsigned left, unsigned right, unsigned top, unsigned bottom)
{
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x)
		CHECK(f->pixels[y*64+x]==(x>=left && x<=right && y>=top && y<=bottom?5:0));
}
static void record(struct fixture *f, size_t i, uint16_t x, uint16_t y)
{
	CHECK(f->records[i].x==x && f->records[i].y==y);
}
static void retained(struct fixture *f)
{
	CHECK(f->state.bgi.x==1 && f->state.bgi.y==2 && f->state.bgi.color==5 && f->state.bgi.device_color==5);
	CHECK(f->state.bgi.write_mode==1 && f->state.bgi.curve_first_valid==0);
	CHECK(f->state.bgi.ddo_x==10 && f->state.bgi.ddo_y==10 && f->producer.mode==0);
}
static void duplicate_strokes_and_restoration(void)
{
	struct fixture f; init(&f,3); f.producer.batch=true;
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==12 && f.producer.count==5 && f.producer.run_length==5);
	rectangle_scene(&f,10,11,9,11); retained(&f);
	record(&f,0,11,10); record(&f,1,10,10); record(&f,2,10,10); record(&f,3,10,10); record(&f,4,0x8000,0x8000);
	CHECK(f.producer.batch && f.state.bgi.device_x==11 && f.state.bgi.device_y==10 && f.state.bgi.device_line_width==1);
	CHECK(f.state.bgi.ddo_line_width==3 && f.state.bgi.line_width==3 && f.state.bgi.line_mask==65535 && f.state.bgi.error==-11);
	/* Angular sweep does not calculate fresh0314..031b offsets. */
	static const uint16_t arc[]={10,10,20,30,40,50};
	for (size_t i=0;i<6;++i) CHECK(f.state.bgi.arc[i]==arc[i]);
	init(&f,3); CHECK(st_big_draw_style(&f.state,4,0,3)==0);
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==12); rectangle_scene(&f,10,11,9,11); retained(&f);
	CHECK(f.state.bgi.line_style==4 && f.state.bgi.line_user==0 && f.state.bgi.ddo_line_style==4 && f.state.bgi.ddo_line_user==0);
	CHECK(f.state.bgi.line_mask==0x8000 && f.state.bgi.device_line_width==1 && f.state.bgi.ddo_line_width==3);
}
static void current_style_restore(void)
{
	struct fixture f; init(&f,3); f.restyle=true;
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==12 && f.state.bgi.line_mask==0xfc78 && f.state.bgi.ddo_line_style==2 && f.state.bgi.line_style==0);
	rectangle_scene(&f,10,11,9,11); retained(&f);
}
static void copy_strokes(void)
{
	struct fixture f; init(&f,3); f.state.bgi.write_mode=0;
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==12 && f.state.bgi.write_mode==0); rectangle_scene(&f,10,11,9,11);
}
static void closed_turn_strokes_and_markers(void)
{
	struct fixture f; init(&f,3);
	CHECK(draw(&f,0,360,1,1)==0 && f.writes==1098 && f.producer.count==363 && f.producer.run_length==1);
	/* Unit trig has six transition segments (6 pixels each), plus354
	 * degenerate segments (3 each):36+1062=1098 ordered XOR writes.
	 * Odd repeats at left(9,10) and bottom(10,11), plus the six explicit
	 * transitions, give these hand-XORed rows. */
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		bool lit=(y==8 && x==10) || (y==9 && x>=9 && x<=11) || (y==10 && x>=8 && x<=11)
		    || (y==11 && (x==8 || x==10)) || (y==12 && (x==9 || x==10));
		CHECK(f.pixels[y*64+x]==(lit?5:0));
	}
	record(&f,0,11,10); record(&f,1,10,10); record(&f,89,10,10); record(&f,90,10,9);
	record(&f,91,9,10); record(&f,180,9,10); record(&f,181,9,11); record(&f,269,9,11);
	record(&f,270,10,11); record(&f,359,10,11); record(&f,360,11,10);
	record(&f,361,0x8001,0x8001); record(&f,362,0x8000,0x8000);
	CHECK(f.state.bgi.device_x==0x8000 && f.state.bgi.device_y==0x8000 && f.state.bgi.error==-11); retained(&f);
}
static void selection_and_word_loops(void)
{
	struct fixture f; init(&f,1); f.state.bgi.curve_metric=179;
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==2 && f.producer.count==0); rectangle_scene(&f,11,11,10,10);
	init(&f,2); CHECK(draw(&f,0,3,1,1)==0 && f.writes==2); rectangle_scene(&f,11,11,10,10);
	init(&f,1); f.state.bgi.curve_metric=180;
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==4 && f.producer.count==5); rectangle_scene(&f,10,11,10,10); retained(&f);
	for (unsigned sticky=1;sticky<256;++sticky) {
		init(&f,1); f.state.bgi.curve_fallback=(uint8_t)sticky;
		CHECK(draw(&f,0,3,1,1)==0 && f.writes==4 && f.state.bgi.curve_fallback==sticky); rectangle_scene(&f,10,11,10,10);
	}
	/* start0/endffff enters its body once, then signed1<=-1 fails. */
	init(&f,3); CHECK(draw(&f,0,65535,1,1)==0 && f.writes==0 && f.producer.count==2 && f.producer.run_length==2);
	record(&f,0,11,10); record(&f,1,0x8000,0x8000); retained(&f);
	/* Word orientation+90 makes7fff negative, so this also enters once. */
	init(&f,3); f.state.bgi.orientation=1;
	CHECK(draw(&f,0,32767,1,1)==0 && f.writes==0 && f.producer.count==2); record(&f,0,10,9);
	/* Equal angles take the inclusive360 sweep. Zero radii keep every
	 * candidate at center; first-run duplicates are suppressed. */
	init(&f,3); CHECK(draw(&f,0,0,0,0)==0 && f.writes==0 && f.producer.count==2 && f.producer.run_length==2); record(&f,0,10,10); record(&f,1,0x8000,0x8000);
	/* Signed end7fff cannot terminate. Reject after a complete WORD turn,
	 * preserving collector/style/error prefixes instead of hanging. */
	init(&f,3); f.producer.capacity=0;
	CHECK(draw(&f,0,32767,1,1)==IO_ERROR_OUT_OF_RANGE && f.writes==0 && f.state.bgi.error==-6 && f.producer.mode==1 && f.state.bgi.line_mask==65535);
	CHECK(f.state.bgi.arc[0]==10 && f.state.bgi.arc[1]==10 && f.state.bgi.arc[2]==0);
}
static void capacity_and_error_prefixes(void)
{
	struct fixture f;
	for (unsigned capacity=0;capacity<=5;++capacity) {
		init(&f,3); f.producer.capacity=(uint16_t)capacity;
		CHECK(draw(&f,0,3,1,1)==0 && f.writes==0 && f.producer.count==capacity && f.state.bgi.error==-6 && f.producer.mode==0);
		CHECK(f.state.bgi.device_x==10 && f.state.bgi.device_y==10);
	}
	init(&f,3); f.producer.capacity=6;
	CHECK(draw(&f,0,3,1,1)==0 && f.writes==12 && f.state.bgi.error==-11); rectangle_scene(&f,10,11,9,11);
	for (size_t fail=1;fail<=12;++fail) {
		init(&f,3); f.fail_at=fail; CHECK(st_big_draw_style(&f.state,4,0,3)==0);
		CHECK(draw(&f,90,93,1,1)==IO_ERROR_IO && f.writes==fail-1 && f.attempts==fail && f.producer.mode==0 && f.producer.count==5);
		CHECK(f.state.bgi.ddo_x==9 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==10 && f.state.bgi.device_y==9 && f.state.bgi.line_mask==65535);
		CHECK(f.state.bgi.arc[2]==20 && f.state.bgi.arc[3]==30); /* Published before flush. */
	}
	init(&f,3); f.fail_at=1; f.failure=IO_ERROR_IN_PROGRESS;
	CHECK(draw(&f,90,93,1,1)==IO_ERROR_PROTOCOL && f.attempts==1 && f.writes==0 && f.state.bgi.ddo_x==9);
	init(&f,3); f.producer.storage_count=1;
	CHECK(draw(&f,0,3,1,1)==IO_ERROR_OUT_OF_RANGE && f.writes==0 && f.producer.count==1 && f.producer.mode==1);
	init(&f,3); f.state.bgi.state=0;
	CHECK(draw(&f,0,3,1,1)==IO_ERROR_PROTOCOL && f.producer.mode==1 && f.producer.count==0 && f.state.bgi.error==-1);
}
static int flush(struct fixture *f, uint16_t *last)
{
	const struct st_big_producer_sink io=sink(f);
	return st_big_producer_flush(&f->state,&f->producer,&io,last);
}
static void invalid_flush_arguments(void)
{
	struct fixture f; init(&f,1); struct st_big_producer_sink io=sink(&f); uint16_t last=0x1234;
	f.producer.mode=1;
	CHECK(st_big_producer_flush(NULL,&f.producer,&io,&last)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_flush(&f.state,NULL,&io,&last)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_flush(&f.state,&f.producer,NULL,&last)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_producer_flush(&f.state,&f.producer,&io,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(f.producer.mode==1 && last==0x1234);
	f.records[0]=(struct st_big_producer_point){11,12}; f.records[1]=(struct st_big_producer_point){12,12}; f.producer.count=2; io.drawing.pixel=NULL;
	CHECK(st_big_producer_flush(&f.state,&f.producer,&io,&last)==IO_ERROR_INVALID_ARGUMENT && f.producer.mode==0 && f.state.bgi.ddo_x==11 && f.state.bgi.ddo_y==12);
}
static void flush_markers_short_counts_and_extents(void)
{
	struct fixture f; init(&f,1);
	static const struct st_big_producer_point points[]={{10,10},{12,10},{0x8001,0x80ff},{20,20},{22,20},{0x8000,0x8000}};
	memcpy(f.records,points,sizeof(points)); f.producer.count=6; uint16_t last=0x1234;
	CHECK(flush(&f,&last)==0 && f.writes==6 && last==0x8000 && f.state.bgi.device_x==20 && f.state.bgi.device_y==20); retained(&f);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(((y==10 && x>=10 && x<=12)||(y==20 && x>=20 && x<=22))?5:0));
	init(&f,1); memcpy(f.records,points,sizeof(points)); f.producer.count=6; f.rebind=true;
	CHECK(flush(&f,&last)==0 && f.writes==6 && last==0x8000 && f.producer.points==NULL && f.producer.count==0 && f.state.bgi.device_x==20); retained(&f);
	init(&f,1); f.producer.count=0; last=0x1234; CHECK(flush(&f,&last)==0 && last==0x1234 && f.writes==0);
	f.producer.count=1; CHECK(flush(&f,&last)==0 && last==0x1234 && f.state.bgi.device_x==10);
	f.producer.capacity=1; CHECK(flush(&f,&last)==0 && last==0x1234 && f.state.bgi.error==-6);
	init(&f,1); f.records[0]=(struct st_big_producer_point){10,10}; f.records[1]=(struct st_big_producer_point){12,10}; f.producer.count=2;
	last=0x1234; CHECK(flush(&f,&last)==0 && last==10 && f.writes==3); rectangle_scene(&f,10,12,10,10); retained(&f);
	/* A final nonzero marker performs the body-first empty fetch/move. */
	init(&f,1); f.records[0]=(struct st_big_producer_point){11,12}; f.records[1]=(struct st_big_producer_point){0x8001,0x8001}; f.producer.count=2;
	CHECK(flush(&f,&last)==0 && f.writes==0 && last==0x8000 && f.state.bgi.device_x==0x8001 && f.state.bgi.device_y==0x8000); retained(&f);
	/* First record is always a Move, even if it is a terminator. */
	init(&f,1); f.records[0]=f.records[1]=(struct st_big_producer_point){0x8000,0x8000}; f.producer.count=2;
	CHECK(flush(&f,&last)==0 && f.writes==0 && f.state.bgi.device_x==0x8000 && f.state.bgi.device_y==0x8000); retained(&f);
	init(&f,1); f.records[0]=(struct st_big_producer_point){11,12}; f.producer.count=3; f.producer.storage_count=1;
	CHECK(flush(&f,&last)==IO_ERROR_OUT_OF_RANGE && last==12 && f.state.bgi.ddo_x==11 && f.state.bgi.ddo_y==12 && f.state.bgi.device_x==11 && f.producer.mode==0 && f.writes==0);
}
int main(void) { duplicate_strokes_and_restoration(); current_style_restore(); copy_strokes(); closed_turn_strokes_and_markers(); selection_and_word_loops(); capacity_and_error_prefixes(); invalid_flush_arguments(); flush_markers_short_counts_and_extents(); return 0; }
