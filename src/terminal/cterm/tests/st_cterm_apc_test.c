/* Reference APC command effects, framing, and owned state. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm.h"
#include "cterm_apc.h"
#include "cterm_apc_cache.h"
#include "st_audio_file.h"
#include "st_base64.h"
#include "st_pixel_image.h"
#include "st_md5.h"
#include "filesystem.h"
#if defined(ST_APC_TEST_POSIX)
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

struct capture {char bytes[4096]; size_t size; unsigned unknown;};
struct fixture {
	st_console_t *console;
	st_font_catalog_t *fonts;
	st_cterm_t *terminal;
	struct capture output;
};

static void
response(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct capture *output = context;
	CHECK(origin == ST_CTERM_OUTPUT_RESPONSE && size <= sizeof(output->bytes) - output->size);
	memcpy(output->bytes + output->size, bytes, size);
	output->size += size;
}

static void
unknown(void *context, const char *text, size_t size)
{
	struct capture *output = context;
	CHECK((size == 6 && memcmp(text, "vendor", 6) == 0)
	    || (size == 12 && memcmp(text, "SyncTERM:VER", 12) == 0));
	++output->unknown;
}

static void
open_fixture_with(struct fixture *fixture, const char *directory, enum st_cterm_rip_version rip)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16};
	CHECK(st_console_create(&fixture->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fixture->fonts, fixture->console) == 0);
	const struct st_cterm_config config = {
		.console = fixture->console, .fonts = fixture->fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_UTF8,
		.codepage = ST_CODEPAGE_CP437, .quiet = true,
		.apc_directory = directory, .application_version = "APC test", .rip_version = rip,
		.output = response, .output_context = &fixture->output,
		.apc = unknown, .apc_context = &fixture->output,
	};
	CHECK(st_cterm_create(&fixture->terminal, &config) == 0);
}

static void
open_fixture(struct fixture *fixture)
{
	open_fixture_with(fixture, ST_APC_TEST_DIRECTORY, ST_CTERM_RIP_OFF);
}

static void
close_fixture(struct fixture *fixture)
{
	st_cterm_destroy(&fixture->terminal);
	st_font_catalog_destroy(fixture->fonts);
	st_console_destroy(fixture->console);
}

static void
feed(struct fixture *fixture, const void *bytes, size_t size)
{
	size_t consumed;
	CHECK(st_cterm_write(fixture->terminal, bytes, size, &consumed) == 0 && consumed == size);
}

static void
apc(struct fixture *fixture, const char *text)
{
	feed(fixture, "\x1b_", 2);
	feed(fixture, text, strlen(text));
	feed(fixture, "\x1b\\", 2);
}

static void
expect(struct fixture *fixture, const char *expected)
{
	CHECK(fixture->output.size == strlen(expected));
	CHECK(memcmp(fixture->output.bytes, expected, strlen(expected)) == 0);
	fixture->output.size = 0;
}

static void
blob(struct fixture *fixture, const char *prefix, const void *data, size_t size)
{
	size_t encoded_size;
	char *encoded, *command;
	CHECK(st_base64_encoded_size(size, &encoded_size));
	encoded = malloc(encoded_size + 1);
	command = malloc(strlen(prefix) + encoded_size + 2);
	CHECK(encoded != NULL && command != NULL);
	CHECK(st_base64_encode(encoded, encoded_size + 1, data, size));
	(void)snprintf(command, strlen(prefix) + encoded_size + 2, "%s;%s", prefix, encoded);
	apc(fixture, command);
	free(command); free(encoded);
}

static void
check_framing(struct fixture *fixture)
{
	const char command[] = "\x1b_SyncTERM:VER\x1b\\";
	for (size_t split = 0; split <= sizeof(command) - 1; ++split) {
		feed(fixture, command, split);
		if (split < sizeof(command) - 1) CHECK(fixture->output.size == 0);
		feed(fixture, command + split, sizeof(command) - 1 - split);
		expect(fixture, "\x1b_SyncTERM:VER;APC test\x1b\\");
	}
	for (size_t i = 0; i < sizeof(command) - 1; ++i) feed(fixture, command + i, 1);
	expect(fixture, "\x1b_SyncTERM:VER;APC test\x1b\\");
	apc(fixture, "vendor");
	CHECK(fixture->output.unknown == 1);
	/* NUL is ignored inside an APC, and no response precedes ST. */
	static const char nul_command[] = "\x1b_Sync\0TERM:VER\x1b\\";
	feed(fixture, nul_command, sizeof(nul_command) - 1);
	expect(fixture, "\x1b_SyncTERM:VER;APC test\x1b\\");
	/* Active remote status display must dispatch to the owning terminal,
	 * not a callback-less status subterminal. */
	feed(fixture, "\x1b[2$~\x1b[1$}", 10);
	apc(fixture, "SyncTERM:VER");
	expect(fixture, "\x1b_SyncTERM:VER;APC test\x1b\\");
	feed(fixture, "\x1b[0$}\x1b[0$~", 10);
}

