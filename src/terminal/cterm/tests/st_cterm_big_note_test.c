/* Independent n note/sweep, backpressure, PCM and lifecycle expectations. */
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
static void note_and_sweeps(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#n 0,440,10,0:"); CHECK(st_cterm_input_paused(f.terminal));
	CHECK(voice(&f).playing && voice(&f).frequency==440 && voice(&f).divisor==2711);
	CHECK(write_some(&f,"\033[6n")==0 && f.size==0 && reads==0);
	local_ns+=9999999; poll(&f); CHECK(st_cterm_input_paused(f.terminal));
	++local_ns; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && voice(&f).playing);
	struct st_big_speaker s=voice(&f); CHECK(s.phase==(uint32_t)(s.step*441));
	string(&f,"\033[6n"); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); f.size=0;
	string(&f,"I#n 0,18,0,0:I#n 0,+1,0,0:I#n 2:"); CHECK(voice(&f).playing && voice(&f).frequency==440);
	string(&f,"I#n 1,100,150,30,2,5,0:"); CHECK(voice(&f).frequency==100);
	local_ns+=2000000; poll(&f); CHECK(voice(&f).frequency==130 && b->note.active);
	local_ns+=2000000; poll(&f); CHECK(voice(&f).frequency==130 && b->note.active);
	local_ns+=5000000; poll(&f); CHECK(voice(&f).frequency==130 && !b->note.active);
	string(&f,"I#n 1,150,100,30,2,5,0:"); CHECK(voice(&f).frequency==150);
	local_ns+=2000000; poll(&f); CHECK(voice(&f).frequency==120);
	local_ns+=2000000; poll(&f); CHECK(voice(&f).frequency==120 && b->note.active);
	local_ns+=5000000; poll(&f); CHECK(voice(&f).frequency==120 && !b->note.active);
	string(&f,"I#n 1,100,100,1,9,5,2:"); CHECK(voice(&f).frequency==120 && b->note.active);
	local_ns+=5000000; poll(&f); CHECK(!voice(&f).playing && !b->note.active);
	string(&f,"I#n 1,100,103,+1,"); CHECK(b->note.step==1 && !st_cterm_input_paused(f.terminal));
	string(&f,"0,0,1:"); CHECK(!voice(&f).playing && voice(&f).frequency==102 && !b->note.active);
	/* Unsigned hold, no key drain or early keyboard abort. */
	string(&f,"I#n 0,440,+1,1:"); CHECK(st_cterm_input_paused(f.terminal));
	key(&f,(struct cterm_key){.kind=CTERM_KEY_TEXT,.value='A'});
	local_ns+=UINT64_C(65534)*1000000; poll(&f); CHECK(b->note.active && reads==0 && f.size==0);
	local_ns+=1000000; poll(&f); CHECK(!b->note.active && !voice(&f).playing && f.size==1 && f.output[0]=='A');
	destroy(&f);
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
static void splits_and_owners(void)
{
	static const char *const cases[]={"I#n 0,440,1,2:","I#n 1,100,150,30,0,1,2:","I#n 2:"};
	for (unsigned utf8=0;utf8<2;++utf8) for (unsigned status=0;status<2;++status) {
		struct fixture f; create(&f,utf8,status); struct cterm_big *b=f.terminal->big_engine;
		for (unsigned res=1;res<=3;++res) for (unsigned test=0;test<3;++test) {
			char setup[128]; snprintf(setup,sizeof(setup),"I#R %u:I#C>0,5:P>100,100:G 0,0,100,100,100,100,120,100:",res);
			string(&f,setup); b->state.bgi.error=-11;
			struct st_big_state state=b->state; state.memory_page=1; state.active_page=state.visual_page=0;
			state.local_palette[0]=state.bgi.palette_size; memcpy(state.local_palette+1,state.bgi.palette,16);
			struct st_big_image image=b->image; uint8_t bytes[10]; memcpy(bytes,b->image_bytes,10);
			const char *wire=cases[test]; size_t length=strlen(wire);
			for (size_t split=0;split<=length+1;++split) {
				f.size=0;
				if (split==length+1) for (size_t i=0;i<length;++i) feed(&f,wire+i,1);
				else { feed(&f,wire,split); feed(&f,wire+split,length-split); }
				CHECK(b->note.active==(test!=2));
				if (test!=2) { local_ns+=1000000; poll(&f); CHECK(!voice(&f).playing); }
				CHECK(!st_cterm_input_paused(f.terminal) && !b->note.active);
				CHECK(memcmp(&state,&b->state,sizeof(state))==0 && memcmp(&image,&b->image,sizeof(image))==0 && memcmp(bytes,b->image_bytes,10)==0);
				CHECK(b->last_error==0 && b->rejected_commands==0); canvas(&f);
				string(&f,"\033[6n"); CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0);
			}
		}
		destroy(&f);
	}
}
static void reads_sources_and_wrap(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#n 0,+1,0,"); CHECK(b->note.from==65535 && b->note.active && !st_cterm_input_paused(f.terminal));
	string(&f,"0:"); CHECK(b->note.from==0 && b->audio==NULL);
	string(&f,"I#n "); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f);
	CHECK(b->note.mode==0 && b->note.index==0 && b->note.active);
	for (unsigned i=0;i<3;++i) { local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f); }
	CHECK(!b->note.active && b->audio==NULL && b->last_error==0);
	/* A new current MODE is tested after the mode0 wait returns. */
	string(&f,"I#n 0,440,1,0:"); b->note.mode=1; ++local_ns; local_ns+=999999; poll(&f);
	CHECK(b->note.active && b->note.phase==CTERM_BIG_NOTE_READ1 && !st_cterm_input_paused(f.terminal));
	string(&f,"100,103,1,0,0,1:"); CHECK(!b->note.active && !voice(&f).playing && voice(&f).frequency==102);
	/* Immediate fields use the existing loop reader and bypass g3 scaling. */
	string(&f,"I#g 3:I#& 0,0,1,0,n@4,0,440,0,1:I#g 0:");
	CHECK(!b->note.active && !voice(&f).playing && voice(&f).frequency==440 && b->numeric.loop.active==0);
	string(&f,"I#"); b->control.input.pending=1; b->control.input.count=1; b->control.input.payload[1]=255;
	string(&f,"n>0,440,1,0:"); local_ns+=1000000; poll(&f); CHECK(b->control.input.pending==1 && f.size==0);
	string(&f,"S 5,7:\033[6n"); CHECK(f.size==7 && memcmp(f.output,"\xff\033[1;1R",7)==0); f.size=0;
	/* Real supplied WORD operands: even ascending values never reach the
	 * odd endpoint, including across signed wrap. Yield without rejecting. */
	string(&f,"I#"); b->numeric.loop=(struct st_big_loop){.active=1,.count=7,.step=1,.to=1,.no_parameters=1,.type='@',.words={1,0,32767,2,0,0,1}};
	string(&f,"n "); CHECK(b->note.active && st_cterm_input_paused(f.terminal));
	for (unsigned i=0;i<12;++i) { poll(&f); CHECK(b->note.active && b->last_error==0 && (b->note.current&1)==0); }
	CHECK(st_cterm_finish(f.terminal)==0 && !b->note.active && !voice(&f).playing);
	string(&f,"I#"); b->numeric.loop=(struct st_big_loop){.active=1,.count=7,.step=1,.to=1,.no_parameters=1,.type='@',.words={1,1,0x8000,2,0,0,1}};
	string(&f,"n ");
	for (unsigned i=0;i<12;++i) { poll(&f); CHECK(b->note.active && b->last_error==0 && (b->note.current&1)==1); }
	CHECK(st_cterm_finish(f.terminal)==0 && !voice(&f).playing);
	destroy(&f);
}
static void errors_and_lifetime(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#n 0,440,10,1:"); clock_error=IO_ERROR_IO;
	/* An asynchronous audio failure is rejected before consuming ordinary
	 * ANSI bytes. Suppress repeated clock failures until a fresh F request. */
	string(&f,"\033[6n"); CHECK(!b->note.active && b->last_error==IO_ERROR_IO && voice(&f).playing);
	CHECK(f.size==6 && memcmp(f.output,"\033[1;1R",6)==0); f.size=0; unsigned errors=b->rejected_commands;
	string(&f,"\033[6n"); CHECK(b->rejected_commands==errors && f.size==6); f.size=0; clock_error=0;
	string(&f,"I#n 0,330,10,1:"); --local_ns; poll(&f);
	CHECK(!b->note.active && b->last_error==IO_ERROR_OUT_OF_RANGE && voice(&f).playing);
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && !voice(&f).playing);
	string(&f,"I#n 0,440,10,1:"); reset_on_clock=true; poll(&f); CHECK(!b->note.active && !voice(&f).playing && !b->reset_pending);
	/* A delay clock can fail without ever allocating a speaker. */
	destroy(&f); create(&f,false,false); b=f.terminal->big_engine;
	string(&f,"I#n 0,0,10,0:"); CHECK(b->audio==NULL); clock_error=IO_ERROR_IN_PROGRESS; poll(&f);
	CHECK(!b->note.active && b->last_error==IO_ERROR_PROTOCOL); clock_error=0;
	CHECK(st_cterm_reset_terminal(f.terminal)==0); local_ns=UINT64_MAX-100;
	string(&f,"I#n 0,440,1,1:"); CHECK(b->note.deadline==UINT64_MAX);
	local_ns=UINT64_MAX; poll(&f); CHECK(!b->note.active && !voice(&f).playing);
	CHECK(st_cterm_reset_terminal(f.terminal)==0); local_ns=1000;
	string(&f,"I#n 0,440,0,0:"); CHECK(voice(&f).playing);
	CHECK(st_cterm_finish(f.terminal)==0 && !voice(&f).playing);
	string(&f,"I#n 0,330,0,0:"); CHECK(voice(&f).playing && voice(&f).frequency==330);
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && !voice(&f).playing);
	destroy(&f);
}
static st_audio_runtime_t *attach(struct fixture *f)
{
	st_audio_runtime_t *r=NULL; CHECK(st_audio_runtime_create_pull(&r)==0); f->terminal->screen.audio=r; return r;
}
/* One raw sample passes through the documented fixed-point stream gain and
 * the mixer's tanh limiter; no physical backend or runtime calibration. */
