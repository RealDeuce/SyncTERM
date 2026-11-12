/* Authored BIG32/64/81 input cases; no emulator-generated expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_collect.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_control c; struct st_big_collect r; struct st_big_collect_sink sink;
	uint8_t echoes[512], wire[512]; size_t echoed, sent, calls, fail_at;
	int failure; bool mutate;
};
static int echo(void *context, uint8_t byte)
{
	struct fixture *f=context;
	if (f->calls++==f->fail_at) return f->failure;
	CHECK(f->echoed<sizeof(f->echoes)); f->echoes[f->echoed++]=byte;
	if (f->mutate) f->c.input.count=0;
	return 0;
}
static int send(void *context, enum st_big_output_kind kind, uint8_t byte)
{
	struct fixture *f=context; CHECK(kind==ST_BIG_OUTPUT_REPLY && f->c.input.pending==0 && f->sent<sizeof(f->wire));
	f->wire[f->sent++]=byte; return 0;
}
static void init(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_control_init(&f->c);
	f->sink=(struct st_big_collect_sink){f,echo}; f->fail_at=SIZE_MAX; f->failure=IO_ERROR_IO;
}
static void begin(struct fixture *f, uint16_t type, uint16_t options) { CHECK(st_big_collect_begin(&f->r,&f->c,type,options)==0); }
static void key(struct fixture *f, uint8_t byte) { CHECK(st_big_collect_key(&f->r,&f->c,byte,&f->sink)==0); }
static void finish(struct fixture *f, bool chained)
{
	struct st_big_loop loop; st_big_loop_init(&loop); struct st_big_footer footer;
	st_big_footer_begin(&footer,chained); size_t budget=512; enum st_big_footer_action action;
	const struct st_big_control_sink sink={.context=f,.send=send};
	CHECK(st_big_footer_poll(&footer,&loop,&f->c,&sink,&budget,&action)==0);
	CHECK(action==(chained?ST_BIG_FOOTER_CHAIN:ST_BIG_FOOTER_RETURN));
}
static void all_types_and_options(void)
{
	struct fixture f;
	for (unsigned type=0;type<65536;++type) {
		if (type==2) continue;
		init(&f); begin(&f,(uint16_t)type,0); key(&f,'A');
		CHECK(f.r.active==(type>0 && type<32768) && f.c.input.pending==1 && f.c.input.count==1 && f.c.input.payload[1]=='A' && f.echoed==0);
	}
	for (unsigned options=0;options<65536;++options) {
		init(&f); f.c.input.append_cr=0xab; f.c.input.count=73; memset(f.c.input.payload,0x5a,sizeof(f.c.input.payload));
		begin(&f,0,(uint16_t)options);
		CHECK(f.c.input.count==0 && f.c.input.append_cr==0xab && f.c.input.payload[1]==0x5a && f.c.input.payload[255]==0x5a);
		CHECK(f.c.input.pending==(options!=2 && options!=3) && f.r.options==(options==3?0:options));
		key(&f,'A'); CHECK(!f.r.active && f.echoed==(options!=0 && options!=3));
	}
}
static void keyboard_counts_and_replies(void)
{
	struct fixture f; init(&f); f.c.input.append_cr=1; begin(&f,1,1);
	for (unsigned i=0;i<130;++i) key(&f,'A');
	CHECK(f.c.input.count==129 && f.echoed==129 && f.r.active);
	key(&f,8); CHECK(f.c.input.count==128 && f.c.input.payload[129]=='A' && f.echoes[129]==' ' && f.echoes[130]==8);
	key(&f,'Z'); key(&f,13); CHECK(!f.r.active && f.r.type==0 && f.c.input.count==129 && f.echoed==132);
	finish(&f,true); CHECK(f.c.input.pending==1 && f.sent==0);
	finish(&f,false); CHECK(f.c.input.pending==0 && f.sent==130 && f.wire[128]=='Z' && f.wire[129]==13);
	for (unsigned i=0;i<128;++i) CHECK(f.wire[i]=='A');
	init(&f); begin(&f,1,0); key(&f,127); CHECK(f.c.input.count==65535 && f.echoed==0);
	key(&f,'A'); CHECK(f.c.input.count==0 && f.c.input.payload[0]=='A'); key(&f,13); finish(&f,false); CHECK(f.sent==0);
	init(&f); begin(&f,1,3); key(&f,8); CHECK(f.echoed==2 && f.echoes[0]==' ' && f.echoes[1]==8 && f.c.input.count==0);
	key(&f,13); finish(&f,false); CHECK(f.sent==0);
	init(&f); begin(&f,1,2); key(&f,'Q'); key(&f,13); finish(&f,false); CHECK(f.echoed==1 && f.echoes[0]=='Q' && f.sent==0);
	static const uint8_t ignored[]={0,1,8,9,13,31,127,128,255};
	for (size_t i=0;i<sizeof(ignored);++i) {
		init(&f); begin(&f,0,0); key(&f,ignored[i]); CHECK(!f.r.active);
		CHECK(f.c.input.count==(ignored[i]==127?65535:0));
	}
	init(&f); begin(&f,1,0); key(&f,' '); key(&f,'~'); key(&f,13);
	CHECK(f.c.input.count==2 && f.c.input.payload[1]==' ' && f.c.input.payload[2]=='~');
	init(&f); begin(&f,7,0); key(&f,0); key(&f,0x48); key(&f,255); key(&f,13);
	CHECK(f.c.input.count==1 && f.c.input.payload[1]=='H' && !f.r.active);
	init(&f); begin(&f,1,0); key(&f,'A'); key(&f,'B'); key(&f,127); key(&f,13);
	CHECK(f.c.input.count==1 && f.c.input.payload[2]=='B');
	begin(&f,0,0); CHECK(f.c.input.count==0 && f.c.input.payload[2]=='B'); key(&f,'C'); finish(&f,false);
	CHECK(f.sent==1 && f.wire[0]=='C');
}
static void failures_and_live_state(void)
{
	struct fixture f; init(&f); begin(&f,1,0); f.c.input.payload[0]=0x77;
	key(&f,127); key(&f,127); CHECK(f.c.input.count==65534);
	CHECK(st_big_collect_key(&f.r,&f.c,'A',&f.sink)==IO_ERROR_OUT_OF_RANGE && f.c.input.count==65535 && f.c.input.payload[0]==0x77);
	CHECK(st_big_collect_key(&f.r,&f.c,'A',&f.sink)==IO_ERROR_OUT_OF_RANGE && f.c.input.count==65535);
	begin(&f,1,1); f.fail_at=0;
	CHECK(st_big_collect_key(&f.r,&f.c,'B',&f.sink)==IO_ERROR_IO && f.c.input.count==1 && f.c.input.payload[1]=='B' && f.echoed==0);
	CHECK(st_big_collect_key(&f.r,&f.c,'B',&f.sink)==IO_ERROR_IO && f.calls==1);
	init(&f); begin(&f,1,1); f.fail_at=1;
	CHECK(st_big_collect_key(&f.r,&f.c,127,&f.sink)==IO_ERROR_IO && f.c.input.count==65535 && f.echoed==1 && f.echoes[0]==' ');
	init(&f); begin(&f,0,1); f.fail_at=0; f.failure=IO_ERROR_IN_PROGRESS;
	CHECK(st_big_collect_key(&f.r,&f.c,'A',&f.sink)==IO_ERROR_PROTOCOL && f.c.input.count==1 && f.r.active);
	init(&f); begin(&f,1,1); f.mutate=true; key(&f,'A'); CHECK(f.c.input.count==0 && f.c.input.payload[1]=='A');
	key(&f,'B'); CHECK(f.c.input.count==0 && f.c.input.payload[1]=='B' && f.echoed==2);
}
static void set_zone(struct fixture *f, size_t id, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint8_t *data, uint8_t length)
{
	uint16_t r[]={x1,y1,x2,y2}; memcpy(f->c.zones.rectangles[id],r,sizeof(r));
	size_t base=id*81; f->c.zones.payload[base]=length;
	if (length!=0) memcpy(f->c.zones.payload+base+1,data,length);
}
static void zones_and_replies(void)
{
	struct fixture f; init(&f); begin(&f,2,0); bool retry=true;
	CHECK(st_big_collect_zone(&f.r,&f.c,0,0,&f.sink,&retry)==0 && !retry && !f.r.active && f.c.input.count==0);
	init(&f); static const uint8_t bytes[]={0,'|','~'};
	set_zone(&f,0,10,10,20,20,bytes,3); set_zone(&f,1,20,10,30,20,(const uint8_t *)"B",1);
	begin(&f,2,0); CHECK(st_big_collect_zone(&f.r,&f.c,20,15,&f.sink,&retry)==0 && !retry && f.c.input.count==3 && memcmp(f.c.input.payload+1,bytes,3)==0);
	finish(&f,false); CHECK(f.sent==3 && memcmp(f.wire,bytes,3)==0);
	init(&f); set_zone(&f,47,65535,65535,65535,65535,(const uint8_t *)"Z",1);
	begin(&f,2,0); CHECK(st_big_collect_zone(&f.r,&f.c,318,98,&f.sink,&retry)==0 && !retry && f.c.input.count==1 && f.c.input.payload[1]=='Z');
	begin(&f,2,0); f.c.zones.loopback=2;
	CHECK(st_big_collect_zone(&f.r,&f.c,318,98,&f.sink,&retry)==0 && retry && f.r.active && f.echoed==1 && f.echoes[0]==7 && f.c.input.count==0 && f.c.input.payload[1]=='Z');
	set_zone(&f,0,65535,65535,65535,65535,(const uint8_t *)"A",1);
	CHECK(st_big_collect_zone(&f.r,&f.c,65535,65535,&f.sink,&retry)==0 && !retry && f.c.input.count==1 && f.c.input.payload[1]=='A');
	init(&f); set_zone(&f,0,20,20,10,10,(const uint8_t *)"A",1); begin(&f,2,0);
	CHECK(st_big_collect_zone(&f.r,&f.c,15,15,&f.sink,&retry)==0 && !retry && f.c.input.count==0);
	init(&f); f.c.zones.payload[0]=255;
	for (size_t i=1;i<=255;++i) f.c.zones.payload[i]=(uint8_t)i;
	begin(&f,2,0); CHECK(st_big_collect_zone(&f.r,&f.c,0,0,&f.sink,&retry)==0 && f.c.input.count==255);
	for (size_t i=1;i<=255;++i) CHECK(f.c.input.payload[i]==(uint8_t)i);
	finish(&f,false); CHECK(f.sent==255 && f.wire[254]==255);
	init(&f); f.c.zones.payload[47*81]=255; memset(f.c.input.payload,0x77,sizeof(f.c.input.payload));
	for (size_t i=1;i<=80;++i) f.c.zones.payload[47*81+i]=(uint8_t)i;
	begin(&f,2,0);
	CHECK(st_big_collect_zone(&f.r,&f.c,318,98,&f.sink,&retry)==IO_ERROR_OUT_OF_RANGE && f.c.input.count==255 && f.r.active);
	for (size_t i=1;i<=80;++i) CHECK(f.c.input.payload[i]==(uint8_t)i);
	CHECK(f.c.input.payload[81]==0x77);
	CHECK(st_big_collect_zone(&f.r,&f.c,318,98,&f.sink,&retry)==IO_ERROR_OUT_OF_RANGE);
	init(&f); begin(&f,2,0); f.c.zones.loopback=1; f.fail_at=0;
	CHECK(st_big_collect_zone(&f.r,&f.c,318,98,&f.sink,&retry)==IO_ERROR_IO && !retry && f.r.active);
}
static void cancel_and_invalid_calls(void)
{
	struct fixture f; init(&f); begin(&f,1,0); key(&f,'A');
	f.c.input.append_cr=0x7f;
	unsigned char before[sizeof(f.c)]; memcpy(before,&f.c,sizeof(before));
	st_big_collect_cancel(&f.r);
	CHECK(!f.r.active && f.r.error==0 && memcmp(&f.c,before,sizeof(before))==0);
	CHECK(st_big_collect_key(&f.r,&f.c,'B',&f.sink)==IO_ERROR_INVALID_ARGUMENT && memcmp(&f.c,before,sizeof(before))==0);
	CHECK(st_big_collect_begin(&f.r,NULL,1,0)==IO_ERROR_INVALID_ARGUMENT && !f.r.active);
	begin(&f,1,0); CHECK(f.c.input.count==0 && f.c.input.payload[1]=='A' && f.c.input.append_cr==0x7f);
	CHECK(st_big_collect_key(&f.r,&f.c,'B',NULL)==IO_ERROR_INVALID_ARGUMENT && f.c.input.count==0);
	begin(&f,2,0); CHECK(st_big_collect_key(&f.r,&f.c,'B',&f.sink)==IO_ERROR_INVALID_ARGUMENT && f.c.input.count==0);
	CHECK(st_big_collect_zone(&f.r,&f.c,0,0,&f.sink,NULL)==IO_ERROR_INVALID_ARGUMENT && f.r.active);
	st_big_collect_cancel(NULL);
}
static void movement(void)
{
	static const struct { uint16_t x,y,sx,sy; unsigned direction; uint16_t ex,ey; } cases[]={
		{318,98,12,6,1,318,92},{318,98,12,6,2,318,104},{318,98,12,6,3,330,98},{318,98,12,6,4,306,98},
		{639,479,12,6,3,639,479},{639,479,12,6,2,639,479},{2,2,12,6,1,2,0},{2,2,12,6,4,0,2},
		{0,0,65535,65535,1,0,1},{0,0,65535,65535,2,0,65535},{0,0,65535,65535,3,65535,0},{0,0,65535,65535,4,1,0},
		{0,0,32768,32768,3,32768,0},{0,0,32768,32768,4,0,0},{65535,65535,1,6,3,0,65535},{65535,65535,1,6,2,65535,5},
		{32769,3,1,6,4,0,3},{65535,65535,1,1,0,65535,65535},{65535,65535,1,1,5,65535,65535},
	};
	struct fixture f; init(&f);
	for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
		f.c.pointer.step_x=cases[i].sx; f.c.pointer.step_y=cases[i].sy;
		uint16_t x=cases[i].x, y=cases[i].y;
		st_big_collect_move(&f.c,639,479,cases[i].direction,&x,&y); CHECK(x==cases[i].ex && y==cases[i].ey);
	}
}
int main(void) { all_types_and_options(); keyboard_counts_and_replies(); failures_and_live_state(); zones_and_replies(); cancel_and_invalid_calls(); movement(); return 0; }
