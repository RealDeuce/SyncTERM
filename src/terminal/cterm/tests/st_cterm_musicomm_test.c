/* Musicomm receiver: literal serial/MIDI expectations, no physical output. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_musicomm.h"
#include "music_internal.h"
#include "st_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct recording { uint8_t bytes[131072]; size_t size; };
static struct recording replies;
static int send_midi(void *context, const uint8_t *bytes, size_t size)
{
	struct recording *r = context;
	CHECK(size <= sizeof(r->bytes)-r->size);
	memcpy(r->bytes+r->size, bytes, size); r->size += size;
	return 0;
}
static void reply(void *context, enum st_cterm_output_origin origin,
    const uint8_t *bytes, size_t size)
{
	(void)context; CHECK(origin == ST_CTERM_OUTPUT_RESPONSE);
	(void)send_midi(&replies, bytes, size);
}
static void feed(st_cterm_t *t, const void *bytes, size_t size)
{
	size_t used;
	CHECK(st_cterm_write(t, bytes, size, &used) == 0 && used == size);
}
static void expect_reply(const char *bytes, size_t size)
{
	CHECK(replies.size == size && memcmp(replies.bytes, bytes, size) == 0);
	replies.size = 0;
}
static void exercise(bool utf8, bool midi)
{
	st_console_t *console; st_font_catalog_t *fonts; st_audio_runtime_t *audio;
	st_cterm_t *t;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns=80,.rows=25,.cell_width=8,.cell_height=16};
	CHECK(st_console_create(&console,&display,&geometry) == 0);
	CHECK(st_font_catalog_create(&fonts,console) == 0);
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	st_audio_runtime_set_midi_allowed(audio,0);
	const struct st_cterm_config config = {.console=console,.fonts=fonts,.audio=audio,
	    .emulation=ST_CTERM_ANSI_BBS,.encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,
	    .codepage=ST_CODEPAGE_CP437,.output=reply};
	CHECK(st_cterm_create(&t,&config) == 0);
	struct recording r = {0};
	if (midi) {
		const struct st_midi_sink sink = {.state=&r,.send=send_midi};
		CHECK(st_music_create_with_sink(&t->music_player,audio,&t->music_stream,&sink) == 0);
	}
	feed(t,"A",1);
	CHECK(st_cterm_musicomm_start(t) == 0);
	CHECK(st_cterm_musicomm_start(t) == IO_ERROR_BUSY);
	CHECK(st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_WAITING);
	feed(t,"\xff\r\n\0",4); expect_reply("",0);
	/* Queued startup bytes, even in separate writes, are drained. */
	feed(t,"\x02\x3c",2); expect_reply("\x02",1);
	feed(t,"\x40",1);
	CHECK(st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_START_DRAIN);
	st_cterm_musicomm_idle(t);
	CHECK(st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_PLAYING);
	const uint8_t notes[] = {60,64,60,64,0,0,2,2,10,10,13,13,17,17,19,19,127,127};
	for (size_t split=0; split<=sizeof(notes); ++split) {
		feed(t,notes,split); feed(t,notes+split,sizeof(notes)-split);
	}
	CHECK(st_music_wait(t->music_player) == 0);
	if (midi) {
		CHECK(t->musicomm->midi && !t->musicomm->physical && t->music_stream == NULL);
		/* Initial channel gain controllers are sink policy; find the first
		 * note, then require every event's exact status, pitch and velocity. */
		size_t first=0;
		while (first<r.size && r.bytes[first]!=0x90) first+=3;
		CHECK(r.size-first == (sizeof(notes)+1)*sizeof(notes)*3);
		for (size_t i=0; i<(sizeof(notes)+1)*sizeof(notes); ++i) {
			size_t n=i%sizeof(notes);
			CHECK(r.bytes[first+3*i] == 0x90 && r.bytes[first+3*i+1] == notes[n]);
			CHECK(r.bytes[first+3*i+2] == ((n==0 || n==1 || (n>=4 && n%2==0))?64:0));
		}
	} else {
		CHECK(!t->musicomm->midi);
		feed(t,"\x3c",1);
		int16_t pcm[2048];
		CHECK(st_audio_runtime_pull(audio,pcm,1024) == 0);
		bool nonzero=false;
		for (size_t i=0;i<2048;++i) if (pcm[i]!=0) nonzero=true;
		CHECK(nonzero);
	}
	size_t before_end = r.size;
	feed(t,"\x80LOST",5); expect_reply("\xff\xff\xff",3);
	if (midi) {
		CHECK(r.size >= before_end+384);
		for (unsigned n=0;n<128;++n) {
			CHECK(r.bytes[before_end+3*n] == 0x90);
			CHECK(r.bytes[before_end+3*n+1] == n && r.bytes[before_end+3*n+2] == 0);
		}
	}
	feed(t,"LOST",4);
	st_cterm_musicomm_idle(t);
	CHECK(st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_OFF);
	CHECK(!st_music_busy(t->music_player));
	feed(t,"Z",1);
	struct st_cterm_state state;
	CHECK(st_cterm_state(t,&state) == 0 && state.x == 3 && state.y == 1);
	for (unsigned end=128;end<256;++end) {
		CHECK(st_cterm_musicomm_start(t) == 0);
		feed(t,"\x02",1); st_cterm_musicomm_idle(t);
		uint8_t byte=(uint8_t)end; feed(t,&byte,1);
		expect_reply("\x02\xff\xff\xff",4); st_cterm_musicomm_idle(t);
	}
	if (!midi) {
		CHECK(st_cterm_musicomm_start(t) == 0);
		feed(t,"\x02",1); st_cterm_musicomm_idle(t); expect_reply("\x02",1);
		const uint8_t chord[] = {0,24,108,127};
		feed(t,chord,sizeof(chord));
		struct cterm_musicomm *m=t->musicomm;
		CHECK(m->on[0] && m->on[24] && m->on[108] && m->on[127]);
		CHECK(m->channel[0]==0 && m->channel[24]==1 && m->channel[108]==2 && m->channel[127]==2);
		/* Literal periods from the accessory's $2652 table. Note 127 folds
		 * to 103; note 0 folds to 24, keeping separate toggle flags. */
		CHECK(m->gist.voice[0].parameters[1]==3822 && m->gist.voice[1].parameters[1]==3822);
		CHECK(m->gist.voice[2].parameters[1]==40);
		/* Release the stolen note: the original channel association releases
		 * its replacement while the replacement's logical flag stays set. */
		feed(t,"\x6c",1);
		CHECK(!m->on[108] && m->on[127] && m->gist.voice[2].control==-1);
		int16_t pcm[882*2];
		for (unsigned i=0;i<100;++i) CHECK(st_audio_runtime_pull(audio,pcm,882)==0);
		/* Held pitches inhibit the preset countdown in the actual binary. */
		CHECK(m->gist.voice[0].parameters[0]==200 && m->gist.voice[1].parameters[0]==200);
		CHECK(m->gist.voice[2].parameters[0]==0);
		CHECK(st_cterm_musicomm_stop(t)); expect_reply("\xff\xff\xff",3);
		st_cterm_musicomm_idle(t);
	}
	/* Abort before start, Unicode/local raw/doorway input, reset and EOF. */
	CHECK(st_cterm_musicomm_start(t) == 0);
	CHECK(st_cterm_handle_text_key(t,0x1f600));
	expect_reply("\xff\xff\xff",3); st_cterm_musicomm_idle(t);
	CHECK(st_cterm_musicomm_start(t) == 0);
	CHECK(st_cterm_handle_key(t,'x',0));
	expect_reply("\xff\xff\xff",3); st_cterm_musicomm_idle(t);
	CHECK(st_cterm_musicomm_start(t) == 0);
	CHECK(st_cterm_handle_codepage_key(t,0));
	expect_reply("\xff\xff\xff",3); st_cterm_musicomm_idle(t);
	CHECK(st_cterm_musicomm_start(t) == 0);
	CHECK(st_cterm_handle_doorway_key(t,1));
	expect_reply("\xff\xff\xff",3); st_cterm_musicomm_idle(t);
	CHECK(st_cterm_musicomm_start(t) == 0);
	feed(t,"\x02",1); st_cterm_musicomm_idle(t); expect_reply("\x02",1);
	feed(t,"\x3c",1);
	CHECK(st_cterm_reset_terminal(t) == 0);
	CHECK(!st_music_busy(t->music_player));
	CHECK(st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_OFF);
	CHECK(st_cterm_musicomm_start(t) == 0);
	feed(t,"\x02",1); st_cterm_musicomm_idle(t); expect_reply("\x02",1);
	feed(t,"\x3c",1);
	CHECK(st_cterm_finish(t) == 0);
	CHECK(!st_music_busy(t->music_player));
	CHECK(st_cterm_musicomm_state(t) == ST_CTERM_MUSICOMM_OFF);
	CHECK(st_cterm_musicomm_start(t) == 0);
	feed(t,"\x02",1); st_cterm_musicomm_idle(t); expect_reply("\x02",1);
	feed(t,"\x3c",1);
	if (utf8) st_audio_runtime_stop(audio);
	st_cterm_destroy(&t);
	st_audio_runtime_destroy(audio); st_font_catalog_destroy(fonts); st_console_destroy(console);
}
static void unavailable(void)
{
	for (unsigned mode=0;mode<3;++mode) {
		st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *t;
		st_audio_runtime_t *audio=NULL;
		const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
		const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=16};
		CHECK(st_console_create(&console,&display,&geometry)==0);
		CHECK(st_font_catalog_create(&fonts,console)==0);
		if (mode!=0) CHECK(st_audio_runtime_create(&audio,0)==0);
		if (mode==2) st_audio_runtime_stop(audio);
		const struct st_cterm_config config={.console=console,.fonts=fonts,.audio=audio,
		    .emulation=ST_CTERM_ANSI_BBS,.encoding=ST_CTERM_ENCODING_CODEPAGE,
		    .codepage=ST_CODEPAGE_CP437,.output=reply};
		CHECK(st_cterm_create(&t,&config)==0);
		CHECK(st_cterm_musicomm_start(t)==0);
		feed(t,"\x02",1); st_cterm_musicomm_idle(t); expect_reply("\x02",1);
		feed(t,"\x3c",1);
		CHECK(t->musicomm->on[60] && !t->musicomm->physical && !t->musicomm->midi);
		/* Local muted time advances envelopes without opening an output. */
		t->musicomm->local_ms-=2000;
		CHECK(st_cterm_poll(t)==0);
		CHECK(t->musicomm->gist.voice[0].parameters[0]==200);
		CHECK(t->musicomm->gist.voice[0].parameters[4]==3);
		CHECK(st_cterm_musicomm_stop(t));
		CHECK(st_cterm_musicomm_stop(t)); expect_reply("\xff\xff\xff",3);
		st_cterm_musicomm_idle(t); feed(t,"Z",1);
		st_cterm_destroy(&t); st_audio_runtime_destroy(audio);
		st_font_catalog_destroy(fonts); st_console_destroy(console);
	}
}
int st_cterm_musicomm_test_main(void)
{
	for (unsigned utf8=0;utf8<2;++utf8)
		for (unsigned midi=0;midi<2;++midi) exercise(utf8,midi);
	unavailable();
	return 0;
}
