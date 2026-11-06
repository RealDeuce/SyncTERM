/* Silent regression tests for remotely supplied decimal parameters. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static void
feed(st_cterm_t *terminal, const char *text, size_t split)
{
	size_t size = strlen(text), consumed;

	CHECK(split <= size);
	CHECK(st_cterm_write(terminal, text, split, &consumed) == 0 && consumed == split);
	CHECK(st_cterm_write(terminal, text + split, size - split, &consumed) == 0
	    && consumed == size - split);
}

static void
music(st_cterm_t *terminal, const char *text)
{
	feed(terminal, "\x1b[|", 0);
	feed(terminal, text, strlen(text) / 2);
	feed(terminal, "\x0e", 0);
	CHECK(terminal->music == 0 && terminal->musicbuf[0] == 0);
}

static void
silent_note(st_cterm_t *terminal, st_audio_runtime_t *audio, const char *text)
{
	/* At T120/L4, 438 ms of tone plus 62 ms of articulation silence
	 * becomes 19315 + 2734 frames after the existing per-part rounding. */
	int16_t frames[22049 * AUDIO_CHANNELS];

	CHECK(audio_stream_clear(terminal->music_stream) == 0);
	music(terminal, text);
	CHECK(!audio_stream_is_idle(terminal->music_stream));
	CHECK(st_audio_runtime_pull(audio, frames, 22048) == 0);
	CHECK(!audio_stream_is_idle(terminal->music_stream));
	for (size_t i = 0; i < 22048 * AUDIO_CHANNELS; ++i)
		CHECK(frames[i] == 0);
	CHECK(st_audio_runtime_pull(audio, frames, 1) == 0);
	CHECK(frames[0] == 0 && frames[1] == 0);
	CHECK(audio_stream_is_idle(terminal->music_stream));
}

static void
music_numbers(st_cterm_t *terminal, st_audio_runtime_t *audio)
{
	char text[1024];
	const char *large[] = {"2147483648", "4294967296", "18446744073709551616"};

	/* Leading zeroes are legal and must not require a temporary buffer. */
	music(terminal, "MBT00000000000000000120O0000000000000000004L0000000000000000004");
	CHECK(terminal->tempo == 120 && terminal->octave == 4 && terminal->notelen == 4);
	for (size_t i = 0; i < sizeof(large) / sizeof(large[0]); ++i) {
		CHECK(snprintf(text, sizeof(text), "MBT%sO%sL%s", large[i], large[i], large[i]) > 0);
		music(terminal, text);
		CHECK(terminal->tempo == 255 && terminal->octave == 6 && terminal->notelen == 64);
	}
	text[0] = 'T';
	memset(text + 1, '9', 1000);
	text[1001] = 0;
	music(terminal, text);
	CHECK(terminal->tempo == 255);
	music(terminal, "MBT0O0L0");
	CHECK(terminal->tempo == 32 && terminal->octave == 0 && terminal->notelen == 1);
	music(terminal, "MBT120O4L4");
	silent_note(terminal, audio, "MBN2147483647+");
	silent_note(terminal, audio, "MBN4294967296");
	silent_note(terminal, audio, "MBN18446744073709551616");

	/* Huge per-note lengths clamp to 64: 28 ms + 3 ms = 1366 frames. */
	music(terminal, "MBP18446744073709551616");
	int16_t frames[1366 * AUDIO_CHANNELS];
	CHECK(st_audio_runtime_pull(audio, frames, 1365) == 0);
	CHECK(!audio_stream_is_idle(terminal->music_stream));
	CHECK(st_audio_runtime_pull(audio, frames, 1) == 0);
	CHECK(audio_stream_is_idle(terminal->music_stream));
	CHECK(terminal->notelen == 4);

	/* Invalid capture must not apply even a previously captured parameter. */
	feed(terminal, "\x1b[|T255?", 5);
	CHECK(terminal->music == 0 && terminal->tempo == 120);
	CHECK(st_cterm_reset_terminal(terminal) == 0);
	music(terminal, "MBT100O2L8");
	CHECK(terminal->tempo == 100 && terminal->octave == 2 && terminal->notelen == 8);
}

static void
font_and_macro_numbers(st_cterm_t *terminal)
{
	/* 2^32 + 48 must not become dynamic font slot 48 on a 64-bit host. */
	terminal->font_slot = 255;
	feed(terminal, "\x1bPCTerm:Font:4294967344:AA==\x1b\\", 18);
	CHECK(terminal->font_slot == 255);
	feed(terminal, "\x1bP0;0;0!zKEEP\x1b\\", 0);
	CHECK(terminal->macro_lens[0] == 4 && memcmp(terminal->macros[0], "KEEP", 4) == 0);
	const char *invalid[] = {
		"\x1bP4294967296;0;0!zBAD\x1b\\",
		"\x1bP0;4294967296;0!zBAD\x1b\\",
		"\x1bP0;0;4294967296!zBAD\x1b\\",
		"\x1bP18446744073709551616;0;0!zBAD\x1b\\",
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		feed(terminal, invalid[i], strlen(invalid[i]) / 2);
		CHECK(terminal->macro_lens[0] == 4 && memcmp(terminal->macros[0], "KEEP", 4) == 0);
	}
	feed(terminal, "\x1bP63;0;0!zLAST\x1b\\", 0);
	CHECK(terminal->macro_lens[63] == 4 && memcmp(terminal->macros[63], "LAST", 4) == 0);
}

int
main(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_audio_runtime_t *audio;

	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	/* Pull-only runtime cannot open a physical audio device. */
	CHECK(st_audio_runtime_create_pull(&audio) == 0);
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		const struct st_cterm_config config = {
			.console = console, .fonts = fonts, .audio = audio,
			.emulation = ST_CTERM_ANSI_BBS, .codepage = ST_CODEPAGE_CP437,
			.encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
		};
		st_cterm_t *terminal;
		CHECK(st_cterm_create(&terminal, &config) == 0);
		music_numbers(terminal, audio);
		font_and_macro_numbers(terminal);
		st_cterm_destroy(&terminal);
	}
	st_audio_runtime_destroy(audio);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	return 0;
}
