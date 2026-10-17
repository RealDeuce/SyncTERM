/* Silent, deterministic APC playback and owner-thread notification tests. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm.h"
#include "cterm_apc.h"
#include "audio_internal.h"
#include "io_error.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static struct audio_backend_callbacks device;
static char replies[1024];
static size_t reply_size;

static bool available(void) { return true; }
static int
open_device(void **result, const struct audio_backend_callbacks *callbacks)
{
	device = *callbacks;
	*result = &device;
	return 0;
}
static void close_device(void *state) { CHECK(state == &device); }
static const struct audio_backend_descriptor backend = {
	.backend = AUDIO_BACKEND_PORTAUDIO, .available = available,
	.open = open_device, .close = close_device,
};

static void
response(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	(void)context;
	CHECK(origin == ST_CTERM_OUTPUT_RESPONSE && size <= sizeof(replies) - reply_size);
	memcpy(replies + reply_size, bytes, size);
	reply_size += size;
}

static void
feed(st_cterm_t *terminal, const char *bytes)
{
	size_t consumed, size = strlen(bytes);
	CHECK(st_cterm_write(terminal, bytes, size, &consumed) == 0 && consumed == size);
}

static void
apc(st_cterm_t *terminal, const char *text)
{
	feed(terminal, "\x1b_"); feed(terminal, text); feed(terminal, "\x1b\\");
}

static void
expect(const char *text)
{
	CHECK(reply_size == strlen(text) && memcmp(replies, text, reply_size) == 0);
	reply_size = 0;
}

static void
patch(st_cterm_t *terminal, unsigned slot)
{
	struct cterm_apc_patch *p = &terminal->apc->patches[slot];
	free(p->frames);
	p->count = 16;
	p->frames = malloc(p->count * AUDIO_FRAME_SIZE);
	CHECK(p->frames != NULL);
	for (size_t i = 0; i < p->count; ++i) {
		p->frames[i * 2] = (int16_t)(1000 + i * 400);
		p->frames[i * 2 + 1] = (int16_t)(-1000 - (int)i * 400);
	}
}

static void
check_options(st_cterm_t *terminal, audio_engine_t *engine)
{
	audio_stream_t *stream = terminal->apc->channels[0], *expected;
	int16_t source[32], actual[96], wanted[96];
	struct audio_buffer_options options = AUDIO_BUFFER_OPTIONS_INIT;
	CHECK(audio_stream_create(&expected, engine, 0, 0) == 0);
	CHECK(audio_stream_set_volume(stream, 0, 0) == 0);
	patch(terminal, 0);
	memcpy(source, terminal->apc->patches[0].frames, sizeof(source));
	apc(terminal, "SyncTERM:A;Queue;C=2;S=0;I=4f;O=3f;VL=50;VR=-6dB");
	CHECK(terminal->apc->patches[0].frames == NULL);
	CHECK(device.pull(device.context, actual, 48) == 16);
	options.fade_in_frames = 4; options.fade_out_frames = 3;
	options.volume_left_db = 20.0f * log10f(0.5f); options.volume_right_db = -6;
	CHECK(audio_stream_append(expected, source, 16, &options) == 0);
	CHECK(device.pull(device.context, wanted, 48) == 16);
	CHECK(memcmp(actual, wanted, sizeof(actual)) == 0);
	CHECK(audio_stream_is_idle(stream));

	/* A looping head's Wait is a no-op. Crossfade replaces its infinite
	 * tail with the new patch, with exactly the supplied fade duration. */
	patch(terminal, 0); patch(terminal, 1);
	apc(terminal, "SyncTERM:A;Queue;C=2;S=0;L");
	apc(terminal, "SyncTERM:A;Wait;C=2");
	apc(terminal, "SyncTERM:A;Queue;C=2;S=1;I=5f;X");
	CHECK(device.pull(device.context, actual, 48) > 0);
	options = (struct audio_buffer_options)AUDIO_BUFFER_OPTIONS_INIT;
	options.loop = true;
	CHECK(audio_stream_append(expected, source, 16, &options) == 0);
	options.loop = false; options.crossfade = true; options.fade_in_frames = 5;
	CHECK(audio_stream_append(expected, source, 16, &options) == 0);
	CHECK(device.pull(device.context, wanted, 48) > 0);
	CHECK(memcmp(actual, wanted, sizeof(actual)) == 0);
	CHECK(audio_stream_is_idle(stream));
	apc(terminal, "SyncTERM:A;Wait;C=2");
	audio_stream_destroy(expected);
}

