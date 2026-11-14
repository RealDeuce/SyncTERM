/* Public wrapper/aspect/query/three-point oracles from original BIG13/27/61. */
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
	struct st_big_producer_point records[1023]; struct st_big_curve_request request;
	uint8_t pixels[64*64]; size_t reads,writes,attempts,fail_at,begins,ends;
	int begin_status,end_status; bool mutate;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state);
	f->state.bgi.width=f->state.bgi.height=64; f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=3; f->state.bgi.ddo_y=4; f->state.bgi.device_x=5; f->state.bgi.device_y=6;
	CHECK(st_big_draw_color(&f->state,5)==0); f->state.bgi.write_mode=1; f->state.bgi.error=-11;
	for (size_t i=0;i<6;++i) f->state.bgi.arc[i]=(uint16_t)(100+i);
	for (size_t i=0;i<4;++i) f->state.bgi.curve_offsets[i]=(uint16_t)(10*(i+1));
	f->producer.mode=1; f->producer.count=7; f->producer.run_length=9; f->producer.video_segment=0xa000;
	CHECK(st_big_producer_bind(&f->producer,f->records,1023,4096)==0);
	f->request=(struct st_big_curve_request){.center={10,10},.start=0,.end=360,.radius={1,1}};
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen==5);
	++f->attempts; if (f->attempts==f->fail_at) return IO_ERROR_IO;
	++f->writes; if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen; return 0;
}
static int read_byte(void *context, uint16_t x, uint16_t y, uint8_t plane, uint8_t *byte)
{
	struct fixture *f=context; CHECK(x%8==0 && x<64 && y<64 && plane==0); ++f->reads; *byte=0;
	for (unsigned bit=0;bit<8;++bit) if ((f->pixels[y*64+x+bit]&1)!=0) *byte|=(uint8_t)(128u>>bit);
	return 0;
}
static int begin(void *context, uint16_t *color)
{
	struct fixture *f=context; ++f->begins; f->producer.batch=true; CHECK(*color==5);
	if (f->mutate) {
		f->state.bgi.viewport[0]=3; f->state.bgi.viewport[1]=4;
		f->request=(struct st_big_curve_request){.center={30,31},.start=0,.end=0,.radius={6554,6554}};
	}
	return f->begin_status;
}
static int end(void *context)
{
	struct fixture *f=context; ++f->ends; f->producer.batch=false; return f->end_status;
}
static int draw(struct fixture *f, bool circular, bool custom)
{
	const struct st_big_producer_sink sink={.drawing={f,pixel},.read_context=f,.read_byte=read_byte};
	const struct st_big_curve_batch_sink batch={f,begin,end};
	return circular?st_big_curve_arc(&f->state,&f->producer,&f->request,&sink,custom?&batch:NULL)
	    :st_big_curve_ellipse(&f->state,&f->producer,&f->request,&sink,custom?&batch:NULL);
}
static void words(const uint16_t *a, const uint16_t *b, size_t count)
{
	for (size_t i=0;i<count;++i) CHECK(a[i]==b[i]);
}
static void scene(struct fixture *f, bool full, unsigned x, unsigned y)
{
	for (unsigned yy=0;yy<64;++yy) for (unsigned xx=0;xx<64;++xx) {
		bool lit=full?(xx==10 && (yy==9 || yy==11)) || (yy==10 && (xx==9 || xx==11)):xx==x && yy==y;
		CHECK(f->pixels[yy*64+xx]==(lit?5:0));
	}
}
static void successful_point_owners(void)
{
	struct fixture f; init(&f); f.request.end=1;
	CHECK(draw(&f,false,true)==0 && f.writes==1 && f.begins==0 && f.ends==0);
	CHECK(f.state.bgi.x==1 && f.state.bgi.y==2 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10);
	CHECK(f.state.bgi.device_x==0x0f02 && f.state.bgi.device_y==2 && f.state.bgi.error==-11 && f.producer.mode==0); scene(&f,false,10,10);
	init(&f); CHECK(draw(&f,false,false)==0 && f.writes==8 && f.reads==8);
	CHECK(f.state.bgi.x==1 && f.state.bgi.y==2 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==0xa050 && f.state.bgi.device_y==1); scene(&f,true,0,0);
	init(&f); f.state.bgi.viewport[0]=3; f.state.bgi.viewport[1]=4; f.request.end=1;
	CHECK(draw(&f,false,false)==0 && f.writes==1 && f.state.bgi.ddo_x==13 && f.state.bgi.ddo_y==14 && f.state.bgi.device_x==0x0f02 && f.state.bgi.device_y==5); scene(&f,false,13,14);
	static const uint16_t translated[]={10,10,11,10,10,10}; words(f.state.bgi.arc,translated,6);
	init(&f); CHECK(st_big_draw_style(&f.state,0,0,3)==0); f.request.end=3;
	CHECK(draw(&f,false,false)==0 && f.writes==12 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==11 && f.state.bgi.device_y==10);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==(x>=10 && x<=11 && y>=9 && y<=11?5:0));
}
static void query_after_center(void)
{
	for (unsigned width=1;width<=3;++width) {
		struct fixture f; init(&f); CHECK(st_big_draw_style(&f.state,4,0x1234,(uint16_t)width)==0);
		f.request.start=65535; f.request.end=32767; f.request.radius[0]=65535; f.request.radius[1]=65535;
		f.state.bgi.viewport[0]=3; f.state.bgi.viewport[1]=4;
		CHECK(draw(&f,false,true)==0 && f.writes==0 && f.reads==0 && f.begins==0 && f.ends==0);
		CHECK(f.state.bgi.ddo_x==13 && f.state.bgi.ddo_y==14 && f.state.bgi.device_x==13 && f.state.bgi.device_y==14 && f.state.bgi.x==1 && f.state.bgi.y==2);
		static const uint16_t old[]={100,101,102,103,104,105}; words(f.state.bgi.arc,old,6);
		CHECK(f.producer.mode==1 && f.producer.count==7 && f.producer.run_length==9 && f.state.bgi.curve_radius[0]==0 && f.state.bgi.line_mask==0x1234);
	}
	struct fixture f; init(&f); f.request.start=65535; f.state.bgi.aspect[0]=10000;
	CHECK(draw(&f,true,true)==0 && f.writes==0 && f.begins==0 && f.state.bgi.ddo_x==10 && f.state.bgi.device_x==10);
	/* Circular aspect is required even for a subsequent query. */
	init(&f); f.request.start=65535; f.state.bgi.aspect[1]=0;
	CHECK(draw(&f,true,true)==IO_ERROR_OUT_OF_RANGE && f.writes==0 && f.state.bgi.ddo_x==3 && f.state.bgi.ddo_y==4 && f.state.bgi.device_x==5 && f.state.bgi.device_y==6 && f.state.bgi.error==-11);
}
static void mode_aspects_and_original_radius_bits(void)
{
	static const uint16_t aspects[]={4500,7750,10000},radii[]={0,1,2};
	static const size_t writes[]={3,5,6};
	for (size_t mode=0;mode<3;++mode) {
		struct fixture f; init(&f); f.state.bgi.aspect[0]=aspects[mode]; f.request.radius[0]=2; f.request.radius[1]=65535; f.request.end=90;
		CHECK(draw(&f,true,false)==0 && f.writes==writes[mode] && f.state.bgi.curve_radius[0]==2 && f.state.bgi.curve_radius[1]==radii[mode]);
		CHECK(f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.device_x==0xa008 && f.state.bgi.device_y==4 && f.state.bgi.x==1 && f.state.bgi.y==2);
		for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
			bool lit=mode==0?(x==11 && y==9)||(x==12 && y==10):mode==1?(y==9 && x>=10 && x<=11)||(x==12 && y==10)
			    :(y==8 && x>=10 && x<=11)||(x==12 && y>=9 && y<=10);
			CHECK(f.pixels[y*64+x]==(lit?5:0));
		}
		const uint16_t arc[]={10,10,12,10,10,(uint16_t)(10-radii[mode])}; words(f.state.bgi.arc,arc,6);
	}
	struct fixture f; init(&f); f.state.bgi.aspect[0]=10000; f.request.radius[0]=32768;
	CHECK(draw(&f,true,true)==0 && f.writes==0 && f.begins==1 && f.ends==1 && f.state.bgi.curve_radius[0]==32768 && f.state.bgi.curve_radius[1]==32768 && f.state.bgi.error==-11);
	init(&f); f.state.bgi.aspect[0]=65535; f.state.bgi.aspect[1]=1; f.request.radius[0]=20;
	CHECK(draw(&f,true,true)==0 && f.writes==0 && f.state.bgi.curve_radius[0]==20 && f.state.bgi.curve_radius[1]==65516 && f.state.bgi.ddo_x==10 && f.state.bgi.device_x==10);
}
static void failures_and_saved_center(void)
{
	struct fixture f; init(&f); f.fail_at=3;
	CHECK(draw(&f,false,false)==IO_ERROR_IO && f.writes==2 && f.state.bgi.ddo_x==0xa020 && f.state.bgi.ddo_y==2 && f.state.bgi.device_x==0xa020 && f.state.bgi.device_y==2 && f.producer.mode==2 && f.producer.batch);
	init(&f); f.begin_status=IO_ERROR_IO;
	CHECK(draw(&f,false,true)==IO_ERROR_IO && f.begins==1 && f.ends==0 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.producer.batch);
	init(&f); f.end_status=IO_ERROR_IO;
	CHECK(draw(&f,false,true)==IO_ERROR_IO && f.writes==8 && f.state.bgi.ddo_x==0xa050 && f.state.bgi.ddo_y==1 && f.state.bgi.device_x==0xa050 && !f.producer.batch);
	init(&f); f.mutate=true;
	CHECK(draw(&f,false,true)==0 && f.writes==8 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.viewport[0]==3 && f.request.center[0]==30); scene(&f,true,0,0);
	static const uint16_t live[]={7,6,8,6,8,6}; words(f.state.bgi.arc,live,6);
	init(&f); f.state.bgi.state=0;
	CHECK(draw(&f,false,false)==IO_ERROR_PROTOCOL && f.state.bgi.error==-1 && f.state.bgi.ddo_x==3 && f.state.bgi.device_x==5 && f.writes==0);
	init(&f); f.state.bgi.state=0; f.state.bgi.aspect[1]=0;
	CHECK(draw(&f,true,false)==IO_ERROR_OUT_OF_RANGE && f.state.bgi.error==-11 && f.state.bgi.ddo_x==3 && f.state.bgi.device_x==5);
}
static void argument_boundaries(void)
{
	struct fixture f; init(&f);
	const struct st_big_producer_sink sink={.drawing={&f,pixel},.read_context=&f,.read_byte=read_byte};
	CHECK(st_big_curve_ellipse(NULL,&f.producer,&f.request,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_ellipse(&f.state,NULL,&f.request,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_ellipse(&f.state,&f.producer,NULL,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_ellipse(&f.state,&f.producer,&f.request,NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_arc(NULL,&f.producer,&f.request,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_arc(&f.state,NULL,&f.request,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_arc(&f.state,&f.producer,NULL,&sink,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_curve_arc(&f.state,&f.producer,&f.request,NULL,NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(f.state.bgi.ddo_x==3 && f.state.bgi.device_x==5 && f.producer.mode==1 && f.state.bgi.error==-11 && f.writes==0);
}
int main(void) { argument_boundaries(); successful_point_owners(); query_after_center(); mode_aspects_and_original_radius_bits(); failures_and_saved_center(); return 0; }
