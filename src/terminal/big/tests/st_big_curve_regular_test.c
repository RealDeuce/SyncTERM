/* Literal regular-outline/front/record expectations from original DDO bytes. */
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
	uint8_t pixels[64*64]; uint16_t points[32][2];
	size_t writes,reads,attempts,fail_write,begins,ends;
	int begin_status,end_status; bool mutate;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64;
	f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.bgi.x=1; f->state.bgi.y=2;
	f->state.bgi.ddo_x=f->state.bgi.device_x=10;
	f->state.bgi.ddo_y=f->state.bgi.device_y=10;
	f->state.bgi.color=5; f->state.bgi.device_color=7;
	f->state.bgi.write_mode=1; f->state.bgi.line_mask=0; f->state.bgi.error=-11;
	for (size_t i=0;i<6;++i) f->state.bgi.arc[i]=(uint16_t)(100+i);
	for (size_t i=0;i<4;++i) f->state.bgi.curve_offsets[i]=(uint16_t)((i+1)*10);
	f->state.bgi.curve_first[0]=50; f->state.bgi.curve_first[1]=60;
	f->state.bgi.curve_filled=255;
	f->producer.mode=1; f->producer.count=13; f->producer.run_length=17; f->producer.video_segment=0xa000;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(!xor_mode && x<64 && y<64 && pen<16 && f->attempts<32);
	f->points[f->attempts][0]=x; f->points[f->attempts++][1]=y;
	if (f->attempts==f->fail_write) return IO_ERROR_IO;
	++f->writes; f->pixels[y*64+x]=pen; return 0;
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	struct fixture *f=context; CHECK(x%8==0 && x<64 && y<64 && plane==0 && f->producer.batch);
	++f->reads; *byte=0;
	for (unsigned bit=0;bit<8;++bit) if ((f->pixels[y*64+x+bit]&1)!=0) *byte|=(uint8_t)(128u>>bit);
	return 0;
}
static int begin(void *context, uint16_t *color)
{
	struct fixture *f=context; ++f->begins; f->producer.batch=true; *color=6;
	if (f->mutate) {
		f->state.bgi.viewport[0]=3; f->state.bgi.viewport[1]=4;
		f->state.bgi.ddo_x=50; f->state.bgi.ddo_y=51;
		f->state.bgi.curve_radius[0]=6554; f->state.bgi.curve_radius[1]=7777;
		for (size_t i=0;i<4;++i) f->state.bgi.curve_offsets[i]=(uint16_t)(12+i);
	}
	return f->begin_status;
}
static int end(void *context)
{
	struct fixture *f=context; ++f->ends; f->producer.batch=false; return f->end_status;
}
static int draw(struct fixture *f, uint16_t start, uint16_t finish, uint16_t rx, uint16_t ry, bool custom)
{
	const struct st_big_producer_sink sink={.drawing={f,pixel},.read_context=f,.read_byte=read_byte};
	const struct st_big_curve_batch_sink batch={f,begin,end};
	return st_big_curve_regular_outline(&f->state,&f->producer,start,finish,rx,ry,&sink,custom?&batch:NULL);
}
static void words(const uint16_t *actual, const uint16_t *expected, size_t count)
{
	for (size_t i=0;i<count;++i) CHECK(actual[i]==expected[i]);
}
static void caches(struct fixture *f)
{
	CHECK(f->state.bgi.x==1 && f->state.bgi.y==2 && f->state.bgi.device_color==7);
	CHECK(f->state.bgi.write_mode==1 && f->state.bgi.line_mask==0 && f->state.bgi.error==-11);
	CHECK(f->producer.count==13 && f->producer.run_length==17 && f->state.bgi.curve_filled==0);
}
static void scene(struct fixture *f, bool full, unsigned x, unsigned y, uint8_t color)
{
	for (unsigned yy=0;yy<64;++yy) for (unsigned xx=0;xx<64;++xx) {
		bool lit=full?(xx==10 && (yy==9 || yy==11)) || (yy==10 && (xx==9 || xx==11)):xx==x && yy==y;
		CHECK(f->pixels[yy*64+xx]==(lit?color:0));
	}
}
static void zero_span_records_are_separate(void)
{
	static const uint16_t offsets[]={10,20,30,40},record[]={7,6,17,26,37,46},first[]={10,20};
	struct fixture f; init(&f); f.state.bgi.viewport[0]=3; f.state.bgi.viewport[1]=4; f.producer.batch=true;
	for (unsigned repeat=0;repeat<3;++repeat) {
		CHECK(draw(&f,10,10,5,6,true)==0 && f.writes==0 && f.reads==0 && f.begins==0 && f.ends==0);
		words(f.state.bgi.arc,record,6); words(f.state.bgi.curve_offsets,offsets,4); words(f.state.bgi.curve_first,first,2);
		CHECK(f.state.bgi.curve_first_valid==255 && f.state.bgi.curve_span==0 && f.state.bgi.curve_radius[0]==5 && f.state.bgi.curve_radius[1]==6);
		CHECK(f.producer.mode==0 && f.producer.batch && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==10 && f.state.bgi.device_y==10); caches(&f);
	}
	for (unsigned flag=1;flag<256;++flag) {
		init(&f); f.state.bgi.curve_first_valid=(uint8_t)flag;
		CHECK(draw(&f,0,0,0,0,false)==0 && f.state.bgi.curve_first_valid==flag && f.state.bgi.curve_first[0]==50 && f.state.bgi.curve_first[1]==60);
	}
	/* WORD additions/subtractions and retained nonzero first flag. */
	init(&f); f.state.bgi.ddo_x=65535; f.state.bgi.ddo_y=0; f.state.bgi.viewport[0]=1; f.state.bgi.viewport[1]=1;
	static const uint16_t wrapped[]={65534,65535,8,19,28,39};
	CHECK(draw(&f,65535,65535,1,1,false)==0); words(f.state.bgi.arc,wrapped,6);
	/* This lower selected front has no public installed/query gate. */
	init(&f); f.state.bgi.state=0; CHECK(draw(&f,1,1,1,1,false)==0); caches(&f);
}
static void shortcut_and_decimal_boundaries(void)
{
	static const uint16_t short_record[]={10,10,11,10,10,10},short_offsets[]={1,0,0,0};
	struct fixture f; init(&f);
	CHECK(draw(&f,0,1,1,1,true)==0 && f.writes==1 && f.reads==0 && f.begins==0 && f.ends==0 && !f.producer.batch && f.producer.mode==0);
	CHECK(f.points[0][0]==10 && f.points[0][1]==10 && f.state.bgi.ddo_x==0x0f02 && f.state.bgi.ddo_y==2 && f.state.bgi.device_x==0x0f02 && f.state.bgi.device_y==2);
	words(f.state.bgi.arc,short_record,6); words(f.state.bgi.curve_offsets,short_offsets,4); scene(&f,false,10,10,5); caches(&f);
	CHECK(f.state.bgi.curve_first_valid==0 && f.state.bgi.curve_first[0]==50 && f.state.bgi.curve_first[1]==60);
	init(&f); f.producer.batch=true;
	CHECK(draw(&f,0,1,1,1,true)==0 && f.writes==1 && f.reads==1 && f.producer.batch && f.begins==0 && f.ends==0 && f.state.bgi.ddo_x==0xa000 && f.state.bgi.ddo_y==2); caches(&f);
	init(&f); CHECK(draw(&f,120,150,1,1,true)==0 && f.writes==1 && f.begins==0); scene(&f,false,9,10,5);
	static const uint16_t coalesced[]={10,10,9,10,9,10}; words(f.state.bgi.arc,coalesced,6);
	init(&f); CHECK(draw(&f,0,2,1,1,false)==0 && f.writes==2 && f.reads==2 && !f.producer.batch); scene(&f,false,11,10,5);
	init(&f); CHECK(draw(&f,0,349,0,0,true)==0 && f.writes==1 && f.begins==0); scene(&f,false,10,10,5);
	init(&f); CHECK(draw(&f,0,350,0,0,false)==0 && f.writes==8 && f.reads==8 && !f.producer.batch); scene(&f,true,0,0,5);
	init(&f); CHECK(draw(&f,0,90,0,1,false)==0 && f.writes==2 && f.reads==2); scene(&f,false,10,9,5);
	/* Full turns other than literal0..360 retain the decimal core. */
	init(&f); CHECK(draw(&f,360,720,1,1,false)==0 && f.writes==8 && f.reads==8 && f.producer.mode==0 && !f.producer.batch);
	CHECK(f.state.bgi.ddo_x==0xa050 && f.state.bgi.ddo_y==1 && f.state.bgi.device_x==0xa050 && f.state.bgi.device_y==1); scene(&f,true,0,0,5); caches(&f);
	static const uint16_t full[]={10,10,11,10,11,10}; words(f.state.bgi.arc,full,6);
	init(&f); f.state.bgi.orientation=1; CHECK(draw(&f,0,1,1,1,true)==0 && f.writes==1 && f.begins==0); scene(&f,false,9,10,5);
	static const uint16_t oriented[]={10,10,10,9,9,10}; words(f.state.bgi.arc,oriented,6);
	init(&f); f.state.bgi.orientation=255; CHECK(draw(&f,65500,65501,1,1,true)==0 && f.writes==1 && f.state.bgi.curve_span==1); scene(&f,false,10,10,5);
}
static void word_angles_and_lower_entry_guards(void)
{
	struct fixture f; init(&f);
	/* The selected device route treats ffff as angle-1. Only the
	 * outer017a wrapper, not0906, recognizes a query sentinel. */
	CHECK(draw(&f,65535,0,1,1,true)==0 && f.writes==1 && f.begins==0 && f.state.bgi.curve_span==1); scene(&f,false,11,10,5);
	static const uint16_t sentinel[]={10,10,10,11,11,10}; words(f.state.bgi.arc,sentinel,6);
	init(&f); CHECK(draw(&f,1,0,1,1,false)==0 && f.writes==8 && f.reads==8 && f.state.bgi.curve_span==65535); scene(&f,true,0,0,5);
	const struct st_big_producer_sink sink={.drawing={&f,pixel},.read_context=&f,.read_byte=read_byte};
	CHECK(st_big_curve_regular_outline(NULL,&f.producer,0,1,1,1,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_regular_outline(&f.state,NULL,0,1,1,1,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_regular_outline(&f.state,&f.producer,0,1,1,1,NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);
}
static void current_state_and_failure_prefixes(void)
{
	struct fixture f; init(&f); f.mutate=true;
	CHECK(draw(&f,0,360,1,1,true)==0 && f.begins==1 && f.ends==1 && f.writes==8 && f.reads==8 && f.state.bgi.color==6 && f.producer.mode==0); scene(&f,true,0,0,6);
	static const uint16_t live[]={7,6,19,19,21,21}; words(f.state.bgi.arc,live,6);
	CHECK(f.state.bgi.curve_radius[0]==6554 && f.state.bgi.curve_radius[1]==7777);
	/* Empty checked overflow still ends, publishes original trig offsets,
	 * and clears the independent producer BYTE. */
	init(&f); CHECK(draw(&f,0,360,6554,1,true)==0 && f.begins==1 && f.ends==1 && f.writes==0 && f.producer.mode==0 && !f.producer.batch);
	static const uint16_t overflow[]={10,10,6564,10,6564,10}; words(f.state.bgi.arc,overflow,6); caches(&f);
	init(&f); f.fail_write=3;
	CHECK(draw(&f,0,360,1,1,false)==IO_ERROR_IO && f.writes==2 && f.producer.mode==2 && f.producer.batch && f.state.bgi.ddo_x==0xa020 && f.state.bgi.ddo_y==2);
	static const uint16_t prefix[]={10,10,102,103,104,105}; words(f.state.bgi.arc,prefix,6);
	static const uint16_t offsets[]={1,0,1,0}; words(f.state.bgi.curve_offsets,offsets,4); caches(&f);
	init(&f); f.begin_status=IO_ERROR_IO;
	CHECK(draw(&f,0,360,1,1,true)==IO_ERROR_IO && f.begins==1 && f.ends==0 && f.writes==0 && f.producer.mode==2 && f.producer.batch && f.state.bgi.color==5); words(f.state.bgi.arc,prefix,6);
	init(&f); f.end_status=IO_ERROR_IO;
	CHECK(draw(&f,0,360,1,1,true)==IO_ERROR_IO && f.begins==1 && f.ends==1 && f.writes==8 && f.producer.mode==2 && !f.producer.batch && f.state.bgi.color==6); words(f.state.bgi.arc,prefix,6);
	/* An installed failure is reached after the front's state prefix. */
	init(&f); f.state.bgi.state=0;
	CHECK(draw(&f,0,1,1,1,false)==IO_ERROR_PROTOCOL && f.state.bgi.error==-1 && f.producer.mode==2 && f.writes==0); words(f.state.bgi.arc,prefix,6);
	/* Clipped shortcut still moves using rejected request coordinates. */
	init(&f); f.state.bgi.viewport[0]=20; f.state.bgi.viewport[1]=20;
	CHECK(draw(&f,0,1,1,1,false)==0 && f.writes==0 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==10 && f.producer.mode==0);
}
int main(void) { zero_span_records_are_separate(); shortcut_and_decimal_boundaries(); word_angles_and_lower_entry_guards(); current_state_and_failure_prefixes(); return 0; }
