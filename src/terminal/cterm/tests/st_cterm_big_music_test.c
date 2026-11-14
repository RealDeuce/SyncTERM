/* Independent native N framing, live storage, sample progress and lifetime. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_cterm.h"
#include "st_audio_runtime.h"
#include "st_audio_weighting.h"
#include "st_threads.h"
#include "st_time.h"
#include "io_error.h"
#include <stdio.h>
#include <math.h>
#include <stdatomic.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static uint64_t local_ns;
static unsigned bells;
static int clock_error;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return clock_error; }
static void bell(void *context) { (void)context; ++bells; }
struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; st_audio_runtime_t *audio; };
static void create(struct fixture *f, bool pull, bool runtime)
{
	memset(f,0,sizeof(*f)); local_ns=1000; bells=0; clock_error=0;
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	CHECK(st_console_create(&f->console,&display,&geometry)==0 && st_font_catalog_create(&f->fonts,f->console)==0);
	if (runtime) CHECK((pull?st_audio_runtime_create_pull(&f->audio):st_audio_runtime_create(&f->audio,0))==0);
	const struct st_cterm_config config={.console=f->console,.fonts=f->fonts,.audio=f->audio,
	    .emulation=ST_CTERM_ANSI_BBS,.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,
	    .encoding=ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,.quiet=true,.scrollback_lines=2,.apc_directory="."};
	CHECK(st_cterm_create(&f->terminal,&config)==0);
	f->terminal->big_engine->numeric.io.now_ns=now; f->terminal->big_engine->numeric.io.bell=bell;
	CHECK(f->terminal->big_engine->music_memory==1);
}
static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_audio_runtime_destroy(f->audio);
	st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}
static void poll(struct fixture *f) { CHECK(st_cterm_poll(f->terminal)==0); }
static void settle(struct fixture *f)
{
	for (unsigned i=0;st_cterm_input_paused(f->terminal);++i) { CHECK(i<10000); poll(f); }
}
static void feed(struct fixture *f, const void *data, size_t count)
{
	size_t offset=0; unsigned attempts=0;
	while (offset<count) {
		size_t used=0; int status=st_cterm_write(f->terminal,(const uint8_t *)data+offset,count-offset,&used);
		CHECK((status==0 || status==IO_ERROR_IN_PROGRESS) && used<=count-offset && ++attempts<100000);
		offset+=used; if (used==0) poll(f);
	}
	settle(f);
}
static void string(struct fixture *f, const char *wire) { feed(f,wire,strlen(wire)); }
static struct st_big_music state(struct fixture *f)
{
	struct st_big_music m; CHECK(cterm_big_music_snapshot(f->terminal->big_engine,&m)==0); return m;
}
static uint16_t memory(struct fixture *f, uint16_t offset, bool word)
{
	uint16_t value=999; CHECK(cterm_big_music_read(f->terminal->big_engine,offset,word,&value)==0); return value;
}
static void load(struct fixture *f, unsigned mode, const uint8_t *data, size_t size)
{
	char header[64]; snprintf(header,sizeof(header),"I#N %u,%u,%u:",mode,(unsigned)(size/1000),(unsigned)(size%1000));
	string(f,header); feed(f,data,size);
}
static const uint8_t example[]={0xb5,9,0x20,0x23,1,0xff,4,0,0x21,0x56,0,0xff,0,0,0xff};
static void parsing_and_controls(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	CHECK(state(&f).enabled==1 && state(&f).length==0 && state(&f).running==0);
	load(&f,0,example,sizeof(example));
	struct st_big_music m=state(&f); CHECK(m.length==15 && m.scratch==15 && m.enabled==1 && m.running==0 && m.cached_divider==0);
	for (unsigned i=0;i<15;++i) CHECK(memory(&f,(uint16_t)i,false)==example[i]);
	string(&f,"I#N 2:"); m=state(&f);
	CHECK(m.length==15 && m.running==1 && m.position_low==2 && m.position_high==0 && m.end_low==14 && m.end_high==0 && m.cached_divider==2485 && m.divider==2485 && m.delay==1);
	/* N0 replaces individual live bytes without stopping/rearming playback. */
	const uint8_t replacement[]={1,0}; load(&f,0,replacement,2); m=state(&f);
	CHECK(m.length==2 && m.running==1 && m.position_low==2 && m.end_low==14 && m.cached_divider==2485 && m.divider==2485 && m.enabled==1);
	CHECK(memory(&f,0,true)==1 && memory(&f,2,false)==0x20 && memory(&f,14,false)==0xff);
	string(&f,"I#N 5,12,345:"); m=state(&f); CHECK(m.scratch==12345 && m.divider==12345 && m.cached_divider==2485);
	string(&f,"I#n 2:"); CHECK(state(&f).divider==2485); /* Unsupported n still restores current qzx. */
	string(&f,"I#N 5,0,99:I#n "); CHECK(state(&f).divider==0);
	string(&f,"2:"); CHECK(state(&f).divider==2485);
	b->effect_sound_on=0; string(&f,"I#N 5,0,99:I#b 19:"); CHECK(state(&f).divider==2485);
	string(&f,"I#N 4:"); m=state(&f); CHECK(m.length==2 && m.running==1 && m.position_low==2 && m.cached_divider==2485 && m.divider==2485);
	string(&f,"I#N 2:"); CHECK(state(&f).cached_divider==1 && state(&f).end_low==1);
	CHECK(st_cterm_finish(f.terminal)==0 && state(&f).running==0 && b->music_memory==1);
	load(&f,0,example,sizeof(example)); string(&f,"I#N 3,0,4,0,11:"); m=state(&f);
	CHECK(m.position_low==5 && m.position_high==0 && m.end_low==10 && m.running==1);
	string(&f,"I#N 3,0,0,0,0:"); m=state(&f);
	CHECK(m.position_low==65534 && m.position_high==65535 && m.end_low==1 && m.running==1);
	CHECK(st_cterm_finish(f.terminal)==0);
	b->feature_gates[2][0]=1; string(&f,"I#N 2:"); m=state(&f);
	CHECK(m.running==0 && m.position_low==2 && m.end_low==14 && m.delay==1 && m.cached_divider==2485);
	b->feature_gates[2][0]=0;
	CHECK(st_cterm_reset_terminal(f.terminal)==0 && state(&f).length==15 && b->music_memory==1 && memory(&f,14,false)==255);
	b->control.music.enabled=0; string(&f,"I#N 2:"); CHECK(state(&f).running==0 && state(&f).enabled==0);
	load(&f,0,replacement,2); CHECK(state(&f).length==2 && state(&f).enabled==0);
	/* Warm's caller tests availability once; a budget suspension inside
	 * the selected library call does not introduce another memory gate. */
	const char warm[]="I#N 4:"; size_t used;
	CHECK(st_cterm_write(f.terminal,warm,sizeof(warm)-1,&used)==IO_ERROR_IN_PROGRESS && used==sizeof(warm)-1);
	CHECK(b->music_command.warm==64 && b->music_command.warm_begun);
	b->music_memory=0; settle(&f); CHECK(b->music_command.warm==175);
	b->music_memory=1;
	destroy(&f);
}
static void split_raw_and_absent(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	static const uint8_t wire[]={'I','#','N',' ','0',',','0',',','1','0',':',0,'I','#','N',255,':','\n','\r',7,0};
	for (size_t split=0;split<=sizeof(wire)+1;++split) {
		if (split==sizeof(wire)+1) for (size_t i=0;i<sizeof(wire);++i) feed(&f,wire+i,1);
		else { feed(&f,wire,split); feed(&f,wire+split,sizeof(wire)-split); }
		CHECK(state(&f).length==10 && state(&f).enabled==1 && bells==0 && b->rejected_commands==0);
		for (unsigned i=0;i<10;++i) CHECK(memory(&f,(uint16_t)i,false)==wire[11+i]);
	}
	b->music_memory=0;
	string(&f,"I#N 0:I#N 1:I#N 2:I#N 3:I#N 4:I#N 5,12,345:");
	CHECK(state(&f).length==10 && state(&f).scratch==12345 && state(&f).divider==0 && state(&f).running==0);
	b->music_memory=1; string(&f,"I#N +1:"); CHECK(state(&f).position_low==2 && state(&f).running==1); /* WORDffff selects2. */
	CHECK(st_cterm_finish(f.terminal)==0); destroy(&f);
}
static void timeout_and_recovery(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#N 0,0,2:"); const uint8_t renewing=0x55;
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS-1; feed(&f,&renewing,1);
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS-1; feed(&f,&renewing,1);
	CHECK(state(&f).length==2 && state(&f).scratch==2 && bells==0 && memory(&f,0,true)==0x5555);
	load(&f,0,example,sizeof(example)); string(&f,"I#N 0,0,2:");
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS-1; poll(&f); CHECK(state(&f).length==2 && state(&f).scratch==0 && bells==0);
	++local_ns; const uint8_t byte=0x71; feed(&f,&byte,1);
	CHECK(memory(&f,0,false)==0x71 && memory(&f,1,false)==0 && memory(&f,2,false)==0x20);
	CHECK(state(&f).length==0 && state(&f).scratch==0 && state(&f).enabled==1 && bells==1 && b->rejected_commands==0);
	string(&f,"I#N 0,0,3:"); feed(&f,&byte,1); local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; poll(&f);
	CHECK(memory(&f,0,false)==0x71 && memory(&f,1,false)==0 && state(&f).length==0 && bells==2);
	string(&f,"I#N 0,0,9:"); feed(&f,&byte,1); CHECK(st_cterm_finish(f.terminal)==0);
	CHECK(state(&f).length==9 && state(&f).scratch==1 && bells==2 && b->music_command.phase==CTERM_BIG_MUSIC_OFF);
	string(&f,"I#N 0,0,9:"); clock_error=IO_ERROR_IO; poll(&f); clock_error=0;
	CHECK(b->last_error==IO_ERROR_IO && b->rejected_commands==1 && !st_cterm_input_paused(f.terminal));
	string(&f,"I#C 0,7:"); CHECK(b->state.application.colors[0]==7); destroy(&f);
}
static void maximum_load_timeout(void)
{
	struct fixture f; create(&f,false,false); struct cterm_big *b=f.terminal->big_engine;
	string(&f,"I#N 0,66,0:"); uint8_t wrapped[464]={0}; feed(&f,wrapped,sizeof(wrapped));
	CHECK(state(&f).length==464 && state(&f).scratch==464);
	string(&f,"I#N 0,65,535:");
	uint8_t *bytes=calloc(65533,1); CHECK(bytes!=NULL); feed(&f,bytes,65533); free(bytes);
	CHECK(state(&f).length==65534 && state(&f).scratch==65533 && bells==0);
	local_ns+=ST_BIG_NUMERIC_TIMEOUT_NS; const uint8_t last=0xab; feed(&f,&last,1);
	CHECK(memory(&f,65533,false)==0xab && state(&f).scratch==65534 && state(&f).length==65534);
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && bells==0 && !st_cterm_input_paused(f.terminal));
	string(&f,"I#C 0,6:"); CHECK(b->state.application.colors[0]==6); destroy(&f);
}
static void sample_progress_and_channel(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	load(&f,1,example,sizeof(example)); CHECK(f.terminal->music_stream!=NULL && st_music_busy(f.terminal->music_player));
	CHECK(st_music_wait(f.terminal->music_player)==0 && state(&f).position_low==2);
	int16_t pcm[2048]; CHECK(st_audio_runtime_pull(f.audio,pcm,90)==0 && state(&f).position_low==2);
	CHECK(st_audio_runtime_pull(f.audio,pcm,1)==0 && state(&f).position_low==8 && state(&f).delay==4 && state(&f).data==4);
	CHECK(st_audio_runtime_pull(f.audio,pcm,367)==0 && state(&f).position_low==8);
	CHECK(st_audio_runtime_pull(f.audio,pcm,1)==0 && state(&f).position_low==14 && state(&f).running==0 && state(&f).delay==1);
	CHECK(st_music_volume(f.terminal->music_player,-6,-18,16)==0 && st_audio_runtime_pull(f.audio,pcm,16)==0);
	float left,right; CHECK(st_music_get_volume(f.terminal->music_player,&left,&right)==0 && left==-6 && right==-18);
	CHECK(st_music_flush(f.terminal->music_player,0)==0); poll(&f);
	CHECK(state(&f).running==0 && b->music_memory==1 && !st_music_busy(f.terminal->music_player));
	string(&f,"I#N 2:"); CHECK(state(&f).position_low==2 && st_music_busy(f.terminal->music_player));
	CHECK(st_cterm_finish(f.terminal)==0 && !st_music_busy(f.terminal->music_player));
	string(&f,"I#N 2:"); CHECK(st_cterm_reset_terminal(f.terminal)==0 && !st_music_busy(f.terminal->music_player) && state(&f).length==15);
	string(&f,"I#N 2:"); destroy(&f);
}
static void muted_progress_and_bad_slice(void)
{
	struct fixture f; create(&f,false,true); struct cterm_big *b=f.terminal->big_engine;
	load(&f,1,example,sizeof(example)); CHECK(f.terminal->music_stream==NULL && st_music_busy(f.terminal->music_player));
	local_ns=1000+UINT64_C(2063493); poll(&f); CHECK(state(&f).position_low==8);
	local_ns=1000+UINT64_C(10408164); poll(&f); CHECK(state(&f).position_low==14 && state(&f).running==0);
	CHECK(st_music_flush(f.terminal->music_player,0)==0); poll(&f); CHECK(!st_music_busy(f.terminal->music_player));
	string(&f,"I#N 3,0,0,0,0:"); local_ns+=100000000; poll(&f);
	CHECK(b->last_error==IO_ERROR_OUT_OF_RANGE && b->rejected_commands==1 && !st_cterm_input_paused(f.terminal) && state(&f).running==0 && state(&f).position_low==65534);
	string(&f,"I#C 0,9:"); CHECK(b->state.application.colors[0]==9 && b->music_memory==1);
	destroy(&f);
}
static void real_x7_progress(void)
{
	struct fixture f; create(&f,true,true); struct cterm_big *b=f.terminal->big_engine;
	load(&f,1,example,sizeof(example)); string(&f,"I#X 7,1:");
	const char command[]="I#C 0,7:"; size_t used;
	CHECK(st_cterm_write(f.terminal,command,sizeof(command)-1,&used)==IO_ERROR_IN_PROGRESS && used==4);
	CHECK(st_cterm_input_paused(f.terminal) && state(&f).scratch==2 && b->state.application.colors[0]==15);
	int16_t pcm[736]; CHECK(st_audio_runtime_pull(f.audio,pcm,91)==0 && state(&f).position_low==8);
	poll(&f); CHECK(!st_cterm_input_paused(f.terminal)); feed(&f,command+used,sizeof(command)-1-used);
	CHECK(b->state.application.colors[0]==7 && state(&f).sync==1);
	const char disable[]="I#X 7,0:";
	CHECK(st_cterm_write(f.terminal,disable,sizeof(disable)-1,&used)==IO_ERROR_IN_PROGRESS && used==4);
	CHECK(st_audio_runtime_pull(f.audio,pcm,368)==0 && state(&f).running==0 && state(&f).position_low==14);
	poll(&f); feed(&f,disable+used,sizeof(disable)-1-used); CHECK(state(&f).sync==0);
	destroy(&f);
}
static void rotation_music_rate(void)
{
	struct fixture f; create(&f,false,false); load(&f,1,example,sizeof(example));
	string(&f,"I#N 5,12,345:"); CHECK(state(&f).divider==12345 && state(&f).cached_divider==2485);
	const char rotate[]="I#X 8,3,4,1,1:"; size_t used;
	CHECK(st_cterm_write(f.terminal,rotate,sizeof(rotate)-1,&used)==IO_ERROR_IN_PROGRESS && used==sizeof(rotate)-1);
	CHECK(st_cterm_input_paused(f.terminal) && state(&f).divider==0);
	local_ns+=1000000; poll(&f); CHECK(st_cterm_input_paused(f.terminal) && state(&f).divider==0);
	local_ns+=1000000; poll(&f); CHECK(!st_cterm_input_paused(f.terminal) && state(&f).divider==2485);
	string(&f,"I#N 5,12,345:I#X 8,1,1,1,1:");
	CHECK(state(&f).divider==2485 && state(&f).cached_divider==2485); /* Equal endpoints still reach both requests. */
	destroy(&f);
}
static void ansi_music_takeover(void)
{
	struct fixture f; create(&f,true,true); load(&f,1,example,sizeof(example));
	audio_stream_t *stream=f.terminal->music_stream;
	struct st_music_note *notes=calloc(1,sizeof(*notes)); CHECK(notes!=NULL);
	*notes=(struct st_music_note){.pitch=12,.sound_frames=128};
	CHECK(st_music_queue_notes(f.terminal->music_player,notes,1)==0); poll(&f);
	CHECK(state(&f).running==0 && f.terminal->music_stream==stream);
	bool sounded=false; uint64_t begin,current; CHECK(st_time_monotonic_ms(&begin)==0);
	while (st_music_busy(f.terminal->music_player)) {
		int16_t pcm[256]; CHECK(st_audio_runtime_pull(f.audio,pcm,128)==0);
		for (unsigned i=0;i<256;++i) if (pcm[i]>1 || pcm[i]<-1) sounded=true;
		CHECK(st_time_monotonic_ms(&current)==0 && current-begin<2000); st_thread_yield();
	}
	CHECK(sounded && st_music_wait(f.terminal->music_player)==0 && f.terminal->music_stream==stream && state(&f).length==15);
	destroy(&f);
}
struct pulling { st_audio_runtime_t *audio; atomic_bool stop,started; size_t count; };
static int pull_thread(void *context)
{
	struct pulling *p=context; int16_t pcm[512]; atomic_store(&p->started,true);
	while (!atomic_load(&p->stop)) {
		int status=st_audio_runtime_pull(p->audio,pcm,256); if (status!=0) return status; ++p->count;
		/* A device consumes this block in5.8ms. A1ms test cadence still
		 * overlaps mutations, without an unpaced mutex-starvation loop. */
		const struct timespec delay={0,1000000}; (void)st_thread_sleep(&delay,NULL);
	}
	return 0;
}
static void concurrent_lifecycle(void)
{
	struct fixture f; create(&f,true,true); load(&f,1,example,sizeof(example));
	struct pulling p={.audio=f.audio}; atomic_init(&p.stop,false); atomic_init(&p.started,false); st_thread_t thread;
	CHECK(st_thread_create(&thread,pull_thread,&p)==ST_THREAD_SUCCESS);
	while (!atomic_load(&p.started)) st_thread_yield();
	for (unsigned i=0;i<4;++i) {
		const uint8_t header[]={0xb5,9}; load(&f,0,header,sizeof(header));
		CHECK(state(&f).enabled==1 && memory(&f,2,false)==0x20);
		CHECK(st_music_flush(f.terminal->music_player,32)==0); poll(&f);
		CHECK(st_cterm_reset_terminal(f.terminal)==0 && !st_music_busy(f.terminal->music_player));
		load(&f,1,example,sizeof(example));
	}
	/* Runtime/frontend outlives terminal/source destruction, just as the
	 * existing device consumer can still be pulling during source teardown. */
	st_cterm_destroy(&f.terminal); atomic_store(&p.stop,true); int result;
	CHECK(st_thread_join(thread,&result)==ST_THREAD_SUCCESS && result==0 && p.count!=0);
	st_audio_runtime_destroy(f.audio); st_font_catalog_destroy(f.fonts); st_console_destroy(f.console);
}
static void source_levels(void)
{
	/* Full-level sustained440Hz carrier, silent modulator; encoded TL/EG
	 * dynamics remain under the same fixed source multiplier. */
	static const uint8_t song[]={0xb5,9,1,0x20,0,0x20,0x21,0,0x23,0x21,0,
	    0x40,63,0,0x43,0,0,0x60,0xf0,0,0x63,0xf0,0,0x80,0x0f,0,0x83,0x0f,0,
	    0xc0,0,0,0xa0,0x44,0,0xb0,0x32,0,0xff,0xff,0xff,0xb0,0x12,0,0xff,0,0,0xff};
	struct fixture f; create(&f,true,true); load(&f,1,song,sizeof(song));
	const size_t count=2*AUDIO_SAMPLE_RATE;
	int16_t *actual=malloc(count*AUDIO_FRAME_SIZE),*reference=malloc(count*AUDIO_FRAME_SIZE);
	CHECK(actual!=NULL && reference!=NULL && st_audio_runtime_pull(f.audio,actual,count)==0);
	CHECK(st_music_flush(f.terminal->music_player,0)==0); poll(&f);
	CHECK(audio_generate_tone(reference,count,440,AUDIO_WAVE_SINE_SAW_HARM,true)==0);
	audio_engine_t *engine; audio_stream_t *stream;
	CHECK(st_audio_runtime_acquire(f.audio,&engine)==0 && audio_stream_create(&stream,engine,-12,-12)==0);
	CHECK(audio_stream_append(stream,reference,count,NULL)==0 && st_audio_runtime_pull(f.audio,reference,count)==0);
	audio_stream_destroy(stream);
	struct st_audio_weighting a={0},r={0}; double ap=0,rp=0; int peak=0;
	for (size_t i=0;i<count;++i) {
		double x=st_audio_weight_sample(&a,actual[2*i]),y=st_audio_weight_sample(&r,reference[2*i]);
		if (i>=AUDIO_SAMPLE_RATE/2 && i<3*AUDIO_SAMPLE_RATE/2) { ap+=x*x; rp+=y*y; }
		CHECK(actual[2*i]==actual[2*i+1]); int p=abs(actual[2*i]); if (p>peak) peak=p;
	}
	CHECK(ap>0 && rp>0); double difference=10*log10(ap/rp);
	printf("BIG N carrier/reference mixed difference %+.6f dB; peak %d\n",difference,peak);
	CHECK(fabs(difference)<0.05 && peak<32767);
	free(actual); free(reference); destroy(&f);
}
static void put_register(uint8_t *song, size_t *size, uint8_t address, uint16_t value)
{
	song[(*size)++]=address; song[(*size)++]=(uint8_t)value; song[(*size)++]=(uint8_t)(value>>8);
}
static void polyphonic_levels(void)
{
	for (unsigned attenuation=0;attenuation<=32;attenuation+=32) {
		uint8_t song[512]={0xb5,9}; size_t size=2;
		static const unsigned offset[]={0,1,2,8,9,10,16,17,18};
		for (unsigned ch=0;ch<9;++ch) {
			unsigned op=offset[ch];
			put_register(song,&size,(uint8_t)(0x20+op),0x21); put_register(song,&size,(uint8_t)(0x23+op),0x21);
			put_register(song,&size,(uint8_t)(0x40+op),63); put_register(song,&size,(uint8_t)(0x43+op),(uint16_t)attenuation);
			put_register(song,&size,(uint8_t)(0x60+op),0xf0); put_register(song,&size,(uint8_t)(0x63+op),0xf0);
			put_register(song,&size,(uint8_t)(0x80+op),0x0f); put_register(song,&size,(uint8_t)(0x83+op),0x0f);
			put_register(song,&size,(uint8_t)(0xc0+ch),0); put_register(song,&size,(uint8_t)(0xa0+ch),0x44); put_register(song,&size,(uint8_t)(0xb0+ch),0x32);
		}
		put_register(song,&size,255,65535); put_register(song,&size,255,0); song[size++]=255;
		struct fixture f; create(&f,true,true); load(&f,1,song,size);
		int16_t pcm[2048]; int peak=0;
		for (unsigned i=0;i<64;++i) {
			CHECK(st_audio_runtime_pull(f.audio,pcm,1024)==0);
			for (unsigned j=0;j<2048;++j) if (abs(pcm[j])>peak) peak=abs(pcm[j]);
		}
		/* Premature S16 clipping would cap this -12dB capture near8060.
		 * Encoded TL dynamics must not be normalized away between songs. */
		CHECK(attenuation==0?(peak>20000 && peak<32767):(peak>1000 && peak<2200));
		printf("BIG N nine-carrier TL%u mixed peak %d\n",attenuation,peak); destroy(&f);
	}
}
static void natural_release(void)
{
	uint8_t song[]={0xb5,9,1,0x20,0,0x20,0x21,0,0x23,0x21,0,
	    0x40,63,0,0x43,0,0,0x60,0xf0,0,0x63,0xf0,0,0x80,0x0f,0,0x83,0x0f,0,
	    0xc0,0,0,0xa0,0x44,0,0xb0,0x32,0,0xff,1,0,0xb0,0x12,0,0xff,0,0,0xff};
	for (unsigned held=0;held<2;++held) {
		if (held) song[41]=5; /* Ignored register in place of melodic keyoff. */
		struct fixture f; create(&f,true,true); load(&f,1,song,sizeof(song));
		int16_t pcm[2048]; bool sounded=false;
		for (unsigned i=0;i<44;++i) {
			CHECK(st_audio_runtime_pull(f.audio,pcm,1024)==0);
			for (unsigned j=0;j<2048;++j) if (pcm[j]>1 || pcm[j]<-1) sounded=true;
		}
		CHECK(sounded && state(&f).running==0 && st_music_busy(f.terminal->music_player)==(held!=0));
		if (!held) for (unsigned j=0;j<2048;++j) CHECK(pcm[j]==0);
		CHECK(st_music_wait(f.terminal->music_player)==0);
		poll(&f); CHECK(st_music_busy(f.terminal->music_player)==(held!=0) && state(&f).length==sizeof(song)); destroy(&f);
	}
}
int main(void)
{
	parsing_and_controls(); split_raw_and_absent(); timeout_and_recovery(); maximum_load_timeout();
	sample_progress_and_channel(); muted_progress_and_bad_slice(); real_x7_progress(); rotation_music_rate();
	ansi_music_takeover(); concurrent_lifecycle(); source_levels(); polyphonic_levels(); natural_release(); return 0;
}
