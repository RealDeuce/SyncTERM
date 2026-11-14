/* Independent curve batch/point/three-CP expectations, original BIG13/58/61. */
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
struct fixture;
struct batch_context { struct fixture *fixture; unsigned number; };
struct fixture {
	struct st_big_state state; struct st_big_producer producer;
	struct st_big_producer_point collected[16]; uint8_t pixels[64*64];
	struct st_big_curve_batch_sink batch; struct batch_context contexts[2];
	size_t reads,writes,attempts,begins,ends[2],fail_read,fail_write;
	uint8_t pens[16]; uint16_t coordinates[16][2],begin_color;
	int begin_status,end_status;
	unsigned mutation;
};
static int alternate_end(void *);
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64;
	f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=3; f->state.bgi.ddo_y=4;
	f->state.bgi.device_x=5; f->state.bgi.device_y=6;
	f->state.bgi.color=0x1205; f->state.bgi.device_color=7;
	f->state.bgi.write_mode=1; f->state.bgi.line_mask=0; f->state.bgi.error=-11;
	f->producer.mode=2; f->producer.video_segment=0xa000;
	CHECK(st_big_producer_bind(&f->producer,f->collected,16,96)==0);
	f->contexts[0]=(struct batch_context){f,0}; f->contexts[1]=(struct batch_context){f,1};
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	struct fixture *f=context; ++f->reads; CHECK(x%8==0 && x<64 && y<64 && plane<4 && f->producer.batch);
	if (f->reads==f->fail_read) return IO_ERROR_IO;
	*byte=0;
	for (unsigned bit=0;bit<8;++bit) if ((f->pixels[y*64+x+bit]&(1u<<plane))!=0) *byte|=(uint8_t)(128u>>bit);
	return 0;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<16 && !xor_mode && f->attempts<16);
	f->coordinates[f->attempts][0]=x; f->coordinates[f->attempts][1]=y; f->pens[f->attempts++]=pen;
	if (f->attempts==f->fail_write) return IO_ERROR_IO;
	++f->writes; f->pixels[y*64+x]=pen;
	if (f->mutation==1 && f->writes==4) f->state.bgi.color=9;
	if (f->mutation==2 && f->writes==1) f->state.bgi.curve_batch_gate=1;
	if (f->mutation==3 && f->writes==1) f->state.bgi.curve_batch_gate=0;
	if (f->mutation==4 && f->writes==1) { f->batch.context=&f->contexts[1]; f->batch.end=alternate_end; }
	if (f->mutation==5 && f->writes==1) { f->producer.video_segment=0xb800; f->producer.read_plane=1; }
	return 0;
}
static int begin(void *context, uint16_t *color)
{
	struct batch_context *c=context; struct fixture *f=c->fixture; CHECK(c->number==0);
	++f->begins; f->begin_color=*color; f->producer.batch=true;
	f->state.bgi.color=9; *color=0x0106;
	return f->begin_status;
}
static int end(void *context)
{
	struct batch_context *c=context; struct fixture *f=c->fixture; CHECK(c->number==0);
	++f->ends[0]; f->producer.batch=false; return f->end_status;
}
static int alternate_end(void *context)
{
	struct batch_context *c=context; struct fixture *f=c->fixture; CHECK(c->number==1);
	++f->ends[1]; f->producer.batch=false; return f->end_status;
}
static void methods(struct fixture *f) { f->batch=(struct st_big_curve_batch_sink){&f->contexts[0],begin,end}; }
static int draw(struct fixture *f, uint16_t rx, uint16_t ry, bool custom)
{
	const struct st_big_curve_decimal_frame frame={.center={10,10},.radius={rx,ry},.start={1,0},.end={1,0}};
	const struct st_big_producer_sink sink={.drawing={f,pixel},.read_context=f,.read_byte=read_byte};
	return st_big_curve_decimal_draw(&f->state,&f->producer,frame,&sink,custom?&f->batch:NULL);
}
static void scene(struct fixture *f, uint8_t vertical, uint8_t horizontal)
{
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		unsigned expected=x==10 && (y==9 || y==11)?vertical:y==10 && (x==9 || x==11)?horizontal:0;
		CHECK(f->pixels[y*64+x]==expected);
	}
	CHECK(f->state.bgi.x==1 && f->state.bgi.y==2 && f->state.bgi.device_color==7 && f->state.bgi.write_mode==1 && f->state.bgi.line_mask==0 && f->state.bgi.error==-11);
}
static void builtin_points_and_live_bytes(void)
{
	static const uint16_t points[8][2]={{10,11},{10,11},{10,9},{10,9},{11,10},{9,10},{11,10},{9,10}};
	for (unsigned plane=0;plane<4;++plane) {
		struct fixture f; init(&f); f.producer.read_plane=(uint8_t)plane; f.producer.batch=true;
		CHECK(draw(&f,1,1,false)==0 && f.reads==8 && f.writes==8 && !f.producer.batch && f.producer.mode==2);
		for (size_t i=0;i<8;++i) CHECK(f.coordinates[i][0]==points[i][0] && f.coordinates[i][1]==points[i][1] && f.pens[i]==5);
		uint16_t expected=(uint16_t)(plane==0 || plane==2?0xa050:0xa000);
		CHECK(f.state.bgi.ddo_x==expected && f.state.bgi.ddo_y==1 && f.state.bgi.device_x==expected && f.state.bgi.device_y==1 && f.state.bgi.color==0x1205);
		scene(&f,5,5);
	}
	for (unsigned color=0;color<256;++color) {
		struct fixture f; init(&f); f.state.bgi.color=(uint16_t)(0xab00u+color);
		CHECK(draw(&f,1,1,false)==0 && f.state.bgi.color==0xab00u+color && f.reads==8 && f.writes==8 && !f.producer.batch);
		CHECK(f.state.bgi.ddo_x==((color&1)?0xa050:0xa000) && f.state.bgi.ddo_y==1);
		scene(&f,(uint8_t)(color%16),(uint8_t)(color%16));
	}
	struct fixture f; init(&f); f.mutation=1;
	CHECK(draw(&f,1,1,false)==0 && f.state.bgi.color==9 && !f.producer.batch);
	for (size_t i=0;i<8;++i) CHECK(f.pens[i]==(i<4?5:9)); scene(&f,5,9);
	init(&f); f.mutation=5; CHECK(draw(&f,1,1,false)==0);
	CHECK(f.state.bgi.ddo_x==0xb800 && f.state.bgi.device_x==0xb800 && f.state.bgi.ddo_y==1); scene(&f,5,5);
}
static void gates_methods_and_producers(void)
{
	for (unsigned gate=1;gate<=255;++gate) {
		struct fixture f; init(&f); f.state.bgi.curve_batch_gate=(uint8_t)gate;
		CHECK(draw(&f,1,1,true)==0 && f.begins==0 && f.ends[0]==0 && f.reads==0 && !f.producer.batch);
		CHECK(f.state.bgi.ddo_x==0x0f02 && f.state.bgi.device_x==0x0f02 && f.state.bgi.ddo_y==1); scene(&f,5,5);
	}
	struct fixture f; init(&f); f.mutation=2;
	CHECK(draw(&f,1,1,false)==0 && f.producer.batch && f.reads==8 && f.state.bgi.curve_batch_gate==1); scene(&f,5,5);
	init(&f); methods(&f); f.state.bgi.curve_batch_gate=1; f.mutation=3;
	CHECK(draw(&f,1,1,true)==0 && f.begins==0 && f.ends[0]==1 && f.reads==0 && !f.producer.batch); scene(&f,5,5);
	init(&f); methods(&f); f.mutation=4;
	CHECK(draw(&f,1,1,true)==0 && f.begins==1 && f.ends[0]==0 && f.ends[1]==1 && f.begin_color==0x1205 && !f.producer.batch && f.state.bgi.color==0x0106); scene(&f,6,6);
	init(&f); f.producer.mode=1;
	CHECK(draw(&f,1,1,false)==0 && f.reads==0 && f.writes==0 && f.producer.count==7 && f.producer.run_length==7 && !f.producer.batch);
	CHECK(f.state.bgi.ddo_x==3 && f.state.bgi.ddo_y==4 && f.state.bgi.device_x==5 && f.state.bgi.device_y==6);
	init(&f); f.producer.mode=0;
	CHECK(draw(&f,1,1,false)==0 && f.reads==0 && f.writes==0 && f.state.bgi.ddo_x==9 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==9 && f.state.bgi.device_y==10);
}
static void empty_rejected_and_error_prefixes(void)
{
	struct fixture f; init(&f); methods(&f);
	CHECK(draw(&f,6554,1,true)==0 && f.begins==1 && f.ends[0]==1 && f.writes==0 && f.reads==0 && !f.producer.batch);
	CHECK(f.state.bgi.ddo_x==3 && f.state.bgi.device_x==5 && f.state.bgi.color==0x0106);
	init(&f); f.state.bgi.viewport[0]=20; f.state.bgi.viewport[1]=20;
	CHECK(draw(&f,1,1,false)==0 && f.reads==0 && f.writes==0 && f.state.bgi.ddo_x==9 && f.state.bgi.device_x==9 && f.state.bgi.ddo_y==10);
	init(&f); methods(&f); f.begin_status=IO_ERROR_IO;
	CHECK(draw(&f,1,1,true)==IO_ERROR_IO && f.begins==1 && f.ends[0]==0 && f.producer.batch && f.state.bgi.color==9 && f.writes==0);
	init(&f); methods(&f); f.begin_status=IO_ERROR_IN_PROGRESS; CHECK(draw(&f,1,1,true)==IO_ERROR_PROTOCOL && f.producer.batch && f.writes==0);
	init(&f); methods(&f); f.end_status=IO_ERROR_IO;
	CHECK(draw(&f,1,1,true)==IO_ERROR_IO && f.begins==1 && f.ends[0]==1 && !f.producer.batch && f.writes==8 && f.state.bgi.color==0x0106); scene(&f,6,6);
	init(&f); methods(&f); f.end_status=IO_ERROR_IN_PROGRESS;
	CHECK(draw(&f,1,1,true)==IO_ERROR_PROTOCOL && f.ends[0]==1 && !f.producer.batch && f.writes==8);
	init(&f); CHECK(draw(&f,1,1,true)==IO_ERROR_INVALID_ARGUMENT && !f.producer.batch && f.state.bgi.color==0x1205 && f.writes==0);
	init(&f); methods(&f); f.batch.end=NULL;
	CHECK(draw(&f,1,1,true)==IO_ERROR_INVALID_ARGUMENT && f.producer.batch && f.state.bgi.color==0x0106 && f.writes==8);
	init(&f); f.fail_read=3;
	CHECK(draw(&f,1,1,false)==IO_ERROR_IO && f.reads==3 && f.writes==2 && f.producer.batch && f.state.bgi.ddo_x==0xa020 && f.state.bgi.ddo_y==2);
	init(&f); f.fail_write=3;
	CHECK(draw(&f,1,1,false)==IO_ERROR_IO && f.reads==3 && f.writes==2 && f.producer.batch && f.state.bgi.ddo_x==0xa020 && f.state.bgi.ddo_y==2);
	init(&f); f.state.bgi.state=0;
	CHECK(draw(&f,1,1,false)==IO_ERROR_PROTOCOL && f.producer.batch && f.state.bgi.error==-1 && f.state.bgi.color==0x1205 && f.writes==0);
}
int main(void) { builtin_points_and_live_bytes(); gates_methods_and_producers(); empty_rejected_and_error_prefixes(); return 0; }
