/* Independent raw-sample, PCM, parser ordering and cancellation expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_skypix.h"
#include "st_cterm.h"
#include "st_builtin_fonts.h"
#include "st_audio_runtime.h"
#include "audio_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (false)
extern bool st_skypix_test_allocation_failure;
extern bool st_skypix_test_open_failure;
static const uint8_t sample[] = {0,0,0,8,0x1f,0x40,0x80,0xc0,0xff,0,1,0x40,0x7f,0,0x1a,0x1a};
/* Eight signed bytes at period 447. Literal rational linear-interpolation
 * results, before channel gain; final interpolation clamps to the last byte. */
static const int16_t expected[] = {-32768,-29793,-26818,-23843,-20868,-17893,-14941,
	-12012,-9084,-6155,-3226,-298,-211,-164,-118,-71,-25,22,68,115,161,208,254,
	3102,6030,8959,11887,14816,17745,20673,23602,26531,29459,32388,26857,
	20953,15050,9146,3242,0,0,0,0,0,0};
static int16_t mixed(int16_t value)
{
	/* The common mixer applies a Q15 envelope of 32767, then tanh. */
	int32_t product=(int32_t)value*32767;
	int32_t scaled=product>=0?product/32768:-((-product+32767)/32768);
	return (int16_t)(tanh((double)scaled / 32767.0) * 32767.0);
}

struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *t; st_audio_runtime_t *runtime; };
static struct fixture create(enum st_cterm_graphics_mode mode, bool muted)
{
	struct fixture f = {0};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns=80,.rows=25,.cell_width=8,.cell_height=8,.wrap=true};
	CHECK(st_console_create(&f.console,&display,&geometry)==0);
	struct st_console_font font; uint16_t id;
	CHECK(st_builtin_font_get(0,8,8,&font));
	CHECK(st_console_register_static_font(f.console,&font,&id)==0);
	CHECK(st_font_catalog_create(&f.fonts,f.console)==0);
	CHECK((muted ? st_audio_runtime_create(&f.runtime,0) : st_audio_runtime_create_pull(&f.runtime))==0);
	const struct st_cterm_config config = {.console=f.console,.fonts=f.fonts,.audio=f.runtime,
		.emulation=ST_CTERM_ANSI_BBS,.encoding=ST_CTERM_ENCODING_CODEPAGE,
		.codepage=ST_CODEPAGE_CP437,.graphics_mode=mode,.quiet=true};
	CHECK(st_cterm_create(&f.t,&config)==0);
	return f;
}
static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->t);
	st_font_catalog_destroy(f->fonts);
	st_console_destroy(f->console);
	st_audio_runtime_destroy(f->runtime);
}
static size_t feed(st_cterm_t *t,const char *wire)
{
	size_t used;
	CHECK(st_cterm_write(t,wire,strlen(wire),&used)==0);
	return used;
}
static void decoder(struct fixture *f)
{
	CHECK(st_cterm_skypix_load_sample(NULL,sample,sizeof(sample))==IO_ERROR_NOT_SUPPORTED);
	CHECK(st_cterm_skypix_load_sample(f->t,sample,sizeof(sample))==0);
	struct cterm_skypix_audio *a=&f->t->skypix_engine->audio;
	CHECK(a->count==8 && a->period==447 && memcmp(a->samples,sample+6,8)==0);
	uint8_t *old=a->samples;
	for(size_t n=0;n<14;++n) {
		CHECK(st_cterm_skypix_load_sample(f->t,sample,n)==IO_ERROR_INVALID_FORMAT);
		CHECK(a->samples==old);
	}
	uint8_t bad[sizeof(sample)];
	memcpy(bad,sample,sizeof(bad)); memset(bad,0,4);
	CHECK(st_cterm_skypix_load_sample(f->t,bad,sizeof(bad))==IO_ERROR_INVALID_FORMAT);
	memcpy(bad,sample,sizeof(bad)); bad[4]=bad[5]=0;
	CHECK(st_cterm_skypix_load_sample(f->t,bad,sizeof(bad))==IO_ERROR_INVALID_FORMAT);
	memcpy(bad,sample,sizeof(bad)); bad[1]=0x40;bad[3]=1;
	CHECK(st_cterm_skypix_load_sample(f->t,bad,sizeof(bad))==IO_ERROR_OUT_OF_RANGE);
	st_skypix_test_allocation_failure=true;
	CHECK(st_cterm_skypix_load_sample(f->t,sample,sizeof(sample))==IO_ERROR_OUT_OF_MEMORY);
	st_skypix_test_allocation_failure=false;
	CHECK(a->samples==old && a->count==8);
	static const unsigned rates[]={1,54,55,8000,11025,22050,44100,65535};
	static const unsigned periods[]={40601,751,65082,447,324,162,81,54};
	for(size_t i=0;i<sizeof(rates)/sizeof(*rates);++i) {
		memcpy(bad,sample,sizeof(bad));bad[3]=3;bad[4]=(uint8_t)(rates[i]>>8);bad[5]=(uint8_t)rates[i];
		CHECK(st_cterm_skypix_load_sample(f->t,bad,sizeof(bad))==0);
		CHECK(a->count==3 && a->period==periods[i]);
		struct cterm_skypix_audio converter=*a;converter.repeats=2;
		int16_t frames[257*2];size_t total=0;
		while(converter.repeats!=0) total+=cterm_skypix_audio_convert(&converter,frames,257);
		CHECK(total==((uint64_t)6*periods[i]*44100+3579544)/3579545);
	}
	uint8_t alphabet[262]={0,0,1,0,0x1f,0x40};
	for(unsigned i=0;i<256;++i) alphabet[i+6]=(uint8_t)i;
	CHECK(st_cterm_skypix_load_sample(f->t,alphabet,sizeof(alphabet))==0);
	CHECK(a->count==256 && memcmp(a->samples,alphabet+6,256)==0);
	size_t maximum=CTERM_SKYPIX_SAMPLE_MAX;
	uint8_t *large=calloc(maximum+6,1);CHECK(large!=NULL);
	large[1]=0x40;large[4]=0x1f;large[5]=0x40;large[maximum+5]=0x7f;
	CHECK(st_cterm_skypix_load_sample(f->t,large,maximum+6)==0);
	CHECK(a->count==maximum && a->samples[maximum-1]==0x7f);free(large);
	CHECK(st_cterm_skypix_load_sample(f->t,sample,sizeof(sample))==0);
}
static void playback(struct fixture *f)
{
	struct cterm_skypix_audio converter=f->t->skypix_engine->audio;
	converter.repeats=1;
	int16_t raw[45*2];
	CHECK(cterm_skypix_audio_convert(&converter,raw,45)==45 && converter.repeats==0);
	for(size_t i=0;i<45;++i) CHECK(raw[i*2]==expected[i] && raw[i*2+1]==expected[i]);
	st_skypix_test_open_failure=true;
	CHECK(feed(f->t,"\x1b[9;0;0;0;1!X")==13 && !st_cterm_input_paused(f->t));
	st_skypix_test_open_failure=false;
	const char *wire="\x1b[9;99;1;2;1!TAIL";
	CHECK(feed(f->t,wire)==strlen(wire)-4 && st_cterm_input_paused(f->t));
	CHECK(f->t->music_stream==NULL && f->t->fx_stream!=NULL);
	CHECK(audio_stream_set_volume(f->t->fx_stream,0,0)==0);
	int16_t out[45*2];
	for(size_t i=0;i<45;++i) {
		CHECK(st_audio_runtime_pull(f->runtime,out+i*2,1)==0);
		CHECK(out[i*2]==mixed(expected[i]) && out[i*2+1]==mixed(expected[i]));
	}
	CHECK(st_cterm_poll(f->t)==0 && !st_cterm_input_paused(f->t));
	CHECK(feed(f->t,"TAIL")==4);
	/* Zero is deliberately one play. Input splits must not change it. */
	const char *zero="\x1b[9;0;0;0;0!";
	for(size_t i=0;i<strlen(zero);++i) {
		size_t used;CHECK(st_cterm_write(f->t,zero+i,1,&used)==0 && used==1);
	}
	CHECK(st_audio_runtime_pull(f->runtime,out,45)==0);
	for(size_t i=0;i<45;++i) CHECK(out[i*2]==mixed(expected[i]) && out[i*2+1]==mixed(expected[i]));
	CHECK(st_cterm_poll(f->t)==0 && !st_cterm_input_paused(f->t));
	/* Continuous phase across repeats: eight bytes * 3 at period 447
	 * produce ceil(24*447*44100/3579545) = 133 frames, not 3*45. */
	CHECK(feed(f->t,"\x1b[9;0;0;0;3!")==12);
	CHECK(st_cterm_skypix_load_sample(f->t,sample,sizeof(sample))==IO_ERROR_BUSY);
	CHECK(audio_stream_buffered_frames(f->t->fx_stream)==133);
	int16_t repeated[134*2];
	CHECK(st_audio_runtime_pull(f->runtime,repeated,134)==0);
	CHECK(repeated[132*2]==0 && repeated[133*2]==0);
	CHECK(st_cterm_poll(f->t)==0 && !st_cterm_input_paused(f->t));
	/* Reset and EOF cancel the shared SFX queue without disturbing music. */
	CHECK(feed(f->t,"\x1b[9;0;0;0;2!")==12);
	CHECK(st_cterm_finish(f->t)==0 && !st_cterm_input_paused(f->t));
	CHECK(audio_stream_buffered_frames(f->t->fx_stream)==0);
	CHECK(f->t->skypix_engine->audio.count==8);
	CHECK(feed(f->t,"\x1b[9;0;0;0;2!")==12);
	CHECK(st_cterm_reset_terminal(f->t)==0 && !st_cterm_input_paused(f->t));
	CHECK(f->t->skypix_engine->audio.samples==NULL);
	CHECK(feed(f->t,"\x1b[9;0;0;0;1!X")==13);
}
static struct audio_backend_callbacks device;
static bool available(void) { return true; }
static int open_device(void **result,const struct audio_backend_callbacks *callbacks)
{
	device=*callbacks;*result=&device;return 0;
}
static void close_device(void *state) { CHECK(state==&device); }
static uint32_t latency(void *state) { CHECK(state==&device);return 17; }
static const struct audio_backend_descriptor backend={.backend=AUDIO_BACKEND_PORTAUDIO,
	.available=available,.open=open_device,.close=close_device,.latency_ms=latency};