static void
check_controls(st_cterm_t *terminal)
{
	float left, right;
	int16_t frames[64];
	audio_stream_t *stream = terminal->apc->channels[0];
	apc(terminal, "SyncTERM:A;Volume;C=2;V=-9dB;VL=-3dB");
	CHECK(audio_stream_get_volume(stream, &left, &right) == 0 && left == -3 && right == -9);
	apc(terminal, "SyncTERM:A;Volume;C=2;VR=-6dB");
	CHECK(audio_stream_get_volume(stream, &left, &right) == 0 && left == -3 && right == -6);
	patch(terminal, 0);
	apc(terminal, "SyncTERM:A;Queue;C=2;S=0;L");
	apc(terminal, "SyncTERM:A;Volume;C=2;V=-12dB;T=4f");
	CHECK(device.pull(device.context, frames, 8) == 8);
	CHECK(audio_stream_get_volume(stream, &left, &right) == 0 && left == -12 && right == -12);
	apc(terminal, "SyncTERM:A;Update;C=2");
	CHECK(st_cterm_poll(terminal) == 0 && reply_size == 0);
	feed(terminal, "\x1b[=7n"); expect("\x1b[=7;2;1n");
	feed(terminal, "\x1b[=7;2n"); expect("\x1b[=7;2;1n");
	apc(terminal, "SyncTERM:A;Flush;C=2;O=4f");
	CHECK(device.pull(device.context, frames, 32) > 0);
	CHECK(audio_stream_is_idle(stream));
	/* Completion is emitted by the owner poll, never the audio callback. */
	CHECK(reply_size == 0 && st_cterm_poll(terminal) == 0);
	expect("\x1b[=7;2;0n");
	CHECK(st_cterm_poll(terminal) == 0 && reply_size == 0);
	apc(terminal, "SyncTERM:A;Update;C=2");
	CHECK(st_cterm_poll(terminal) == 0 && reply_size == 0);
	patch(terminal, 0); apc(terminal, "SyncTERM:A;Queue;C=2;S=0");
	CHECK(st_cterm_poll(terminal) == 0 && reply_size == 0);
	CHECK(device.pull(device.context, frames, 32) == 16);
	CHECK(st_cterm_poll(terminal) == 0); expect("\x1b[=7;2;0n");
	/* Channels 0/1 alias the existing CTerm streams; Queue is rejected. */
	CHECK(audio_stream_append(terminal->music_stream, frames, 16, NULL) == 0);
	CHECK(audio_stream_append(terminal->fx_stream, frames, 16, NULL) == 0);
	apc(terminal, "SyncTERM:A;Flush;C=0");
	CHECK(audio_stream_is_idle(terminal->music_stream) && !audio_stream_is_idle(terminal->fx_stream));
	apc(terminal, "SyncTERM:A;Flush;C=1");
	CHECK(audio_stream_is_idle(terminal->fx_stream));
	patch(terminal, 0); apc(terminal, "SyncTERM:A;Queue;C=1;S=0");
	CHECK(terminal->apc->patches[0].frames != NULL);
	/* Failed append consumes the patch, but must not double-free it. */
	device.failed(device.context);
	apc(terminal, "SyncTERM:A;Queue;C=2;S=0");
	CHECK(terminal->apc->patches[0].frames == NULL);
}

int
main(void)
{
	const struct audio_backend_descriptor *backends[] = {&backend};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	audio_engine_t *engine;
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_cterm_t *terminal;
	CHECK(audio_engine_create_with_backends(&engine, AUDIO_BACKEND_PORTAUDIO, backends, 1) == 0);
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts, .emulation = ST_CTERM_ANSI_BBS,
		.encoding = ST_CTERM_ENCODING_UTF8, .codepage = ST_CODEPAGE_CP437,
		.quiet = true, .apc_directory = ST_APC_TEST_DIRECTORY, .output = response,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	/* Inject silent streams only at this private test boundary. Production
	 * acquires these through the shared, lazily opened audio runtime. */
	CHECK(audio_stream_create(&terminal->apc->channels[0], engine, -12, -12) == 0);
	CHECK(audio_stream_create(&terminal->music_stream, engine, -12, -12) == 0);
	CHECK(audio_stream_create(&terminal->fx_stream, engine, -12, -12) == 0);
	check_options(terminal, engine);
	check_controls(terminal);
	st_cterm_destroy(&terminal);
	audio_engine_destroy(engine);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}