static void
check_configuration(void)
{
	struct fixture fixture = {0};
	/* Existing callback-only consumers are not opted into cache access. */
	open_fixture_with(&fixture, NULL, ST_CTERM_RIP_OFF);
	apc(&fixture, "SyncTERM:VER");
	CHECK(fixture.terminal->apc == NULL && fixture.output.unknown == 1 && fixture.output.size == 0);
	close_fixture(&fixture);
	fixture = (struct fixture){0};
	/* A regular file cannot serve as the cache root. Reference cache
	 * failure consumes even queries and unknown APCs, without a callback. */
	open_fixture_with(&fixture, ST_APC_TEST_DIRECTORY "/apc-test.dat", ST_CTERM_RIP_OFF);
	apc(&fixture, "SyncTERM:VER"); apc(&fixture, "vendor");
	CHECK(fixture.output.unknown == 0 && fixture.output.size == 0);
	close_fixture(&fixture);
	const enum st_cterm_rip_version versions[] = {ST_CTERM_RIP_1, ST_CTERM_RIP_3};
	for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
		fixture = (struct fixture){0};
		open_fixture_with(&fixture, ST_APC_TEST_DIRECTORY, versions[i]);
		apc(&fixture, "SyncTERM:VER");
		expect(&fixture, "\x1b_SyncTERM:VER;APC test\x1b\\");
		close_fixture(&fixture);
	}
}

static void
check_cache(struct fixture *fixture)
{
	FILE *stream;
	char bytes[4] = {0};
	apc(fixture, "SyncTERM:C;S;apc-test.dat;Y W\nJj");
	stream = cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "apc-test.dat", false);
	CHECK(stream != NULL && fread(bytes, 1, 4, stream) == 3 && memcmp(bytes, "abc", 3) == 0);
	CHECK(fclose(stream) == 0);
	apc(fixture, "SyncTERM:C;L;apc-test.dat");
	expect(fixture, "\x1b_SyncTERM:C;L\napc-test.dat\t900150983cd24fb0d6963f7d28e17f72\n\x1b\\");
	apc(fixture, "SyncTERM:C;S;nested/empty.dat;");
	stream = cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "nested/empty.dat", false);
	CHECK(stream != NULL && fgetc(stream) == EOF && fclose(stream) == 0);
	apc(fixture, "SyncTERM:C;L;nested/*.dat");
	expect(fixture, "\x1b_SyncTERM:C;L\nempty.dat\td41d8cd98f00b204e9800998ecf8427e\n\x1b\\");
	apc(fixture, "SyncTERM:C;L;../*");
	expect(fixture, "\x1b_SyncTERM:C;L\n\x1b\\");
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "../escape", true) == NULL);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "/escape", true) == NULL);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "nested/./escape", true) == NULL);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "syncterm-cache-name.txt", true) == NULL);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "SYNCTERM-CACHE-NAME.TXT", false) == NULL);
	CHECK(!cterm_apc_cache_name_valid("nested/syncterm-cache-name.txt", false, false));
	CHECK(!cterm_apc_cache_name_valid("nested\\SYNCTERM-CACHE-NAME.TXT", false, true));
	CHECK(!cterm_apc_cache_name_valid("NUL", false, true));
	CHECK(!cterm_apc_cache_name_valid("file:stream", false, true));
	const char *metadata = ST_APC_TEST_DIRECTORY "/syncterm-cache-name.txt";
	CHECK(fs_replace_file(metadata, "APC test", 8) == 0);
	apc(fixture, "SyncTERM:C;S;syncterm-cache-name.txt;YWJj");
	CHECK(fs_open(&stream, metadata, "rb") == 0);
	char owner[9] = {0};
	CHECK(fread(owner, 1, sizeof(owner), stream) == 8);
	CHECK(fclose(stream) == 0 && strcmp(owner, "APC test") == 0);
	apc(fixture, "SyncTERM:C;L;syncterm-cache-*");
	expect(fixture, "\x1b_SyncTERM:C;L\n\x1b\\");
	CHECK(fs_remove_file(metadata) == 0);
}

