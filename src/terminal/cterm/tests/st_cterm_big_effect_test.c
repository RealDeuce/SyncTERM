/* BIG b native selector, input, shared RNG, PCM and lifecycle expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "io_error.h"
#include "cterm_big_audio.h"
#include "st_audio_runtime.h"
#include "st_threads.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; uint8_t output[128]; size_t size; };
static uint64_t local_ns;
static unsigned clock_calls,reads;
static int clock_error,read_error,peek_error;
static bool reset_on_clock,reset_on_read;
static int now(void *context, uint64_t *value) { (void)context; ++clock_calls; *value=local_ns; if (reset_on_clock) { reset_on_clock=false; CHECK(st_cterm_reset_terminal(((struct cterm_big *)context)->terminal)==0); } return clock_error; }
static int read_key(void *context, uint8_t *value) { ++reads; if (reset_on_read) { reset_on_read=false; CHECK(st_cterm_reset_terminal(((struct cterm_big *)context)->terminal)==0); } return read_error?read_error:cterm_big_input_read(context,value); }
static int peek_key(void *context, bool *value) { return peek_error?peek_error:cterm_big_input_peek(context,value); }
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct fixture *f=context; (void)origin; CHECK(size<=sizeof(f->output)-f->size);
	memcpy(f->output+f->size,bytes,size); f->size+=size;
}
static void create(struct fixture *f, bool utf8, bool status)
{
	memset(f,0,sizeof(*f)); local_ns=1000; clock_calls=reads=0; clock_error=read_error=peek_error=0; reset_on_clock=reset_on_read=false;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.emulation=ST_CTERM_ANSI_BBS,
	    .graphics_mode=ST_CTERM_GRAPHICS_BIG_103,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.quiet=true,.status_line=status,.scrollback_lines=2,.output=output,.output_context=f,
	    .apc_directory="."};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.read_key=read_key; f->terminal->big_engine->numeric.io.peek_key=peek_key;
}
static void destroy(struct fixture *f) { st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console); }
static void feed(struct fixture *f, const void *bytes, size_t size) { size_t used; int status=st_cterm_write(f->terminal,bytes,size,&used); CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used==size); }
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void key(struct fixture *f, struct cterm_key k) { CHECK(cterm_big_handle_key(f->terminal->big_engine,&k)); }
static size_t write_some(struct fixture *f, const char *wire)
{
	size_t used; int status=st_cterm_write(f->terminal,wire,strlen(wire),&used);
	CHECK(status==0 || status==IO_ERROR_IN_PROGRESS); return used;
}
static struct st_big_speaker voice(struct fixture *f)
{
	struct st_big_speaker s; CHECK(cterm_big_speaker_snapshot(f->terminal->big_engine,&s)==0); return s;
}
static const unsigned delay_ms[]={120,120,900,930,150,1104,430,0,1440,560,1092,175,324,495,800,175,2750,4000,192,400};
static const unsigned frequency_count[]={24,24,190,10,50,276,86,32700,80,80,156,100,324,66,160,3,500,1000,64,30};
static size_t duration(unsigned selector) { return delay_ms[selector]*441/10+frequency_count[selector]; }
static void advance(struct fixture *f, uint64_t frames)
{
	local_ns+=(frames*UINT64_C(1000000000)+44099)/44100; poll(f);
}
static void canvas(struct fixture *f)
{
	uint32_t width,height; CHECK(st_console_pixel_size(f->console,&width,&height)==0);
	struct st_pixel_buffer *p=NULL; const struct st_console_rect area={0,0,width,height}; CHECK(st_console_get_pixels(&p,f->console,&area,false)==0);
	for (size_t i=0;i<(size_t)width*height;++i) CHECK(p->primary[i]==(i==100u*width+100u?805u:800u) && p->alternate[i]==p->primary[i]);
	st_pixel_buffer_destroy(p);
}
static void splits_and_scenes(void)
{
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (unsigned selector=0;selector<20;++selector) {
			char wire[24]; snprintf(wire,sizeof(wire),"I#b %u:",selector); size_t length=strlen(wire);
			unsigned partitions=selector==0 || selector==7 || selector==19?(unsigned)length+2:1;
			for (unsigned split=0;split<partitions;++split) {
				CHECK(st_cterm_finish(f.terminal)==0);
				char setup[80]; snprintf(setup,sizeof(setup),"I#R %u:I#C>0,5:P 100,100:",res); string(&f,setup);
				struct st_big_state before=b->state; before.memory_page=1; before.local_palette[0]=before.bgi.palette_size; memcpy(before.local_palette+1,before.bgi.palette,16);
				b->numeric.random_state=1;
				if (split==length+1) for (size_t i=0;i<length;++i) feed(&f,wire+i,1);
				else { size_t cut=partitions==1?length:split; feed(&f,wire,cut); feed(&f,wire+cut,length-cut); }
				CHECK(b->effect.active && b->effect.selector==selector && st_cterm_input_paused(f.terminal));
				CHECK(write_some(&f,"\033[6n")==0 && f.size==0 && reads==0);
				advance(&f,duration(selector)-1); CHECK(b->effect.active && st_cterm_input_paused(f.terminal));
				advance(&f,1); CHECK(!b->effect.active && !st_cterm_input_paused(f.terminal) && !voice(&f).playing);
				CHECK(b->last_error==0 && memcmp(&before,&b->state,sizeof(before))==0); canvas(&f);
				CHECK(b->numeric.random_state==(selector==7?UINT32_C(0x11bbdc95):1));
				string(&f,"\033[6n"); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); f.size=0;
			}
		}
		destroy(&f);
	}
}
static void gates_sources_and_keys(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#b "); CHECK(!st_cterm_input_paused(f.terminal)); b->feature_gates[3][1]=1;
	string(&f,"18:"); CHECK(b->effect.selector==9); advance(&f,duration(9)); CHECK(!b->effect.active);
	b->feature_gates[3][1]=0; string(&f,"I#b +1:"); CHECK(b->effect.selector==19); advance(&f,duration(19));
	string(&f,"I#b 9999:"); CHECK(b->effect.selector==19); advance(&f,duration(19));
	string(&f,"I#n 0,440,0,0:"); b->effect_sound_on=0; struct st_big_speaker held=voice(&f);
	string(&f,"I#b 7:"); CHECK(!b->effect.active && voice(&f).playing && voice(&f).frequency==440 && voice(&f).phase==held.phase);
	b->effect_sound_on=255; string(&f,"I#b 3:"); b->effect_sound_on=0;
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='A'}); advance(&f,duration(3)-1);
	CHECK(b->effect.active && reads==0 && f.size==0); advance(&f,1);
	CHECK(!b->effect.active && f.size==1 && f.output[0]=='A'); f.size=0; b->effect_sound_on=1;
	string(&f,"I#g 3:I#& 0,2,1,0,b@1,15:"); CHECK(b->effect.selector==15 && b->numeric.loop.active==1);
	/* The second source read reaches TO before its effect finishes. */
	advance(&f,duration(15)); CHECK(b->effect.active && b->numeric.loop.current==2 && b->numeric.loop.active==0);
	advance(&f,duration(15)); CHECK(!b->effect.active && b->numeric.loop.active==0); string(&f,"I#g 0:");
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"b>15:"); advance(&f,duration(15)); CHECK(b->control.input.pending==1 && f.size==0);
	string(&f,"S 5,7:\033[6n"); CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0);
	destroy(&f);
}
static struct st_big_effect_player player(struct fixture *f)
{
	struct st_big_effect_player p; CHECK(cterm_big_effect_snapshot(f->terminal->big_engine,&p)==0); return p;
}
static void rng_prefix_and_errors(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	b->numeric.random_state=1; string(&f,"I#b 7:"); CHECK(b->numeric.random_state==0x015a4e36 && voice(&f).frequency==5536);
	advance(&f,1); CHECK(b->numeric.random_state==0x8082a52f && voice(&f).frequency==2080);
	CHECK(st_cterm_finish(f.terminal)==0 && !b->effect.active && !voice(&f).playing && b->numeric.random_state==0x8082a52f);
	string(&f,"I#X 2,0,2:"); /* Completed effect no longer owns/overwrites numeric RNG. */
	b->numeric.random_state=123; poll(&f); CHECK(b->numeric.random_state==123);
	string(&f,"I#b 7:"); uint32_t prefix=b->numeric.random_state; clock_error=IO_ERROR_IO;
	string(&f,"\033[6n"); CHECK(!b->effect.active && !player(&f).active && voice(&f).playing && b->numeric.random_state==prefix);
	CHECK(b->last_error==IO_ERROR_IO && f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); clock_error=0; f.size=0;
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && !voice(&f).playing);
	string(&f,"I#b 15:"); --local_ns; poll(&f); CHECK(!b->effect.active && b->last_error==IO_ERROR_OUT_OF_RANGE);
	string(&f,"I#b 15:"); reset_on_clock=true; poll(&f); CHECK(!b->effect.active && !player(&f).active && !voice(&f).playing);
	destroy(&f);
}
static st_audio_runtime_t *attach(struct fixture *f)
{
	st_audio_runtime_t *r=NULL; CHECK(st_audio_runtime_create_pull(&r)==0); f->terminal->screen.audio=r; return r;
}
static void pcm_mute_and_flush(void)
{
	struct fixture f; create(&f,false,false); st_audio_runtime_t *r=attach(&f); struct cterm_big *b=f.terminal->big_engine;
	b->numeric.random_state=1; string(&f,"I#b 7:"); CHECK(f.terminal->music_stream==NULL && f.terminal->fx_stream!=NULL);
	int16_t pcm[256]; CHECK(st_audio_runtime_pull(r,pcm,1)==0); poll(&f);
	CHECK(pcm[0]==1147 && pcm[1]==1147); /* Fixed speaker sample4574 through -12dB and tanh. */
	CHECK(b->numeric.random_state==0x8082a52f && voice(&f).frequency==2080 && player(&f).sequence.position==3);
	/* Unheard progression borrows the same RNG and source state, without
	 * replaying buffered requests when physical output becomes available. */
	f.terminal->screen.audio=NULL; poll(&f); advance(&f,32699);
	CHECK(!b->effect.active && !player(&f).active && b->numeric.random_state==0x11bbdc95 && !voice(&f).playing);
	f.terminal->screen.audio=r; poll(&f); CHECK(audio_stream_is_idle(f.terminal->fx_stream));
	CHECK(st_audio_runtime_pull(r,pcm,128)==0); for (unsigned i=0;i<256;++i) CHECK(pcm[i]==0);
	string(&f,"I#b 3:"); CHECK(st_audio_runtime_pull(r,pcm,128)==0); poll(&f);
	CHECK(player(&f).active && voice(&f).frequency==660);
	CHECK(write_some(&f,"I#b 3:")==0); /* No following command may be consumed during b. */
	CHECK(audio_stream_fade_source(f.terminal->fx_stream,4)==0 && st_audio_runtime_pull(r,pcm,8)==0);
	poll(&f); CHECK(!b->effect.active && !player(&f).active && !voice(&f).playing);
	string(&f,"I#b 3:"); CHECK(st_cterm_finish(f.terminal)==0 && !player(&f).active && audio_stream_is_idle(f.terminal->fx_stream));
	string(&f,"I#b 3:"); st_audio_runtime_stop(r); poll(&f); advance(&f,duration(3));
	CHECK(!b->effect.active && !voice(&f).playing);
	destroy(&f); st_audio_runtime_destroy(r);
}
struct consumer { st_audio_runtime_t *runtime; atomic_uint requests; atomic_bool stop,ready; };
static int consume(void *context)
{
	struct consumer *c=context; int16_t pcm[128]; unsigned completed=0; atomic_store(&c->ready,true);
	while (!atomic_load(&c->stop)) {
		if (completed<atomic_load(&c->requests)) { CHECK(st_audio_runtime_pull(c->runtime,pcm,64)==0); ++completed; }
		else st_thread_yield();
	}
	return 0;
}
static void concurrent_reset(void)
{
	struct fixture f; create(&f,false,false); st_audio_runtime_t *r=attach(&f);
	struct consumer c={.runtime=r}; atomic_init(&c.requests,0); atomic_init(&c.stop,false); atomic_init(&c.ready,false);
	st_thread_t thread; CHECK(st_thread_create(&thread,consume,&c)==ST_THREAD_SUCCESS);
	while (!atomic_load(&c.ready)) st_thread_yield();
	for (unsigned i=0;i<50;++i) {
		string(&f,"I#b 7:"); atomic_fetch_add(&c.requests,1); poll(&f);
		CHECK(st_cterm_reset_terminal(f.terminal)==0);
		struct cterm_big *b=f.terminal->big_engine; uint32_t rng=b->numeric.random_state;
		CHECK(!b->effect.active && !player(&f).active && !voice(&f).playing); poll(&f); CHECK(b->numeric.random_state==rng);
	}
	string(&f,"I#b 7:"); atomic_fetch_add(&c.requests,1); destroy(&f);
	atomic_store(&c.stop,true); int result; CHECK(st_thread_join(thread,&result)==ST_THREAD_SUCCESS && result==0);
	st_audio_runtime_destroy(r);
}
int main(void)
{
	splits_and_scenes(); gates_sources_and_keys(); rng_prefix_and_errors(); pcm_mute_and_flush(); concurrent_reset(); return 0;
}
