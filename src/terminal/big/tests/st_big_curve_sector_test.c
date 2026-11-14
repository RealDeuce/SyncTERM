/* Independent DDO0822..0af6 lower sector and0450 radial oracles, BIG13/60/61. */
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
	struct st_big_state state; struct st_big_producer producer; struct st_big_producer_point records[1023];
	uint8_t pixels[64*64]; size_t writes,lines,spans,colors,begins,ends,fail_line;
	uint16_t requests[32][4],pieces[8][5]; bool in_span,fail_span,mutate_span,mutate_radial,render;
	int begin_error,end_error;
};
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); memset(f->pixels,3,sizeof(f->pixels)); st_big_state_init(&f->state); f->render=true;
	f->state.bgi.width=f->state.bgi.height=64; f->state.bgi.viewport[2]=f->state.bgi.viewport[3]=63;
	CHECK(st_big_fill_builtin(&f->state,1,5)==0 && st_big_draw_color(&f->state,9)==0);
	f->state.bgi.x=1; f->state.bgi.y=2; f->state.bgi.ddo_x=f->state.bgi.device_x=10; f->state.bgi.ddo_y=f->state.bgi.device_y=10;
	f->state.bgi.error=-11; f->state.bgi.curve_span=222; f->state.bgi.curve_first_valid=7; f->state.bgi.curve_first[0]=50; f->state.bgi.curve_first[1]=60;
	for (size_t i=0;i<6;++i) f->state.bgi.arc[i]=(uint16_t)(100+i);
	for (size_t i=0;i<4;++i) f->state.bgi.curve_offsets[i]=(uint16_t)(10*(i+1));
	CHECK(st_big_producer_bind(&f->producer,f->records,1023,4096)==0); f->producer.count=13; f->producer.run_length=17; f->producer.video_segment=0xa000;
}
static int pixel(void *context, uint16_t x, uint16_t y, uint8_t pen, bool xor_mode)
{
	struct fixture *f=context; CHECK(x<64 && y<64 && pen<=15);
	if (!f->in_span && ++f->lines==f->fail_line) return IO_ERROR_IO;
	if (f->mutate_radial && !f->in_span && f->lines==1) {
		f->state.bgi.ddo_x=20; f->state.bgi.ddo_y=21; f->state.bgi.curve_offsets[2]=1; f->state.bgi.curve_offsets[3]=0;
		f->state.bgi.viewport[0]=3; f->state.bgi.viewport[1]=4;
	}
	++f->writes;
	if (f->render) { if (xor_mode) f->pixels[y*64+x]^=pen; else f->pixels[y*64+x]=pen; }
	return 0;
}
static int color(void *context, uint16_t tuple) { struct fixture *f=context; ++f->colors; CHECK(tuple==0x0505 || tuple==0x0509 || tuple==9); return 0; }
static int span(void *context, bool raw, const uint16_t p[4])
{
	struct fixture *f=context; CHECK(f->spans<32); memcpy(f->requests[f->spans++],p,4*sizeof(*p));
	if (f->fail_span) return IO_ERROR_IO;
	f->in_span=true; const struct st_big_draw_sink sink={f,pixel};
	int status=st_big_draw_polygon_span(&f->state,raw,p,&sink);
	f->in_span=false; return status;
}
static int begin(void *context, uint16_t *value)
{
	struct fixture *f=context; CHECK(f->begins<8 && *value==9); uint16_t *p=f->pieces[f->begins++];
	p[0]=f->state.bgi.curve_span; for (size_t i=0;i<4;++i) p[i+1]=f->state.bgi.curve_offsets[i];
	f->producer.batch=true; if (f->mutate_span) f->state.bgi.curve_span=0;
	return f->begin_error;
}
static int end(void *context) { struct fixture *f=context; ++f->ends; f->producer.batch=false; return f->end_error; }
static int draw(struct fixture *f, uint16_t start, uint16_t end_angle, uint16_t rx, uint16_t ry)
{
	const struct st_big_producer_sink sink={.drawing={f,pixel}}; const struct st_big_polygon_sink polygon={f,color,span}; const struct st_big_curve_batch_sink batch={f,begin,end};
	return st_big_curve_sector(&f->state,&f->producer,start,end_angle,rx,ry,&sink,&batch,&polygon);
}
static void scene(const struct fixture *f, const uint16_t (*sites)[2], size_t count, uint8_t pen)
{
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		bool lit=false; for (size_t i=0;i<count;++i) if (sites[i][0]==x && sites[i][1]==y) lit=true;
		CHECK(f->pixels[y*64+x]==(lit?pen:3));
	}
}
static void arc(const struct fixture *f, const uint16_t words[6]) { for (size_t i=0;i<6;++i) CHECK(f->state.bgi.arc[i]==words[i]); }
static void normal_small_and_zero(void)
{
	struct fixture f; static const uint16_t center[][2]={{10,10}},short_arc[][2]={{10,10},{11,10}},quarter[][2]={{10,9},{10,10},{11,10}},half[][2]={{9,10},{10,9},{10,10},{11,10}},full[][2]={{10,9},{10,11},{9,10},{11,10},{10,10}};
	static const uint16_t equal[]={0,1,359,361,720,0x7fff,0x8000,0xffff};
	for (size_t i=0;i<sizeof(equal)/sizeof(equal[0]);++i) {
		init(&f); CHECK(draw(&f,equal[i],equal[i],65535,65535)==0 && f.writes==2 && f.begins==0 && f.spans==0 && f.producer.count==13 && f.producer.run_length==17);
		scene(&f,center,1,9); const uint16_t a[]={10,10,10,10,10,10}; arc(&f,a); CHECK(f.state.bgi.curve_first_valid==255 && f.state.bgi.curve_first[0]==0 && f.state.bgi.curve_span==0);
	}
	init(&f); f.state.bgi.write_mode=1; CHECK(draw(&f,0,0,10,10)==0 && f.writes==2); scene(&f,NULL,0,0);
	init(&f); CHECK(draw(&f,0,1,1,1)==0 && f.writes==3 && f.producer.count==0); scene(&f,short_arc,2,9);
	const uint16_t a1[]={10,10,11,10,10,10}; arc(&f,a1);
	init(&f); CHECK(draw(&f,0,90,1,1)==0 && f.writes==8 && f.begins==1 && f.ends==1 && f.spans==1 && f.producer.count==3);
	scene(&f,quarter,3,9); const uint16_t a2[]={10,10,11,10,10,9}; arc(&f,a2); CHECK(f.state.bgi.device_x==10 && f.state.bgi.device_y==9 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10);
	init(&f); CHECK(draw(&f,0,180,1,1)==0 && f.writes==12 && f.begins==2 && f.spans==2); scene(&f,half,4,9);
	const uint16_t ah[]={10,10,11,10,9,10}; arc(&f,ah);
	init(&f); CHECK(draw(&f,720,360,1,1)==0 && f.writes==22 && f.begins==4 && f.ends==4 && f.spans==4); scene(&f,full,5,9);
	const uint16_t af[]={10,10,11,10,11,10}; arc(&f,af); CHECK(f.state.bgi.device_x==10 && f.state.bgi.device_y==11);
	init(&f); CHECK(draw(&f,360,360,1,1)==0 && f.writes==22 && f.spans==4); scene(&f,full,5,9); arc(&f,af);
	const uint16_t spans[][4]={{10,9,10,9},{10,9,10,9},{9,10,10,10},{10,10,11,10}}; CHECK(memcmp(f.requests,spans,sizeof(spans))==0);
	init(&f); CHECK(draw(&f,0xffff,0,1,1)==0 && f.state.bgi.curve_span==15 && f.producer.count==1 && f.writes==3); scene(&f,short_arc,2,9); arc(&f,a1);
	init(&f); f.state.bgi.orientation=1; CHECK(draw(&f,0,90,1,2)==0 && f.state.bgi.curve_radius[0]==2 && f.state.bgi.curve_radius[1]==1);
	const uint16_t rotated[][2]={{10,9},{9,9},{8,10},{9,10},{10,10}},ar[]={10,10,10,9,8,10}; scene(&f,rotated,5,9); arc(&f,ar);
	init(&f); CHECK(st_big_draw_style(&f.state,4,0,3)==0); CHECK(draw(&f,0,90,1,1)==0 && f.state.bgi.line_mask==0 && f.state.bgi.device_line_width==3 && f.state.bgi.ddo_line_width==3);
	const uint16_t top[][2]={{10,9}}; scene(&f,top,1,5);
	init(&f); CHECK(st_big_fill_builtin(&f.state,0,0)==0); CHECK(draw(&f,0,90,1,1)==0 && f.spans==0 && f.colors==1); scene(&f,quarter,3,9);
}
static void quadrants_overflow_and_callbacks(void)
{
	struct fixture f; init(&f); f.render=false; CHECK(draw(&f,80,100,10,10)==0 && f.begins==2 && f.ends==2);
	const uint16_t pieces[][5]={{10,1,65527,0,65526},{10,0,65526,65534,65527}}; CHECK(memcmp(f.pieces,pieces,sizeof(pieces))==0);
	const uint16_t a[]={10,10,11,1,8,1}; arc(&f,a);
	init(&f); f.mutate_span=true; CHECK(draw(&f,0,90,1,1)==0 && f.producer.count==3 && f.spans==1 && f.state.bgi.curve_span==0);
	const uint16_t q[][2]={{10,9},{10,10},{11,10}}; scene(&f,q,3,9);
	init(&f); CHECK(draw(&f,0,90,6554,6554)==0 && f.begins==1 && f.ends==1 && f.spans==0 && f.producer.count==0 && f.state.bgi.error==-11);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) CHECK(f.pixels[y*64+x]==((y==10 && x>=10) || (x==10 && y<=10)?9:3));
	const uint16_t large[]={10,10,6564,10,10,58992}; arc(&f,large);
	init(&f); f.mutate_radial=true; CHECK(draw(&f,0,0,1,1)==0 && f.writes==3 && f.state.bgi.ddo_x==20 && f.state.bgi.ddo_y==21);
	const uint16_t late[][2]={{10,10},{11,10}},late_arc[]={7,6,7,6,8,6}; scene(&f,late,2,9); arc(&f,late_arc);
	init(&f); f.begin_error=IO_ERROR_IO; CHECK(draw(&f,0,90,1,1)==IO_ERROR_IO && f.producer.mode==1 && f.producer.count==0 && f.producer.batch && f.state.bgi.curve_first_valid==0); scene(&f,NULL,0,0);
	init(&f); f.end_error=IO_ERROR_IO; CHECK(draw(&f,0,90,1,1)==IO_ERROR_IO && f.producer.mode==1 && f.producer.count==3 && !f.producer.batch && f.spans==0);
	init(&f); f.fail_span=true; CHECK(draw(&f,0,90,1,1)==IO_ERROR_IO && f.producer.count==6 && f.state.bgi.curve_first_valid==0 && f.state.bgi.device_color==5 && f.writes==0);
	CHECK(f.state.bgi.arc[2]==102 && f.state.bgi.curve_offsets[0]==1 && f.state.bgi.curve_offsets[3]==65535);
	init(&f); f.fail_line=1; CHECK(draw(&f,0,90,1,1)==IO_ERROR_IO && f.producer.count==3 && f.state.bgi.ddo_x==11 && f.state.bgi.ddo_y==10 && f.state.bgi.device_y==9 && f.state.bgi.arc[2]==102);
	const uint16_t top[][2]={{10,9}}; scene(&f,top,1,5);
	init(&f); f.fail_line=2; f.state.bgi.viewport[0]=3; f.state.bgi.viewport[1]=4; f.state.bgi.ddo_x=13; f.state.bgi.ddo_y=14;
	CHECK(draw(&f,0,0,10,10)==IO_ERROR_IO && f.writes==1 && f.state.bgi.arc[0]==13 && f.state.bgi.arc[1]==14 && f.state.bgi.arc[2]==102);
	const uint16_t translated[][2]={{13,14}}; scene(&f,translated,1,9);
}
static void alternate_and_producer_radials(void)
{
	struct fixture f; const uint16_t sites[][2]={{10,10},{11,10}};
	for (unsigned selector=0;selector<3;++selector) {
		init(&f); if (selector==2) f.state.bgi.curve_metric=180; else f.state.bgi.curve_fallback=1;
		CHECK(draw(&f,selector==1?0xffff:0,0,1,1)==0 && f.writes==4 && f.begins==0 && f.producer.count==5 && f.producer.mode==0);
		scene(&f,sites,2,9); CHECK(f.state.bgi.curve_first_valid==7 && f.state.bgi.curve_span==222 && f.state.bgi.curve_offsets[0]==10 && f.state.bgi.arc[0]==10 && f.state.bgi.arc[2]==102 && f.state.bgi.arc[5]==105);
		CHECK(f.state.bgi.device_x==0x8000 && f.state.bgi.device_y==0x8000 && f.state.bgi.curve_fallback==(selector==2?255:1));
	}
	init(&f); f.state.bgi.curve_fallback=1; CHECK(draw(&f,0,360,1,1)==0 && f.producer.count==365 && f.lines==368 && f.writes==372 && f.spans==2);
	for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
		uint8_t pen=(y==9 && x==10) || (y==10 && x>=9 && x<=11)?5:(y==11 && (x==9 || x==10))?9:3;
		CHECK(f.pixels[y*64+x]==pen);
	}
	CHECK(f.state.bgi.device_x==10 && f.state.bgi.device_y==10 && f.state.bgi.ddo_x==10 && f.state.bgi.ddo_y==10 && f.state.bgi.curve_first[0]==50 && f.state.bgi.curve_first[1]==60);
	const uint16_t old_arc[]={10,10,102,103,104,105}; arc(&f,old_arc);
	init(&f); f.state.bgi.curve_fallback=1; f.fail_span=true;
	CHECK(draw(&f,0,90,1,1)==IO_ERROR_IO && f.producer.count==95 && f.lines==96 && f.state.bgi.arc[2]==102 && f.state.bgi.curve_first_valid==7);
	const uint16_t q[][2]={{10,9},{10,10},{11,10}}; scene(&f,q,3,9);
	init(&f); f.state.bgi.curve_fallback=1; f.producer.capacity=0; CHECK(draw(&f,0,32767,1,1)==IO_ERROR_OUT_OF_RANGE && f.producer.mode==1 && f.producer.count==0 && f.state.bgi.error==-6 && f.writes==0 && f.colors==0);
	init(&f); f.producer.mode=1; f.producer.count=f.producer.run_length=0; CHECK(draw(&f,0,0,1,1)==0 && f.writes==0 && f.producer.count==1 && f.records[0].x==10 && f.records[0].y==10 && f.producer.mode==1);
	init(&f); f.producer.mode=2; CHECK(draw(&f,0,0,1,1)==0 && f.writes==2 && f.producer.mode==2 && f.state.bgi.ddo_x==10 && f.state.bgi.device_x==10);
	const uint16_t center[][2]={{10,10}}; scene(&f,center,1,9);
	init(&f); f.producer.mode=2; struct st_big_producer_registers r={.ax=10,.bx=10,.cx=11,.dx=10}; const struct st_big_producer_sink sink={.drawing={&f,pixel}};
	CHECK(st_big_producer_stroke(&f.state,&f.producer,&r,&sink)==0 && r.ax==0x0f02 && r.bx==2 && r.cx==0x2009 && r.dx==9 && f.writes==1 && f.state.bgi.ddo_x==10 && f.state.bgi.device_x==10); scene(&f,center,1,9);
	const struct st_big_polygon_sink polygon={&f,color,span}; CHECK(st_big_curve_sector(NULL,&f.producer,0,90,1,1,&sink,NULL,&polygon)==IO_ERROR_INVALID_ARGUMENT);
}
int main(void) { normal_small_and_zero(); quadrants_overflow_and_callbacks(); alternate_and_producer_radials(); return 0; }
