/* BIG OPL amplitude headroom at the existing PCM/channel-control boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "audio.h"
#include "audio_mixer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct source { int32_t left,right; size_t count,limit; bool invalid; };
static size_t render32(void *context, int32_t *pcm, size_t capacity)
{
	struct source *s=context;
	if (s->invalid) return capacity+1;
	size_t n=s->limit-s->count; if (n>capacity) n=capacity;
	for (size_t i=0;i<n;++i) { pcm[2*i]=s->left; pcm[2*i+1]=s->right; }
	s->count+=n; return n;
}
static size_t render16(void *context, int16_t *pcm, size_t capacity)
{
	struct source *s=context; size_t n=s->limit-s->count; if (n>capacity) n=capacity;
	for (size_t i=0;i<n;++i) { pcm[2*i]=(int16_t)s->left; pcm[2*i+1]=(int16_t)s->right; }
	s->count+=n; return n;
}
static int64_t contribution(int32_t sample, int envelope, float db)
{
	int64_t product=(int64_t)sample*envelope;
	int64_t scaled=product>=0?product/32768:-((-product+32767)/32768);
	return (int64_t)((float)scaled*powf(10.0f,db/20.0f));
}
static int16_t limited(int64_t amplitude) { return (int16_t)(tanh((double)amplitude/32767.0)*32767.0); }
static void wide_headroom(void)
{
	audio_mixer_t *m; audio_mixer_stream_t *stream[9]; CHECK(audio_mixer_create(&m)==0);
	struct source sum={90000,90000,0,5,false};
	CHECK(audio_mixer_stream_create(stream,m,-12,-12)==0 && audio_mixer_stream_set_source32(stream[0],render32,&sum)==0);
	CHECK(audio_mixer_stream_source_active(stream[0],true)==0);
	int16_t wide[4],individual[4]; CHECK(audio_mixer_pull(m,wide,2)==2);
	CHECK(wide[0]==limited(contribution(90000,32767,-12)) && wide[0]>19000 && wide[1]==wide[0]);
	audio_mixer_stream_destroy(stream[0]);
	struct source voice[9];
	for (unsigned i=0;i<9;++i) {
		voice[i]=(struct source){10000,10000,0,5,false};
		CHECK(audio_mixer_stream_create(stream+i,m,-12,-12)==0 && audio_mixer_stream_set_source(stream[i],render16,voice+i)==0 && audio_mixer_stream_source_active(stream[i],true)==0);
	}
	CHECK(audio_mixer_pull(m,individual,2)==2);
	CHECK(individual[0]==limited(9*contribution(10000,32767,-12)) && abs(wide[0]-individual[0])<=8);
	for (unsigned i=0;i<9;++i) audio_mixer_stream_destroy(stream[i]);
	struct source a={90000,INT32_MIN,0,5,false},b={-45000,INT32_MAX,0,5,false};
	CHECK(audio_mixer_stream_create(stream,m,-12,-96)==0 && audio_mixer_stream_create(stream+1,m,-12,-96)==0);
	CHECK(audio_mixer_stream_set_source32(stream[0],render32,&a)==0 && audio_mixer_stream_set_source32(stream[1],render32,&b)==0);
	CHECK(audio_mixer_stream_source_active(stream[0],true)==0 && audio_mixer_stream_source_active(stream[1],true)==0);
	CHECK(audio_mixer_pull(m,wide,1)==1);
	CHECK(wide[0]==limited(contribution(90000,32767,-12)+contribution(-45000,32767,-12)) && wide[0]>10000);
	CHECK(wide[1]==limited(contribution(INT32_MIN,32767,-96)+contribution(INT32_MAX,32767,-96)));
	audio_mixer_stream_destroy(stream[0]); audio_mixer_stream_destroy(stream[1]); audio_mixer_destroy(m);
}
static void equivalence_and_controls(void)
{
	audio_mixer_t *m; audio_mixer_stream_t *s; CHECK(audio_mixer_create(&m)==0 && audio_mixer_stream_create(&s,m,0,0)==0);
	/* Every S16 value must retain its established gain/envelope/limiter
	 * result when supplied through the S32 source interface. */
	struct source source={0,0,0,SIZE_MAX,false}; int16_t narrow[2],wide[2];
	for (int value=-32768;value<=32767;++value) {
		source.left=value; source.right=-1-value;
		CHECK(audio_mixer_stream_set_source(s,render16,&source)==0 && audio_mixer_stream_source_active(s,true)==0 && audio_mixer_pull(m,narrow,1)==1);
		CHECK(audio_mixer_stream_set_source32(s,render32,&source)==0 && audio_mixer_stream_source_active(s,true)==0 && audio_mixer_pull(m,wide,1)==1);
		CHECK(memcmp(narrow,wide,sizeof(wide))==0 && wide[0]==limited(contribution(value,32767,0)));
	}
	CHECK(audio_mixer_stream_append(s,narrow,1,NULL)==IO_ERROR_BUSY);
	CHECK(audio_mixer_stream_fade_source(s,4)==0);
	source.left=100000; source.right=-100000;
	int16_t out[16]; CHECK(audio_mixer_pull(m,out,2)==2 && audio_mixer_stream_buffered_frames(s)==2);
	CHECK(audio_mixer_pull(m,out+4,6)==2 && audio_mixer_stream_is_idle(s));
	static const int gain[]={30271,23169,12538,0};
	for (unsigned i=0;i<4;++i) CHECK(out[i*2]==limited(contribution(100000,gain[i],0)) && out[i*2+1]==limited(contribution(-100000,gain[i],0)));
	for (unsigned i=8;i<16;++i) CHECK(out[i]==0);
	CHECK(audio_mixer_stream_set_source(s,NULL,NULL)==0 && audio_mixer_stream_source_active(s,true)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_stream_append(s,narrow,1,NULL)==0 && audio_mixer_pull(m,out,1)==1);
	source.invalid=true;
	CHECK(audio_mixer_stream_set_source32(s,render32,&source)==0 && audio_mixer_stream_source_active(s,true)==0);
	CHECK(audio_mixer_pull(m,out,1)==0 && audio_mixer_stream_drain(s)==IO_ERROR_OUT_OF_RANGE);
	CHECK(audio_mixer_stream_set_source32(s,NULL,NULL)==0);
	audio_mixer_stream_destroy(s); audio_mixer_destroy(m);
}
static void public_stream_and_snapshot(void)
{
	audio_engine_t *engine=NULL; audio_stream_t *stream=NULL; CHECK(audio_engine_create_pull(&engine)==0);
	CHECK(audio_stream_create(&stream,engine,-12,-12)==0);
	struct audio_stream_volume state;
	CHECK(audio_stream_get_volume_state(stream,&state)==0 && state.left_db==-12 && state.right_db==-12 && state.target_left_db==-12 && state.ramp_frames==0);
	struct source source={90000,-90000,0,SIZE_MAX,false}; CHECK(audio_stream_set_source32(stream,render32,&source)==0 && audio_stream_source_active(stream,true)==0);
	CHECK(audio_stream_ramp_volume(stream,0,-6,8)==0);
	int16_t pcm[16]; CHECK(audio_engine_pull(engine,pcm,3)==0);
	CHECK(audio_stream_get_volume_state(stream,&state)==0 && state.ramp_frames==5 && state.target_left_db==0 && state.target_right_db==-6 && state.left_db>-12 && state.left_db<0);
	CHECK(audio_stream_set_source(stream,NULL,NULL)==0 && audio_stream_get_volume_state(stream,&state)==0 && state.ramp_frames==5);
	CHECK(audio_stream_set_source32(stream,render32,&source)==0 && audio_stream_source_active(stream,true)==0 && audio_engine_pull(engine,pcm,5)==0);
	CHECK(audio_stream_get_volume_state(stream,&state)==0 && state.ramp_frames==0 && state.left_db==0 && state.right_db==-6);
	CHECK(audio_stream_set_source32(NULL,render32,&source)==IO_ERROR_INVALID_ARGUMENT && audio_stream_get_volume_state(stream,NULL)==IO_ERROR_INVALID_ARGUMENT);
	audio_stream_destroy(stream); audio_engine_destroy(engine);
}
int main(void) { wide_headroom(); equivalence_and_controls(); public_stream_and_snapshot(); return 0; }