static void
check_graphics(struct fixture *fixture)
{
	const char ppm[] = "P3\n3 2\n255\n0 0 0 255 255 255 128 128 128\n255 255 255 0 0 0 64 64 64\n";
	const char pbm[] = "P1\n3 2\n1 0 1 0 1 0\n";
	struct st_pixel_buffer *frame = NULL, *background = NULL;
	const struct st_console_rect area = {0,0,8,8};
	const uint32_t green = ST_PIXEL_COLOR_RGB_VALUE(0,255,0);
	const struct st_pixel_blit blit = {.width = 8, .height = 8, .scale_x = 1, .scale_y = 1};
	struct cterm_apc_state *state = fixture->terminal->apc;

	blob(fixture, "SyncTERM:C;LoadPPMBlob;B=1", ppm, sizeof(ppm) - 1);
	CHECK(state->images[1] != NULL && state->images[1]->width == 3 && state->images[1]->height == 2);
	CHECK(state->images[1]->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(0,0,0));
	CHECK(state->images[1]->primary[1] == ST_PIXEL_COLOR_RGB_VALUE(255,255,255));
	CHECK(state->images[1]->primary[2] == ST_PIXEL_COLOR_RGB_VALUE(140,140,140));
	CHECK(state->images[1]->primary[5] == ST_PIXEL_COLOR_RGB_VALUE(79,79,79));
	CHECK(st_pixel_buffer_create(&background, 8, 8, false) == 0);
	CHECK(st_pixel_buffer_fill(background, green, green) == 0);
	CHECK(st_console_blit_pixels(fixture->console, background, NULL, &blit) == 0);
	apc(fixture, "SyncTERM:P;Paste;B=1;DX=1;DY=1;ZX=2;ZY=2;FX");
	CHECK(st_console_get_pixels(&frame, fixture->console, &area, false) == 0);
	CHECK(frame->primary[0] == green);
	CHECK(frame->primary[1 + 8] == ST_PIXEL_COLOR_RGB_VALUE(140,140,140));
	CHECK(frame->primary[5 + 8] == ST_PIXEL_COLOR_RGB_VALUE(0,0,0));
	st_pixel_buffer_destroy(frame); frame = NULL;
	blob(fixture, "SyncTERM:C;LoadPBMBlob", pbm, sizeof(pbm) - 1);
	CHECK(state->mask != NULL && state->mask->width == 3 && state->mask->height == 2);
	CHECK(st_console_blit_pixels(fixture->console, background, NULL, &blit) == 0);
	apc(fixture, "SyncTERM:P;Paste;B=1;MBUF");
	CHECK(st_console_get_pixels(&frame, fixture->console, &area, false) == 0);
	CHECK(frame->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(0,0,0));
	CHECK(frame->primary[1] == green && frame->primary[8] == green);
	CHECK(frame->primary[2] == ST_PIXEL_COLOR_RGB_VALUE(140,140,140));
	st_pixel_buffer_destroy(frame);
	apc(fixture, "SyncTERM:P;Copy;B=0;W=3;H=2");
	CHECK(state->images[0] != NULL && state->images[0]->width == 3 && state->images[0]->height == 2);
	struct st_pixel_buffer *saved = state->images[0];
	CHECK(st_cterm_reset_terminal(fixture->terminal) == 0);
	CHECK(state->images[0] == saved && state->mask != NULL);
	st_pixel_buffer_destroy(background);
}

#if defined(ST_APC_TEST_POSIX)
static void
check_cache_links(void)
{
	char outside[4096], link[4096];
	char bytes[9] = {0};
	struct cterm_apc_cache_entries entries = {0};
	FILE *stream;

	CHECK(snprintf(outside, sizeof(outside), "%s-outside.dat", ST_APC_TEST_DIRECTORY) > 0);
	CHECK(snprintf(link, sizeof(link), "%s/file-link", ST_APC_TEST_DIRECTORY) > 0);
	stream = fopen(outside, "wb");
	CHECK(stream != NULL && fwrite("sentinel", 1, 8, stream) == 8 && fclose(stream) == 0);
	(void)unlink(link);
	CHECK(symlink(outside, link) == 0);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "file-link", false) == NULL);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "file-link", true) == NULL);
	CHECK(cterm_apc_cache_list(&entries, ST_APC_TEST_DIRECTORY, "file-*") == 0 && entries.count == 0);
	cterm_apc_cache_entries_free(&entries);
	stream = fopen(outside, "rb");
	CHECK(stream != NULL && fread(bytes, 1, 8, stream) == 8 && memcmp(bytes, "sentinel", 8) == 0 && fclose(stream) == 0);
	CHECK(unlink(link) == 0);
	CHECK(snprintf(link, sizeof(link), "%s/directory-link", ST_APC_TEST_DIRECTORY) > 0);
	(void)unlink(link);
	CHECK(symlink(ST_APC_TEST_DIRECTORY, link) == 0);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "directory-link/apc-test.dat", false) == NULL);
	CHECK(cterm_apc_cache_open(ST_APC_TEST_DIRECTORY, "directory-link/apc-test.dat", true) == NULL);
	CHECK(cterm_apc_cache_list(&entries, ST_APC_TEST_DIRECTORY, "directory-link/*") == 0 && entries.count == 0);
	cterm_apc_cache_entries_free(&entries);
	CHECK(!cterm_apc_cache_ready(link));
	CHECK(unlink(link) == 0);
}
#endif