static int16_t mixed(int16_t raw, float db)
{
	int32_t product=(int32_t)raw*32767;
	int32_t envelope=product>=0?product/32768:-((-product+32767)/32768);
	int32_t scaled=(int32_t)((float)envelope*powf(10.0f,db/20.0f));
	return (int16_t)(tanh((double)scaled/32767.0)*32767.0);
}
static void mixer_and_mute(void)
{
	struct fixture f; create(&f,false,false); st_audio_runtime_t *r=attach(&f);
	string(&f,"I#n 0,440,0,0:"); audio_stream_t *stream=f.terminal->fx_stream;
	CHECK(stream!=NULL && f.terminal->music_stream==NULL && !audio_stream_is_idle(stream));
	float left,right; CHECK(audio_stream_get_volume(stream,&left,&right)==0 && left==-12 && right==-12);
	int16_t out[256],raw[256]; struct st_big_speaker expected=voice(&f);
	CHECK(st_big_speaker_render(&expected,raw,128)==0 && st_audio_runtime_pull(r,out,128)==0);
	for (unsigned i=0;i<256;++i) CHECK(out[i]==mixed(raw[i],-12));
	CHECK(voice(&f).phase==expected.phase && voice(&f).low==expected.low);
	string(&f,"I#n 0,880,0,0:"); CHECK(voice(&f).phase==expected.phase);
	string(&f,"\033_SyncTERM:A;Volume;C=1;VL=0dB;VR=-6dB\033\\"); expected=voice(&f);
	CHECK(st_big_speaker_render(&expected,raw,128)==0 && st_audio_runtime_pull(r,out,128)==0);
	for (unsigned i=0;i<128;++i) CHECK(out[2*i]==mixed(raw[2*i],0) && out[2*i+1]==mixed(raw[2*i+1],-6));
	string(&f,"\033_SyncTERM:A;Volume;C=1;V=-12dB;T=128f\033\\");
	CHECK(st_audio_runtime_pull(r,out,128)==0 && audio_stream_get_volume(stream,&left,&right)==0 && left==-12 && right==-12);
	/* Pause channel1 via APC's shared stream controls; it must not revive
	 * until another accepted F. Muting policy, in contrast, keeps the gate. */
	string(&f,"\033[=7;1n"); CHECK(f.size==9 && memcmp(f.output,"\033[=7;1;1n",9)==0); f.size=0;
	string(&f,"\033_SyncTERM:A;Flush;C=1\033\\"); poll(&f); CHECK(!voice(&f).playing && audio_stream_is_idle(stream));
	string(&f,"I#n 0,440,0,0:"); CHECK(!audio_stream_is_idle(stream) && voice(&f).playing);
	f.terminal->screen.audio=NULL; poll(&f); CHECK(audio_stream_is_idle(stream) && voice(&f).playing);
	expected=voice(&f); local_ns+=1000000000; poll(&f);
	CHECK(voice(&f).phase==(uint32_t)(expected.phase+expected.step*44100));
	f.terminal->screen.audio=r; poll(&f); CHECK(!audio_stream_is_idle(stream) && voice(&f).playing);
	CHECK(st_audio_runtime_pull(r,out,128)==0);
	string(&f,"I#n 0,0,0,2:"); CHECK(!voice(&f).playing);
	for (unsigned i=0;i<3;++i) CHECK(st_audio_runtime_pull(r,out,128)==0);
	CHECK(audio_stream_is_idle(stream)); poll(&f);
	/* Non-BIG FX use the same stream after detaching the continuous source. */
	string(&f,"I#n 0,440,0,0:"); const int16_t effect[]={1000,-1000};
	CHECK(cterm_play_fx(f.terminal,effect,1) && !voice(&f).playing);
	CHECK(st_audio_runtime_pull(r,out,1)==0 && out[0]==mixed(1000,-12) && out[1]==mixed(-1000,-12));
	string(&f,"I#n 0,440,0,0:"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && !voice(&f).playing && audio_stream_is_idle(stream));
	string(&f,"I#n 0,440,0,0:"); CHECK(st_cterm_finish(f.terminal)==0 && audio_stream_is_idle(stream));
	string(&f,"I#n 0,440,0,0:"); st_audio_runtime_stop(r); poll(&f); CHECK(voice(&f).playing && audio_stream_is_idle(stream));
	destroy(&f); st_audio_runtime_destroy(r);
}
struct pull_owner { st_audio_runtime_t *runtime; atomic_bool stop,ready; atomic_uint requests; };
static int pull_thread(void *context)
{
	struct pull_owner *p=context; int16_t pcm[128];
	CHECK(st_audio_runtime_pull(p->runtime,pcm,64)==0); atomic_store(&p->ready,true);
	unsigned completed=0;
	while (!atomic_load(&p->stop)) {
		if (completed<atomic_load(&p->requests)) { CHECK(st_audio_runtime_pull(p->runtime,pcm,64)==0); ++completed; }
		else st_thread_yield();
	}
	return 0;
}
static void concurrent_shutdown(void)
{
	struct fixture f; create(&f,false,false); st_audio_runtime_t *r=attach(&f); string(&f,"I#n 0,440,0,0:");
	struct pull_owner p={.runtime=r}; atomic_init(&p.stop,false); atomic_init(&p.ready,false); atomic_init(&p.requests,0); st_thread_t thread;
	CHECK(st_thread_create(&thread,pull_thread,&p)==ST_THREAD_SUCCESS);
	while (!atomic_load(&p.ready)) st_thread_yield();
	for (unsigned i=0;i<100;++i) {
		/* Bound the test consumer's demand; an unpaced infinite consumer
		 * needlessly contends on the mixer for the duration of every write. */
		atomic_fetch_add(&p.requests,1);
		string(&f,i%2?"I#n 0,440,0,0:":"I#n 0,880,0,1:");
		if (i%10==0) CHECK(st_cterm_reset_terminal(f.terminal)==0);
	}
	/* Destruction must detach/quiesce while the existing mixer still pulls. */
	destroy(&f); atomic_store(&p.stop,true); int result;
	CHECK(st_thread_join(thread,&result)==ST_THREAD_SUCCESS && result==0); st_audio_runtime_destroy(r);
}
int main(void)
{
	note_and_sweeps(); splits_and_owners(); reads_sources_and_wrap(); errors_and_lifetime(); mixer_and_mute(); concurrent_shutdown(); return 0;
}
