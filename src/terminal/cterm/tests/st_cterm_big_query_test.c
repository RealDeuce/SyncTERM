/* Independent BIG ? bytes, readiness, source and lifetime expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[512], requests[64]; enum st_cterm_output_origin origins[512]; size_t size, requested;
	unsigned ready_calls,attempts,fail_at,mutation; bool block_ready,block_send,reset_ready,reset_send; int ready_error;
	int (*native_send)(void *, enum st_big_output_kind, uint8_t); };
static uint64_t local_ns;
static int clock_error;
static unsigned clocks;
static int now(void *context, uint64_t *value) { (void)context; ++clocks; *value=local_ns; return clock_error; }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; CHECK(size<=sizeof(f->output)-f->size);
	for (size_t i=0;i<size;++i) f->origins[f->size+i]=origin;
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_error=0; clocks=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f};
	CHECK(st_cterm_create(&f->terminal,&config)==0); f->terminal->big_engine->numeric.io.now_ns=now;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static int ready(void *context, enum st_big_output_kind kind)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	if (kind!=ST_BIG_OUTPUT_QUERY) return 0;
	++f->ready_calls;
	if (f->reset_ready) { f->reset_ready=false; CHECK(st_cterm_reset_terminal(f->terminal)==0); }
	return f->ready_error?f->ready_error:f->block_ready?IO_ERROR_IN_PROGRESS:0;
}
static int send(void *context, enum st_big_output_kind kind, uint8_t byte)
{
	struct cterm_big *b=context; struct fixture *f=b->terminal->api_output_context;
	if (kind!=ST_BIG_OUTPUT_QUERY) return f->native_send(context,kind,byte);
	++f->attempts; CHECK(f->requested<sizeof(f->requests)); f->requests[f->requested++]=byte;
	if (f->block_send) return IO_ERROR_IN_PROGRESS;
	if (f->attempts==f->fail_at) return IO_ERROR_IO;
	int status=f->native_send(context,kind,byte);
	if (f->mutation==1 && f->attempts==4) b->feature_gates[3][1]=1;
	if (f->mutation==2 && f->attempts==4) b->feature_gates[3][1]=b->feature_gates[3][0];
	if (f->mutation==3 && f->attempts==1) { b->query.selector=1; b->numeric.context.resolution=255; }
	if (f->mutation==4 && f->attempts==1) { b->query.selector=2; b->music_memory=65535; }
	if (f->reset_send) { f->reset_send=false; CHECK(st_cterm_reset_terminal(f->terminal)==0); }
	return status;
}
static void instrument(struct fixture *f)
{
	struct cterm_big *b=f->terminal->big_engine; f->native_send=b->control_io.send;
	b->control_io.ready=ready; b->control_io.send=send;
}
static void begin(struct fixture *f, uint16_t selector)
{
	struct st_graphics_argument arg={.has_value=true,.value=selector};
	const struct st_graphics_command_view view={.code='?',.arguments=&arg,.count=1};
	CHECK(cterm_big_query_begin(f->terminal->big_engine,&view)==IO_ERROR_IN_PROGRESS);
}
static void run(struct fixture *f)
{
	size_t budget=64; bool complete=false;
	CHECK(cterm_big_query_poll(f->terminal->big_engine,&budget,&complete)==0 && complete);
}
static void setup(struct fixture *f, unsigned resolution)
{
	char wire[128]; snprintf(wire,sizeof(wire),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",resolution);
	string(f,wire); f->size=0; f->terminal->big_engine->state.bgi.error=-11;
}
static void canvas(struct fixture *f)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	struct st_pixel_buffer *pixels=NULL; const struct st_console_rect area={0,0,width,height};
	CHECK(st_console_get_pixels(&pixels,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)width*height;++i)
		CHECK(pixels->primary[i]==((i==100u*width+100u || i==100u*width+120u)?805u:800u) && pixels->alternate[i]==pixels->primary[i]);
	st_pixel_buffer_destroy(pixels);
}
static void partitions_and_owners(void)
{
	static const char wire[]="I#?>0:?>1:?>2:? 9:\033[6n";
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (size_t split=0;split<=sizeof(wire);++split) {
			setup(&f,res); struct st_big_state state; memcpy(&state,&b->state,sizeof(state)); state.memory_page=1; state.active_page=state.visual_page=0;
			state.local_palette[0]=state.bgi.palette_size; memcpy(state.local_palette+1,state.bgi.palette,16);
			struct st_big_image image; memcpy(&image,&b->image,sizeof(image)); uint8_t bytes[10]; memcpy(bytes,b->image_bytes,10);
			if (split==sizeof(wire)) for (size_t i=0;i<sizeof(wire)-1;++i) feed(&f,wire+i,1);
			else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-1-split); }
			char expected[]="1.03R2:0:\033[1;1R"; expected[5]=(char)('0'+res);
			CHECK(f.size==sizeof(expected)-1 && memcmp(f.output,expected,f.size)==0);
			for (size_t i=0;i<f.size;++i) CHECK(f.origins[i]==ST_CTERM_OUTPUT_RESPONSE);
			CHECK(b->query.phase==CTERM_BIG_QUERY_OFF && !st_cterm_input_paused(f.terminal) && b->query.selector==9 && b->music_memory==0);
			CHECK(memcmp(&state,&b->state,sizeof(state))==0 && memcmp(&image,&b->image,sizeof(image))==0 && memcmp(bytes,b->image_bytes,10)==0);
			CHECK(b->last_error==0 && b->rejected_commands==0); canvas(&f);
		}
		destroy(&f);
	}
}
static void raw_words_and_current_reads(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; instrument(&f);
	for (unsigned selector=0;selector<=65535;++selector) {
		f.size=f.requested=f.attempts=f.ready_calls=0; begin(&f,(uint16_t)selector); run(&f);
		CHECK(f.ready_calls==1 && f.size==(selector==0?5u:selector<=2?2u:0u));
	}
	for (unsigned value=0;value<=65535;++value) {
		f.size=f.requested=f.attempts=0; b->music_memory=(uint16_t)value; begin(&f,2); run(&f);
		CHECK(f.size==2 && f.output[0]==(uint8_t)((value+48)%256) && f.output[1]==':');
		if (value<256) {
			f.size=f.requested=f.attempts=0; b->numeric.context.resolution=(uint8_t)value; begin(&f,1); run(&f);
			CHECK(f.size==2 && f.output[0]==(uint8_t)((value+48)%256) && f.output[1]==':');
		}
	}
	b->numeric.context.resolution=2; b->music_memory=0;
	for (unsigned mutation=1;mutation<=4;++mutation) {
		f.size=f.requested=f.attempts=0; f.mutation=mutation; b->numeric.context.resolution=2;
		b->feature_gates[3][0]=0; b->feature_gates[3][1]=mutation==2?1:0;
		begin(&f,mutation==4?1:0); run(&f);
		const char *expected=mutation==1?"1.03":mutation==2?"1.03R":mutation==3?"1.03R/:":"2:/:";
		CHECK(f.size==strlen(expected) && memcmp(f.output,expected,f.size)==0);
	}
	f.mutation=0; f.size=f.requested=f.attempts=0; b->feature_gates[3][1]=0; f.fail_at=3;
	begin(&f,0); run(&f);
	CHECK(f.requested==5 && memcmp(f.requests,"1.03R",5)==0 && f.size==4 && memcmp(f.output,"1.3R",4)==0 && b->rejected_commands==0);
	destroy(&f);
}
static void readiness_budget_and_retry(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; instrument(&f);
	string(&f,"I#? "); CHECK(f.ready_calls==0 && f.size==0); f.block_ready=true; string(&f,"9:");
	CHECK(st_cterm_input_paused(f.terminal) && f.ready_calls==1 && f.size==0);
	unsigned before=clocks; local_ns+=UINT64_C(1000000000000); poll(&f);
	CHECK(st_cterm_input_paused(f.terminal) && clocks==before && f.ready_calls==2);
	CHECK(st_cterm_handle_key(f.terminal,'K',0) && f.size==1 && f.output[0]=='K'); /* Query has no key cancellation. */
	f.block_ready=false; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && f.size==1);
	begin(&f,65535); size_t empty=0; bool finished=false; CHECK(cterm_big_query_poll(b,&empty,&finished)==0 && finished && empty==0 && f.size==1);
	f.size=f.requested=f.attempts=f.ready_calls=0; begin(&f,0);
	size_t budget=0; bool complete;
	CHECK(cterm_big_query_poll(b,&budget,&complete)==IO_ERROR_IN_PROGRESS && !complete && f.ready_calls==1 && f.attempts==0);
	budget=2; CHECK(cterm_big_query_poll(b,&budget,&complete)==IO_ERROR_IN_PROGRESS && f.size==2 && memcmp(f.output,"1.",2)==0);
	CHECK(f.ready_calls==1); f.block_send=true; budget=4;
	CHECK(cterm_big_query_poll(b,&budget,&complete)==IO_ERROR_IN_PROGRESS && b->query.byte=='0' && f.size==2);
	f.block_send=false; CHECK(cterm_big_query_poll(b,&budget,&complete)==0 && complete && f.size==5 && memcmp(f.output,"1.03R",5)==0);
	CHECK(f.ready_calls==1 && f.requested==6 && memcmp(f.requests,"1.003R",6)==0);
	f.size=f.requested=f.attempts=0; b->numeric.context.resolution=2; begin(&f,1); f.block_send=true; budget=4;
	CHECK(cterm_big_query_poll(b,&budget,&complete)==IO_ERROR_IN_PROGRESS && b->query.byte=='2');
	b->numeric.context.resolution=255; f.block_send=false; run(&f); CHECK(f.size==2 && memcmp(f.output,"2:",2)==0);
	CHECK(st_cterm_finish(f.terminal)==0); destroy(&f);
}
static void cancellation_sources_and_replies(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine; instrument(&f);
	b->music_memory=65535; string(&f,"I#I 0:I#R 1:I#? 2:"); CHECK(f.size==2 && memcmp(f.output,"/:",2)==0 && b->music_memory==65535);
	f.size=0; f.block_ready=true; string(&f,"I#? 0:"); CHECK(st_cterm_finish(f.terminal)==0 && !st_cterm_input_paused(f.terminal) && f.size==0 && b->music_memory==65535);
	string(&f,"I#? 0:"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && !st_cterm_input_paused(f.terminal) && f.size==0 && b->music_memory==65535);
	f.block_ready=false; f.ready_error=IO_ERROR_IO; string(&f,"I#? 0:\033[6n");
	CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); f.size=0;
	CHECK(b->last_error==IO_ERROR_IO && b->rejected_commands==1 && f.size==0 && !st_cterm_input_paused(f.terminal));
	f.ready_error=0; f.reset_ready=true; string(&f,"I#? 0:"); CHECK(!st_cterm_input_paused(f.terminal) && f.size==0 && b->rejected_commands==1);
	f.reset_send=true; string(&f,"I#? 0:"); CHECK(f.size==1 && f.output[0]=='1' && b->query.phase==CTERM_BIG_QUERY_OFF && b->rejected_commands==1);
	f.size=f.requested=f.attempts=0; string(&f,"I#? "); clock_error=IO_ERROR_IO; poll(&f); clock_error=0;
	CHECK(f.size==0 && b->last_error==IO_ERROR_IO && b->rejected_commands==2);
	for (unsigned reset=0;reset<2;++reset) {
		f.size=f.requested=f.attempts=0; begin(&f,0); size_t budget=2; bool complete=false;
		CHECK(cterm_big_query_poll(b,&budget,&complete)==IO_ERROR_IN_PROGRESS && f.size==2 && memcmp(f.output,"1.",2)==0);
		CHECK((reset?st_cterm_reset_terminal(f.terminal):st_cterm_finish(f.terminal))==0);
		CHECK(!st_cterm_input_paused(f.terminal) && f.size==2 && b->music_memory==65535);
	}
	f.size=0; string(&f,"I#? "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f); CHECK(f.size==5 && memcmp(f.output,"1.03R",5)==0);
	f.size=f.requested=f.attempts=0; string(&f,"I#"); b->control.input.pending=1; b->control.input.count=2; b->control.input.payload[1]=0; b->control.input.payload[2]=255;
	string(&f,"?>0:? 1:\033[6n"); CHECK(f.size==15 && memcmp(f.output,"1.03R1:\0\xff\033[1;1R",15)==0);
	for (size_t i=0;i<f.size;++i) CHECK(f.origins[i]==(i==7 || i==8?ST_CTERM_OUTPUT_USER:ST_CTERM_OUTPUT_RESPONSE));
	f.size=f.requested=f.attempts=0; b->music_memory=0;
	string(&f,"I#g 3:I#& 0,3,1,0,?@1,x:"); CHECK(f.size==9 && memcmp(f.output,"1.03R1:0:",9)==0 && b->numeric.loop.active==0);
	f.size=f.requested=f.attempts=0; string(&f,"I#? +1:\033[6n"); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
	destroy(&f);
}
int main(void) { partitions_and_owners(); raw_words_and_current_reads(); readiness_budget_and_retry(); cancellation_sources_and_replies(); return 0; }
