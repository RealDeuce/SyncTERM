/* Actual CTerm APC/capture paths with an external recording MIDI sink. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "music_internal.h"
#include "filesystem.h"
#include "st_threads.h"
#include "st_time.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct recording { st_mutex_t mutex; size_t on, off, closed; uint8_t key, program, note_program; float left, right; };
static char reply[256]; static size_t reply_size;
static int send(void *context, const uint8_t *b, size_t size)
{
	(void)size;
	struct recording *r = context; st_mutex_lock(&r->mutex);
	if (b[0] == 0xc0) r->program = b[1];
	if ((b[0] & 0xf0) == 0x90 && b[2]) { ++r->on; r->key = b[1]; r->note_program = r->program; }
	if ((b[0] & 0xf0) == 0x80) ++r->off;
	st_mutex_unlock(&r->mutex); return 0;
}
static int gain(void *context, float left, float right)
{
	struct recording *r = context; st_mutex_lock(&r->mutex); r->left = left; r->right = right; st_mutex_unlock(&r->mutex); return 0;
}
static void close_sink(void *context) { ++((struct recording *)context)->closed; }
static void response(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	(void)context; CHECK(origin == ST_CTERM_OUTPUT_RESPONSE && size <= sizeof(reply) - reply_size);
	memcpy(reply + reply_size, bytes, size); reply_size += size;
}
static void feed(st_cterm_t *t, const char *text)
{
	/* Every request crosses input-call boundaries, including the ST. */
	for (size_t i = 0; text[i] != 0; ++i) { size_t used; CHECK(st_cterm_write(t, text + i, 1, &used) == 0 && used == 1); }
}
static void expect(const char *text)
{
	CHECK(reply_size == strlen(text) && memcmp(reply, text, reply_size) == 0); reply_size = 0;
}
static void wait_on(struct recording *r, size_t count)
{
	uint64_t start, now; CHECK(st_time_monotonic_ms(&start) == 0);
	for (;;) {
		st_mutex_lock(&r->mutex); bool done = r->on >= count; st_mutex_unlock(&r->mutex);
		if (done) return;
		CHECK(st_time_monotonic_ms(&now) == 0 && now - start < 3000);
		const struct timespec pause = {0, 2000000}; st_thread_sleep(&pause, NULL);
	}
}
static void exercise(st_console_t *console, st_font_catalog_t *fonts, bool utf8)
{
	st_audio_runtime_t *runtime; st_cterm_t *t;
	CHECK(st_audio_runtime_create(&runtime, 0) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts, .audio = runtime,
	    .emulation = ST_CTERM_ANSI_BBS, .encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
	    .codepage = ST_CODEPAGE_CP437, .apc_directory = ST_MIDI_TEST_DIRECTORY, .output = response};
	CHECK(st_cterm_create(&t, &config) == 0);
	struct recording r = {0}; CHECK(st_mutex_init(&r.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	const struct st_midi_sink sink = {.state = &r, .send = send, .gain = gain, .close = close_sink};
	CHECK(st_music_create_with_sink(&t->music_player, runtime, &t->music_stream, &sink) == 0);
	feed(t, "\x1b_SyncTERM:Q;midi\x1b\\"); expect("\x1b[=7;102;1n");
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;fixture.mid\x1b\\");
	wait_on(&r, 1); CHECK(st_music_busy(t->music_player) && t->music_stream == NULL);
	CHECK(r.note_program == 0);
	feed(t, "\x1b_SyncTERM:A;Update;C=0\x1b\\\x1b[=7;0n"); expect("\x1b[=7;0;1n");
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;../fixture.mid\x1b\\");
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;syncterm-cache-name.txt\x1b\\");
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;bad.mid\x1b\\");
	CHECK(st_music_busy(t->music_player));
	feed(t, "\x1b[|MBMLT255L64N83\x0e"); /* queued behind the file */
	feed(t, "\x1b_SyncTERM:A;Volume;C=0;VL=-3dB;VR=-6dB\x1b\\");
	float left, right;
	CHECK(st_music_get_volume(t->music_player, &left, &right) == 0 && left == -3 && right == -6);
	feed(t, "\x1b_SyncTERM:A;Flush;C=0\x1b\\");
	CHECK(!st_music_busy(t->music_player) && r.on == 1);
	CHECK(st_cterm_poll(t) == 0); expect("\x1b[=7;0;0n");
	CHECK(st_cterm_poll(t) == 0 && reply_size == 0);
	feed(t, "\x1b[|MBMLT255L64N0N83\x0e\x1b_SyncTERM:A;Wait;C=0\x1b\\");
	CHECK(!st_music_busy(t->music_player) && r.on == 3 && r.key == 119);
	CHECK(r.note_program == 81);
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;fixture.mid\x1b\\"); wait_on(&r, 4);
	CHECK(r.note_program == 0);
	feed(t, "\x1b" "c"); CHECK(!st_music_busy(t->music_player));
	t->quiet = true;
	feed(t, "\x1b_SyncTERM:Q;midi\x1b\\"); expect("\x1b[=7;102;0n");
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;fixture.mid\x1b\\\x1b[|MBN0\x0e");
	CHECK(!st_music_busy(t->music_player)); t->quiet = false;
	feed(t, "\x1b_SyncTERM:A;QueueMIDI;fixture.mid\x1b\\"); wait_on(&r, 5);
	st_cterm_destroy(&t); CHECK(r.closed == 1);
	st_audio_runtime_destroy(runtime); st_mutex_destroy(&r.mutex);
}
int st_cterm_midi_test_main(void)
{
	static const uint8_t song[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
	    'M','T','r','k',0,0,0,13,0,0x90,60,100,0x87,0x40,0x80,60,0,0,0xff,0x2f,0};
	CHECK(fs_create_directories(ST_MIDI_TEST_DIRECTORY) == 0);
	FILE *file; CHECK(fs_open(&file, ST_MIDI_TEST_DIRECTORY "/fixture.mid", "wb") == 0);
	CHECK(fwrite(song, 1, sizeof(song), file) == sizeof(song) && fclose(file) == 0);
	CHECK(fs_open(&file, ST_MIDI_TEST_DIRECTORY "/syncterm-cache-name.txt", "wb") == 0);
	CHECK(fwrite(song, 1, sizeof(song), file) == sizeof(song) && fclose(file) == 0);
	CHECK(fs_open(&file, ST_MIDI_TEST_DIRECTORY "/bad.mid", "wb") == 0);
	CHECK(fputs("invalid", file) >= 0 && fclose(file) == 0);
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	st_console_t *console; st_font_catalog_t *fonts;
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	exercise(console, fonts, false); exercise(console, fonts, true);
	st_font_catalog_destroy(fonts); st_console_destroy(console); return 0;
}