static void
check_audio(struct fixture *fixture)
{
	struct cterm_apc_state *state = fixture->terminal->apc;
	apc(fixture, "SyncTERM:A;Synth;S=2;W=SILENCE;T=4f");
	CHECK(state->patches[2].count == 4 && state->patches[2].frames != NULL);
	for (unsigned i = 0; i < 8; ++i) CHECK(state->patches[2].frames[i] == 0);
	apc(fixture, "SyncTERM:A;Copy;S=2;D=3");
	CHECK(state->patches[2].frames != state->patches[3].frames);
	CHECK(state->patches[3].count == 4 && memcmp(state->patches[2].frames, state->patches[3].frames, 16) == 0);
	apc(fixture, "SyncTERM:A;Queue;C=0;S=2");
	CHECK(state->patches[2].frames != NULL);
	apc(fixture, "SyncTERM:A;Queue;C=2;S=2"); /* No runtime: retain patch. */
	CHECK(state->patches[2].frames != NULL);
	apc(fixture, "SyncTERM:A;Synth;S=8;W=SQ;F=30000;T=600f");
	CHECK(state->patches[8].frames != NULL && state->patches[8].count == 600);
	apc(fixture, "SyncTERM:A;Synth;S=9;W=SINE_SAW_HARM;F=1e100;T=400f");
	CHECK(state->patches[9].frames != NULL && state->patches[9].count == 400);
	for (unsigned i = 0; i < 800; ++i) CHECK(state->patches[9].frames[i] == 0);
	feed(fixture, "\x1b[=7;0n", 7);
	expect(fixture, "\x1b[=7;0;0n");
	feed(fixture, "\x1b[=7n", 5);
	expect(fixture, "\x1b[=7n");
	apc(fixture, "SyncTERM:A;Update;C=0");
	state->was_running[0] = true;
	CHECK(st_cterm_poll(fixture->terminal) == 0);
	expect(fixture, "\x1b[=7;0;0n");
	CHECK(st_cterm_poll(fixture->terminal) == 0 && fixture->output.size == 0);
	apc(fixture, "SyncTERM:Q;libsndfile");
	expect(fixture, st_audio_file_available() ? "\x1b[=7;100;1n" : "\x1b[=7;100;0n");
	apc(fixture, "SyncTERM:Q;libsndfileFormat;1;2");
	expect(fixture, st_audio_file_format_available(1,2) ? "\x1b[=7;101;1;2;1n" : "\x1b[=7;101;1;2;0n");
}

static void
check_fonts(struct fixture *fixture)
{
	uint8_t font[10240];
	uint16_t active[4], glyph_font;
	uint32_t glyph;
	uint8_t columns;
	const size_t sizes[] = {2048,3584,4096,10240};
	const uint32_t widths[] = {8,8,8,12}, heights[] = {8,14,16,20};
	const char font_start[] = "\x1b[=46;0{";

	memset(font, 0xf0, sizeof(font));
	feed(fixture, font_start, sizeof(font_start) - 1);
	feed(fixture, font, 4096);
	CHECK(fixture->terminal->font_slot == 46 && fixture->terminal->screen.font_valid[46]);
	for (unsigned i = 0; i < 4; ++i) {
		char store[80], load[80];
		memset(font, (int)(0x80U >> i), sizes[i]);
		(void)snprintf(store, sizeof(store), "SyncTERM:C;S;font%u.bin", i);
		(void)snprintf(load, sizeof(load), "SyncTERM:C;SetFont;47;font%u.bin", i);
		blob(fixture, store, font, sizes[i]);
		apc(fixture, load);
		active[i] = fixture->terminal->screen.remote_fonts[46].identifiers[i];
		CHECK((fixture->terminal->screen.remote_fonts[46].present & (1U << i)) != 0);
		CHECK(!fixture->terminal->screen.font_valid[47]);
	}
	for (unsigned i = 0; i < 4; ++i) {
		const struct st_console_config geometry = {
			.columns = 80, .rows = 25, .cell_width = widths[i], .cell_height = heights[i],
		};
		CHECK(st_console_reconfigure(fixture->console, &geometry) == 0);
		CHECK(st_cterm_resize(fixture->terminal, 80, 25) == 0);
		CHECK(fixture->terminal->screen.font_identifiers[46] == active[i]);
		CHECK(st_console_font_lookup(fixture->console, active[i], 'A', &glyph_font, &glyph, &columns) == 0);
		CHECK(glyph_font == active[i] && glyph == 'A' && columns == 1);
	}
	CHECK(fixture->terminal->screen.remote_fonts[46].present == 15);
}

