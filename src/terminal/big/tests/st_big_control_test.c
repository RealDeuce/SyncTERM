/* Independent footer/flow expectations from original BIG21/32/33/43. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_control.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture {
	struct st_big_control c; struct st_big_loop loop; struct st_big_footer footer;
	struct st_big_control_sink sink;
	uint8_t bytes[512]; size_t sent, ready, attempts;
	bool block_ready, block_send; int ready_error, send_error, mutation, uniform;
};
static int ready(void *context, enum st_big_output_kind kind)
{
	struct fixture *f=context; ++f->ready;
	if (kind==ST_BIG_OUTPUT_REPLY) CHECK(f->c.input.pending==0);
	if (f->block_ready) return IO_ERROR_IN_PROGRESS;
	if (f->ready_error!=0) return f->ready_error;
	if (f->mutation==2) { f->c.input.count=0; f->c.input.payload[1]='Q'; }
	return 0;
}
static int send(void *context, enum st_big_output_kind kind, uint8_t byte)
{
	struct fixture *f=context; ++f->attempts;
	if (f->block_send) return IO_ERROR_IN_PROGRESS;
	if (f->send_error!=0) return f->send_error;
	if (f->uniform>=0) CHECK(byte==(uint8_t)f->uniform);
	if (f->sent<sizeof(f->bytes)) f->bytes[f->sent]=byte;
	++f->sent;
	if (f->mutation==4 && f->sent==1) { f->c.flow.count[0]=2; f->c.flow.byte[0]=41; f->c.flow.mode=0; }
	if (f->mutation==3 && kind==ST_BIG_OUTPUT_REPLY) { f->c.input.count=2; f->c.input.payload[2]='B'; f->c.input.append_cr=1; }
	return 0;
}
static void prepare(struct fixture *f)
{
	memset(f,0,sizeof(*f)); f->uniform=-1;
	st_big_control_init(&f->c); st_big_loop_init(&f->loop);
	f->sink=(struct st_big_control_sink){f,ready,send};
	f->c.frame.command='I'; f->c.frame.gang_marker='>'; f->loop.type='@';
}
static int footer(struct fixture *f, size_t *budget, enum st_big_footer_action *action)
{
	return st_big_footer_poll(&f->footer,&f->loop,&f->c,&f->sink,budget,action);
}
static void order_and_chaining(void)
{
	struct fixture f; prepare(&f); size_t budget=64;
	enum st_big_footer_action action=ST_BIG_FOOTER_RETURN;
	f.c.frame.did_loop=1; f.loop.active=1; f.loop.command_read=true;
	f.c.input.pending=1; st_big_footer_begin(&f.footer,true);
	CHECK(footer(&f,&budget,&action)==0 && action==ST_BIG_FOOTER_REPEAT);
	CHECK(f.c.frame.did_loop==1 && f.c.input.pending==1 && f.loop.type=='@' && f.c.frame.gang_marker=='>' && f.sent==0);
	/* No numeric/text call means no repeat, even with an active loop. */
	f.loop.command_read=false; st_big_footer_begin(&f.footer,true); budget=0;
	CHECK(footer(&f,&budget,&action)==IO_ERROR_IN_PROGRESS);
	CHECK(f.c.frame.did_loop==0 && f.loop.type=='@' && f.c.frame.gang_marker=='>' && f.c.input.pending==1);
	budget=1; CHECK(footer(&f,&budget,&action)==0 && action==ST_BIG_FOOTER_CHAIN);
	CHECK(f.sent==1 && f.bytes[0]==17 && f.loop.type==' ' && f.c.frame.gang_marker=='X');
	CHECK(f.c.input.pending==1 && f.c.frame.command=='I' && f.loop.active==1);
	/* Inherited loop state does not itself send finishing XON. */
	prepare(&f); f.loop.active=2; f.loop.command_read=true; f.c.frame.did_loop=2;
	st_big_footer_begin(&f.footer,false); budget=64;
	CHECK(footer(&f,&budget,&action)==0 && action==ST_BIG_FOOTER_RETURN);
	CHECK(f.sent==0 && f.c.frame.did_loop==2 && f.loop.active==2 && f.c.frame.command==0);
}
static void counted_replies(void)
{
	struct fixture f; prepare(&f); f.c.input.pending=7; f.c.input.count=4; f.c.input.append_cr=1;
	f.c.input.payload[0]='!'; memcpy(f.c.input.payload+1,(uint8_t[]){0,0x7c,0x7e,0xff},4);
	st_big_footer_begin(&f.footer,true); size_t budget=64; enum st_big_footer_action action;
	CHECK(footer(&f,&budget,&action)==0 && action==ST_BIG_FOOTER_CHAIN && f.sent==0 && f.c.input.pending==7);
	st_big_footer_begin(&f.footer,false); budget=0;
	CHECK(footer(&f,&budget,&action)==IO_ERROR_IN_PROGRESS && f.c.input.pending==0 && f.ready==0);
	for (unsigned i=0;i<5;++i) {
		budget=1; int status=footer(&f,&budget,&action);
		CHECK(status==(i==4?0:IO_ERROR_IN_PROGRESS));
	}
	CHECK(action==ST_BIG_FOOTER_RETURN && f.sent==5 && f.ready==4);
	CHECK(memcmp(f.bytes,(uint8_t[]){0,0x7c,0x7e,0xff,13},5)==0);
	CHECK(f.c.input.count==4 && f.c.input.append_cr==1 && f.c.input.payload[0]=='!');
	budget=64; CHECK(footer(&f,&budget,&action)==0 && f.sent==5); /* No duplicate sends. */
	prepare(&f); f.c.input.pending=1; f.c.input.append_cr=255;
	st_big_footer_begin(&f.footer,false); CHECK(footer(&f,&budget,&action)==0);
	CHECK(f.sent==1 && f.bytes[0]==13 && f.ready==0); /* No readiness before empty CR. */
	/* Independent unsigned interval oracle for signed WORD(count+1)>1. */
	for (unsigned count=0;count<=65535;++count) {
		struct st_big_control c; struct st_big_loop loop={0}; struct st_big_footer r;
		st_big_control_init(&c); c.input.pending=1; c.input.count=(uint16_t)count;
		st_big_footer_begin(&r,false); budget=0;
		int status=st_big_footer_poll(&r,&loop,&c,&f.sink,&budget,&action);
		CHECK(status==((count>=1 && count<=32766)?IO_ERROR_IN_PROGRESS:0) && c.input.pending==0);
	}
}
static void waits_and_live_state(void)
{
	struct fixture f; prepare(&f); struct st_big_flow_reader r;
	f.c.flow.count[0]=2; f.c.flow.byte[0]=31;
	st_big_flow_begin(&r,&f.c,true); f.block_send=true; size_t budget=64;
	CHECK(st_big_flow_poll(&r,&f.c,&f.sink,&budget)==IO_ERROR_IN_PROGRESS && f.ready==1);
	f.c.flow.byte[0]=41; f.c.flow.mode=0; f.block_send=false;
	CHECK(st_big_flow_poll(&r,&f.c,&f.sink,&budget)==0);
	CHECK(f.ready==2 && f.sent==2 && f.bytes[0]==31 && f.bytes[1]==41);
	prepare(&f); f.c.flow.byte[0]=31; f.mutation=4;
	st_big_flow_begin(&r,&f.c,true);
	CHECK(st_big_flow_poll(&r,&f.c,&f.sink,&budget)==0);
	CHECK(f.sent==2 && f.bytes[0]==31 && f.bytes[1]==41); /* Live count grew after first send. */
	prepare(&f); f.block_ready=true; st_big_flow_begin(&r,&f.c,true);
	CHECK(st_big_flow_poll(&r,&f.c,&f.sink,&budget)==IO_ERROR_IN_PROGRESS);
	f.c.flow.count[0]=0; f.block_ready=false;
	CHECK(st_big_flow_poll(&r,&f.c,&f.sink,&budget)==0 && f.sent==1 && f.bytes[0]==17);
	/* An admitted data iteration keeps waiting without retesting changed count. */
	prepare(&f); f.c.input.pending=1; f.c.input.count=3; f.c.input.append_cr=1;
	st_big_footer_begin(&f.footer,false); f.block_ready=true;
	enum st_big_footer_action action;
	CHECK(footer(&f,&budget,&action)==IO_ERROR_IN_PROGRESS && f.c.input.pending==0);
	f.block_ready=false; f.mutation=2;
	CHECK(footer(&f,&budget,&action)==0 && f.sent==2 && f.bytes[0]=='Q' && f.bytes[1]==13);
	/* Current count, data and CR are read after preceding requests return. */
	prepare(&f); f.c.input.pending=1; f.c.input.count=1; f.c.input.payload[1]='A'; f.mutation=3;
	st_big_footer_begin(&f.footer,false);
	CHECK(footer(&f,&budget,&action)==0 && f.sent==3 && memcmp(f.bytes,"AB\r",3)==0 && f.ready==2);
	prepare(&f); f.c.input.pending=1; f.c.input.count=1; f.c.input.payload[1]='A';
	st_big_footer_begin(&f.footer,false); f.block_send=true;
	CHECK(footer(&f,&budget,&action)==IO_ERROR_IN_PROGRESS && f.ready==1);
	f.c.input.payload[1]='Z'; f.block_send=false;
	CHECK(footer(&f,&budget,&action)==0 && f.sent==1 && f.bytes[0]=='A' && f.ready==1);
}
static void bounds_errors_and_flow(void)
{
	struct fixture f; prepare(&f); struct st_big_flow_reader r; enum st_big_footer_action action;
	for (unsigned count=0;count<=65535;++count) {
		struct st_big_control c; st_big_control_init(&c); c.flow.count[1]=(uint16_t)count;
		st_big_flow_begin(&r,&c,false); size_t budget=0;
		CHECK(st_big_flow_poll(&r,&c,&f.sink,&budget)==((count>=1 && count<=32767)?IO_ERROR_IN_PROGRESS:0));
	}
	f.c.flow.count[1]=32767; f.uniform=19; st_big_flow_begin(&r,&f.c,false);
	int status;
	do { size_t budget=64; status=st_big_flow_poll(&r,&f.c,&f.sink,&budget); } while (status==IO_ERROR_IN_PROGRESS);
	CHECK(status==0 && f.sent==32767 && f.ready==32767);
	prepare(&f); f.c.flow.mode=0; f.c.flow.count[0]=32767; st_big_flow_begin(&r,&f.c,true);
	size_t budget=64; CHECK(st_big_flow_poll(&r,&f.c,&f.sink,&budget)==0 && f.ready==0);
	prepare(&f); f.c.input.pending=1; f.c.input.count=256;
	st_big_footer_begin(&f.footer,false); budget=1000;
	CHECK(footer(&f,&budget,&action)==IO_ERROR_OUT_OF_RANGE && f.sent==255 && f.ready==256 && f.c.input.pending==0);
	f.c.input.count=0; CHECK(footer(&f,&budget,&action)==IO_ERROR_OUT_OF_RANGE && f.sent==255);
	prepare(&f); f.c.frame.did_loop=1; f.send_error=IO_ERROR_IO;
	st_big_footer_begin(&f.footer,false); CHECK(footer(&f,&budget,&action)==IO_ERROR_IO);
	CHECK(f.c.frame.did_loop==0 && f.loop.type=='@' && f.c.frame.gang_marker=='>' && f.sent==0);
	prepare(&f); f.c.input.pending=1; f.c.input.count=1; f.ready_error=IO_ERROR_IO;
	st_big_footer_begin(&f.footer,false); CHECK(footer(&f,&budget,&action)==IO_ERROR_IO && f.c.input.pending==0);
	/* New frame initializes local bytes, retaining application reply/flow owners. */
	f.c.input.payload[9]=77; f.c.flow.count[0]=19; memset(f.c.frame.gang,0xaa,128);
	st_big_control_enter(&f.c); CHECK(f.c.input.payload[9]==77 && f.c.flow.count[0]==19 && f.c.frame.gang[127]==0 && f.c.frame.gang_marker=='X');
}
int main(void)
{
	order_and_chaining(); counted_replies(); waits_and_live_state(); bounds_errors_and_flow(); return 0;
}