static void device_tests(enum st_cterm_graphics_mode mode)
{
	struct fixture f=create(mode,false);
	audio_engine_t *engine=NULL;
	const struct audio_backend_descriptor *backends[]={&backend};
	CHECK(audio_engine_create_with_backends(&engine,AUDIO_BACKEND_PORTAUDIO,backends,1)==0);
	CHECK(audio_stream_create(&f.t->fx_stream,engine,0,0)==0);
	CHECK(st_cterm_skypix_load_sample(f.t,sample,sizeof(sample))==0);
	CHECK(feed(f.t,"\x1b[9;0;0;0;1!X")==12);
	int16_t out[45*2];
	CHECK(device.pull(device.context,out,45)==45);
	CHECK(cterm_skypix_poll(f.t->skypix_engine,100)==0 && st_cterm_input_paused(f.t));
	CHECK(cterm_skypix_poll(f.t->skypix_engine,116)==0 && st_cterm_input_paused(f.t));
	CHECK(cterm_skypix_poll(f.t->skypix_engine,117)==0 && !st_cterm_input_paused(f.t));
	/* More than 8192 frames remain when the device fails. Refill must
	 * fail and cancel the parser hold rather than waiting on a dead queue. */
	CHECK(feed(f.t,"\x1b[9;0;0;0;1000!")==15);
	device.failed(device.context);
	CHECK(st_cterm_poll(f.t)==0 && !st_cterm_input_paused(f.t));
	CHECK(feed(f.t,"OK")==2);
	/* Destroy cancels an active play before the shared stream is freed. */
	destroy(&f);audio_engine_destroy(engine);
}
static void large(struct fixture *f)
{
	size_t n=131003;
	uint8_t *bytes=malloc(n+6); CHECK(bytes!=NULL);
	bytes[0]=0;bytes[1]=1;bytes[2]=0xff;bytes[3]=0xbb;bytes[4]=0xac;bytes[5]=0x44;
	memset(bytes+6,0x40,n);
	CHECK(st_cterm_skypix_load_sample(f->t,bytes,n+6)==0);free(bytes);
	CHECK(feed(f->t,"\x1b[9;0;0;0;2!")==12);
	int16_t frames[257*2]; size_t total=0;
	while(st_cterm_input_paused(f->t)) {
		size_t queued=audio_stream_buffered_frames(f->t->fx_stream);
		CHECK(queued<=8192);
		size_t take=queued<257?queued:257; CHECK(take>0);
		CHECK(st_audio_runtime_pull(f->runtime,frames,take)==0);
		for(size_t i=0;i<take*2;++i) CHECK(frames[i]==mixed(16384));
		total+=take;CHECK(st_cterm_poll(f->t)==0);
	}
	CHECK(total==((uint64_t)n*2*81*44100+3579544)/3579545);
	CHECK(feed(f->t,"\x1b[9;0;0;0;1!")==12);
	st_audio_runtime_stop(f->runtime);
	CHECK(st_cterm_poll(f->t)==0 && !st_cterm_input_paused(f->t));
}
int main(void)
{
	for(unsigned mode=ST_CTERM_GRAPHICS_SKYPIX_13;mode<=ST_CTERM_GRAPHICS_SKYPIX_31;++mode) {
		struct fixture f=create((enum st_cterm_graphics_mode)mode,false);
		decoder(&f);playback(&f);large(&f);destroy(&f);
		f=create((enum st_cterm_graphics_mode)mode,true);
		CHECK(st_cterm_skypix_load_sample(f.t,sample,sizeof(sample))==0);
		CHECK(feed(f.t,"\x1b[9;0;0;0;0!X")==13 && !st_cterm_input_paused(f.t));
		destroy(&f);
		device_tests((enum st_cterm_graphics_mode)mode);
		f=create((enum st_cterm_graphics_mode)mode,false);
		CHECK(st_cterm_skypix_load_sample(f.t,sample,sizeof(sample))==0);
		CHECK(feed(f.t,"\x1b[9;0;0;0;1!")==12 && st_cterm_input_paused(f.t));
		destroy(&f);
	}
	return 0;
}