static void
check_reference_pcm(struct fixture *fixture)
{
	/* Canonical little-endian PCM captured from the unmodified v1.10a
	 * binary by compare_legacy_apc_live.py; no audio device is involved. */
	static const struct {const char *shape, *frequency, *duration, *digest; size_t count;} cases[] = {
		{"SIN", "440", "600f", "b31b93d98b0a346e5356996d54d882af", 600},
		{"SAW", "440", "600f", "170eca44f4c74f5b4c7ebbcd6c89c3cd", 600},
		{"SQ", "440", "600f", "12a90ffffe3f514ee414628062703231", 600},
		{"SINE_HARM", "440", "600f", "a1baa8b4226cf585898bf6b0b6bceb1b", 600},
		{"SINE_SAW", "440", "600f", "9bd8d735c988b66ba5a5668198e8c718", 600},
		{"SINE_SAW_CHORD", "440", "600f", "dc910958ff7b71c8992c5ef4e6afdc50", 600},
		{"SINE_SAW_HARM", "440", "600f", "f5904a7db10e00480cbf55ed4351a636", 600},
		{"SAW", "1000", "1000f", "885195505c054ed2430fd4f4fd88d04f", 1000},
		{"SQ", "30000", "600f", "5be28002868969cd4512b0e91a389a52", 600},
		{"SILENCE", "0", "5ms", "e2bdd4d017ce36dec632e386e894a4e5", 220},
		{"SIN", "440", "2p", "7c4e56b4f98b748c2a26b84c265d761b", 200},
	};
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		char command[128], hex[33];
		uint8_t digest[16];
		struct st_md5 context;
		struct cterm_apc_patch *patch = &fixture->terminal->apc->patches[20];

		(void)snprintf(command, sizeof(command), "SyncTERM:A;Synth;S=20;W=%s;F=%s;T=%s",
		    cases[i].shape, cases[i].frequency, cases[i].duration);
		apc(fixture, command);
		CHECK(patch->frames != NULL && patch->count == cases[i].count);
		st_md5_init(&context);
		for (size_t sample = 0; sample < patch->count * 2; ++sample) {
			uint16_t value = (uint16_t)patch->frames[sample];
			uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
			st_md5_update(&context, bytes, sizeof(bytes));
		}
		st_md5_finish(&context, digest);
		for (size_t byte = 0; byte < 16; ++byte) {
			hex[byte * 2] = digits[digest[byte] >> 4];
			hex[byte * 2 + 1] = digits[digest[byte] & 15];
		}
		hex[32] = 0;
		if (strcmp(hex, cases[i].digest) != 0)
			fprintf(stderr, "%s: expected %s, got %s\n", command, cases[i].digest, hex);
		CHECK(strcmp(hex, cases[i].digest) == 0);
	}
}

int
main(void)
{
	struct fixture fixture = {0}, second = {0};
	open_fixture(&fixture);
	check_framing(&fixture); check_cache(&fixture);
	check_configuration();
#if defined(ST_APC_TEST_POSIX)
	check_cache_links();
#endif
	check_graphics(&fixture); check_audio(&fixture); check_reference_pcm(&fixture);
	check_fonts(&fixture);
	open_fixture(&second);
	CHECK(second.terminal->apc != fixture.terminal->apc && second.terminal->apc->images[0] == NULL);
	CHECK(second.terminal->apc->mask == NULL && second.terminal->apc->patches[2].frames == NULL);
	close_fixture(&second); close_fixture(&fixture);
	return 0;
}
