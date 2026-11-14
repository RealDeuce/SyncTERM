/* BIG live-source fade: independent envelope, partitions, drains and errors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "audio_mixer.h"
#include "io_error.h"
#include "st_threads.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct source { size_t count,end; bool invalid; };
static size_t render(void *context, int16_t *pcm, size_t capacity)
{
	struct source *s=context;
	if (s->invalid) return capacity+1;
	size_t count=s->end-s->count; if (count>capacity) count=capacity;
	for (size_t i=0;i<count;++i) { pcm[2*i]=10000; pcm[2*i+1]=-10000; }
	s->count+=count; return count;
}
static void create(audio_mixer_t **m, audio_mixer_stream_t **s, struct source *source)
{
	CHECK(audio_mixer_create(m)==0 && audio_mixer_stream_create(s,*m,0,0)==0);
	CHECK(audio_mixer_stream_set_source(*s,render,source)==0);
	CHECK(audio_mixer_stream_source_active(*s,true)==0);
}
static int16_t limited(int raw, int gain)
{
	int product=raw*gain;
	int scaled=product>=0?product/32768:-((-product+32767)/32768);
	return (int16_t)(tanh((double)scaled/32767.0)*32767.0);
}
static void samples_and_partitions(void)
{
	audio_mixer_t *m; audio_mixer_stream_t *s; struct source source={.end=SIZE_MAX}; create(&m,&s,&source);
	CHECK(audio_mixer_stream_fade_source(NULL,4)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_stream_fade_source(s,4)==0 && audio_mixer_stream_buffered_frames(s)==4);
	int16_t pcm[16]; CHECK(audio_mixer_pull(m,pcm,2)==2 && source.count==2);
	CHECK(audio_mixer_stream_buffered_frames(s)==2 && !audio_mixer_stream_is_idle(s));
	CHECK(audio_mixer_pull(m,pcm+4,6)==2 && source.count==4 && audio_mixer_stream_is_idle(s));
	/* Normal sine-table overlay: at3/4,1/2,1/4,0 its interpolated Q15
	 * coefficients are30272,23170,12539,0. Multiplication by32767 adds
	 * the established envelope rounding before channel gain/soft clipping. */
	static const int gain[]={30271,23169,12538,0};
	for (unsigned i=0;i<4;++i) CHECK(pcm[2*i]==limited(10000,gain[i]) && pcm[2*i+1]==limited(-10000,gain[i]));
	for (unsigned i=8;i<16;++i) CHECK(pcm[i]==0);
	CHECK(audio_mixer_stream_drain(s)==0 && audio_mixer_stream_fade_source(s,4)==IO_ERROR_NOT_SUPPORTED);
	audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
	int16_t whole[4102],parts[4102];
	for (unsigned partition=0;partition<2;++partition) {
		source=(struct source){.end=SIZE_MAX}; create(&m,&s,&source);
		CHECK(audio_mixer_stream_fade_source(s,2051)==0);
		if (partition==0) CHECK(audio_mixer_pull(m,whole,2051)==2051);
		else for (size_t offset=0;offset<2051;) {
			size_t count=offset%37+1; if (count>2051-offset) count=2051-offset;
			CHECK(audio_mixer_pull(m,parts+2*offset,count)==count); offset+=count;
		}
		CHECK(source.count==2051 && audio_mixer_stream_is_idle(s) && audio_mixer_stream_buffered_frames(s)==0);
		audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
	}
	CHECK(memcmp(whole,parts,sizeof(whole))==0);
}
struct waiter { audio_mixer_stream_t *stream; atomic_bool started,done; int status; };
static int drain(void *context)
{
	struct waiter *w=context; atomic_store(&w->started,true);
	w->status=audio_mixer_stream_drain(w->stream); atomic_store(&w->done,true); return 0;
}
static void drain_live_fade(void)
{
	audio_mixer_t *m; audio_mixer_stream_t *s; struct source source={.end=SIZE_MAX}; create(&m,&s,&source);
	CHECK(audio_mixer_stream_fade_source(s,4)==0);
	struct waiter w={.stream=s}; atomic_init(&w.started,false); atomic_init(&w.done,false);
	st_thread_t thread; CHECK(st_thread_create(&thread,drain,&w)==ST_THREAD_SUCCESS);
	while (!atomic_load(&w.started)) st_thread_yield();
	int16_t pcm[4]; CHECK(audio_mixer_pull(m,pcm,2)==2 && !atomic_load(&w.done));
	CHECK(audio_mixer_pull(m,pcm,2)==2);
	int result; CHECK(st_thread_join(thread,&result)==ST_THREAD_SUCCESS && result==0 && w.status==0);
	audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
}
static void acknowledgment_and_cancel(unsigned action)
{
	audio_mixer_t *m; audio_mixer_stream_t *s; struct source source={.end=SIZE_MAX}; create(&m,&s,&source);
	CHECK(audio_mixer_stream_drain(s)==IO_ERROR_BUSY);
	CHECK(audio_mixer_stream_fade_source(s,4)==0);
	int16_t pcm[8]; uint64_t ticket; CHECK(audio_mixer_pull_pending(m,pcm,2,&ticket)==2);
	CHECK(source.count==2 && audio_mixer_stream_buffered_frames(s)==4);
	audio_mixer_acknowledge(m,ticket); CHECK(audio_mixer_stream_buffered_frames(s)==2);
	CHECK(audio_mixer_pull_pending(m,pcm,4,&ticket)==2 && audio_mixer_stream_is_idle(s));
	CHECK(audio_mixer_stream_buffered_frames(s)==2);
	/* The final backend submission still belongs to the drain's sequence. */
	struct waiter w={.stream=s}; atomic_init(&w.started,false); atomic_init(&w.done,false);
	st_thread_t thread; CHECK(st_thread_create(&thread,drain,&w)==ST_THREAD_SUCCESS);
	while (!atomic_load(&w.started)) st_thread_yield();
	CHECK(!atomic_load(&w.done));
	if (action==0) audio_mixer_acknowledge(m,ticket);
	else if (action==1) CHECK(audio_mixer_stream_clear(s)==0);
	else audio_mixer_stop(m,IO_ERROR_CANCELLED);
	int result; CHECK(st_thread_join(thread,&result)==ST_THREAD_SUCCESS && result==0 && atomic_load(&w.done));
	/* Clear can win before drain's initial snapshot; stop has a persistent
	 * terminal status and must return CANCELLED in either ordering. */
	CHECK(action==0?w.status==0:action==1?(w.status==0 || w.status==IO_ERROR_CANCELLED):w.status==IO_ERROR_CANCELLED);
	audio_mixer_acknowledge(m,ticket);
	CHECK(audio_mixer_stream_buffered_frames(s)==0);
	audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
}
static void replacement_and_errors(void)
{
	audio_mixer_t *m; audio_mixer_stream_t *s; struct source source={.end=SIZE_MAX}; create(&m,&s,&source);
	int16_t pcm[16]; CHECK(audio_mixer_stream_fade_source(s,SIZE_MAX)==0);
	CHECK(audio_mixer_pull(m,pcm,2)==2 && audio_mixer_stream_buffered_frames(s)==SIZE_MAX-2);
	CHECK(audio_mixer_stream_fade_source(s,4)==0); CHECK(audio_mixer_pull(m,pcm,2)==2);
	CHECK(audio_mixer_stream_fade_source(s,1)==0); CHECK(audio_mixer_pull(m,pcm,4)==1 && source.count==5);
	CHECK(pcm[0]==0 && pcm[1]==0 && audio_mixer_stream_is_idle(s));
	CHECK(audio_mixer_stream_source_active(s,true)==0 && audio_mixer_stream_buffered_frames(s)==SIZE_MAX);
	CHECK(audio_mixer_stream_fade_source(s,0)==0 && audio_mixer_pull(m,pcm,4)==0 && source.count==5);
	CHECK(audio_mixer_stream_source_active(s,true)==0); CHECK(audio_mixer_stream_fade_source(s,8)==0);
	CHECK(audio_mixer_stream_clear(s)==0 && audio_mixer_pull(m,pcm,4)==0);
	CHECK(audio_mixer_stream_source_active(s,true)==0 && audio_mixer_pull(m,pcm,4)==4);
	source.end=source.count+3; CHECK(audio_mixer_stream_fade_source(s,8)==0);
	CHECK(audio_mixer_pull(m,pcm,8)==3 && audio_mixer_stream_drain(s)==0);
	source.invalid=true; CHECK(audio_mixer_stream_source_active(s,true)==0 && audio_mixer_stream_fade_source(s,1)==0);
	CHECK(audio_mixer_pull(m,pcm,4)==0 && audio_mixer_stream_drain(s)==IO_ERROR_OUT_OF_RANGE);
	CHECK(audio_mixer_stream_fade_source(s,4)==IO_ERROR_OUT_OF_RANGE);
	CHECK(audio_mixer_stream_set_source(s,NULL,NULL)==0);
	audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
	CHECK(audio_mixer_create(&m)==0 && audio_mixer_stream_create(&s,m,0,0)==0);
	const int16_t queued[]={10000,-10000}; CHECK(audio_mixer_stream_append(s,queued,1,NULL)==0);
	CHECK(audio_mixer_stream_fade_source(s,4)==IO_ERROR_NOT_SUPPORTED && audio_mixer_stream_buffered_frames(s)==1);
	CHECK(audio_mixer_pull(m,pcm,1)==1 && pcm[0]==limited(10000,32767));
	audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
}
int main(void)
{
	samples_and_partitions(); drain_live_fade(); for (unsigned i=0;i<3;++i) acknowledgment_and_cancel(i); replacement_and_errors(); return 0;
}
