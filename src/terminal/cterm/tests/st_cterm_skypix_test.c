/* Independent wire, pixel, font, and transfer-boundary expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_skypix.h"
#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_cterm.h"
#include "st_session_history.h"
#include "st_time.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
	do {                                                                                           \
		if (!(x)) {                                                                                \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
			exit(1);                                                                               \
		}                                                                                          \
	} while (false)
struct parser_result {
	unsigned commands;
	unsigned values[17];
	size_t count;
	char name[100];
	char plain[100];
	size_t size;
	bool paused;
};
static int plain(void *context, const uint8_t *bytes, size_t size)
{
	struct parser_result *r = context;
	CHECK(size <= sizeof(r->plain) - r->size);
	memcpy(r->plain + r->size, bytes, size);
	r->size += size;
	return 0;
}
static int parsed(void *context, const unsigned *p, size_t count, const uint8_t *bytes, size_t size)
{
	struct parser_result *r = context;
	++r->commands;
	memcpy(r->values, p, count * sizeof(*p));
	r->count = count;
	CHECK(size < sizeof(r->name));
	if (size != 0)
		memcpy(r->name, bytes, size);
	r->name[size] = 0;
	if (p[0] == 16)
		r->paused = true;
	return 0;
}
static bool paused(void *context) { return ((struct parser_result *)context)->paused; }
static void parser_tests(void)
{
	const char wire[] = "A\x1b[31mB\x1b[10;19!Sapphire.font!C\x1b[16;1;20;10!brush.iff!\nTAIL";
	size_t boundary = strlen(wire) - 5;
	for (size_t split = 0; split <= boundary; ++split) {
		struct cterm_skypix_parser parser = {0};
		struct parser_result r = {0};
		struct cterm_skypix_sink s = {.context = &r, .text = plain, .command = parsed, .paused = paused};
		size_t used;
		CHECK(cterm_skypix_parser_feed(&parser, &s, (const uint8_t *)wire, split, &used) == 0 &&
			  used == split);
		CHECK(cterm_skypix_parser_feed(&parser, &s, (const uint8_t *)wire + split,
									   strlen(wire) - split, &used) == 0 &&
			  used == boundary - split);
		CHECK(r.commands == 2 && r.count == 4 && r.values[1] == 1 &&
			  strcmp(r.name, "brush.iff") == 0);
		CHECK(r.size == 8 && memcmp(r.plain, "A\x1b[31mBC", 8) == 0);
	}
	struct cterm_skypix_parser parser = {0};
	struct parser_result r = {0};
	struct cterm_skypix_sink s = {.context = &r, .text = plain, .command = parsed, .paused = paused};
	size_t used;
	const char malformed[] = "\x1b[1;999999999999999999999!\x1b[15;2!\x1b]x\x1b[15;4!\aZ";
	CHECK(cterm_skypix_parser_feed(&parser, &s, (const uint8_t *)malformed, strlen(malformed),
								   &used) == 0 &&
		  used == strlen(malformed));
	CHECK(r.commands == 2 && r.values[1] == 2 && r.plain[r.size - 1] == 'Z');
}
static void feed(st_cterm_t *t, const char *text)
{
	size_t used;
	CHECK(st_cterm_write(t, text, strlen(text), &used) == 0 && used == strlen(text));
}
/* Raster/position fixtures exclude cursor pixels explicitly. Cursor behaviour
 * is independently checked by st_cterm_skypix_cursor_test. */
static int reset_without_cursor(st_cterm_t *t)
{
	int status = st_cterm_reset_terminal(t);
	if (status == 0) feed(t, "\x1b[?25l");
	return status;
}
static void depth_tests(st_cterm_t *t)
{
	/* ANSI.TXT / SkyTERM RGB4 defaults, independent of the renderer. */
	static const uint32_t expected[16] = {0x000000, 0x1111ff, 0xdddddd, 0xff0000,
		0x00ff11, 0x33aaff, 0xffff22, 0xcc00ee, 0x00bb66, 0x00dddd, 0x00aaff,
		0x0077cc, 0x0000ff, 0x7700ff, 0xcc00ee, 0xcc0088};
	const char mode[] = "\x1b[17;2!";
	for (size_t split = 0; split <= strlen(mode); ++split) {
		CHECK(reset_without_cursor(t) == 0);
		CHECK(t->skypix_engine->colours == 16);
		feed(t, "\x1b[17;1!");
		size_t used;
		CHECK(st_cterm_write(t, mode, split, &used) == 0 && used == split);
		CHECK(st_cterm_write(t, mode + split, strlen(mode) - split, &used) == 0);
		CHECK(t->skypix_engine->colours == 16 && t->attr == 2);
	}
	uint32_t colours[16];
	CHECK(st_console_get_palette(t->screen.console, 512, colours, 16) == 0);
	CHECK(memcmp(colours, expected, sizeof(expected)) == 0);
	for (unsigned pen = 0; pen < 16; ++pen) {
		char wire[64];
		snprintf(wire, sizeof(wire), "\x1b[15;%u!\x1b[1;%u;20!", pen, pen);
		feed(t, wire);
	}
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect row = {0, 20, 16, 1};
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &row, false) == 0);
	for (unsigned pen = 0; pen < 16; ++pen) {
		CHECK(pixels->primary[pen] == 512 + pen);
		CHECK(pixels->alternate[pen] == 512 + pen);
	}
	st_pixel_buffer_destroy(pixels);
	feed(t, "\x1b[H\x1b[15;15!\x1b[18;14!A");
	struct st_pixel_buffer *phases = NULL;
	CHECK(st_pixel_buffer_create(&phases, 1, 1, true) == 0);
	phases->primary[0] = 527; phases->alternate[0] = 526;
	const struct st_pixel_blit blit = {.destination_x = 200, .destination_y = 30,
		.width = 1, .height = 1, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(t->screen.console, phases, NULL, &blit) == 0);
	st_pixel_buffer_destroy(phases);
	uint32_t plane_width, plane_height;
	CHECK(st_console_pixel_size(t->screen.console, &plane_width, &plane_height) == 0);
	bool status_line = t->skypix_engine->height < plane_height;
	if (status_line)
		CHECK(st_console_set_pixel(t->screen.console, 1, (int)t->skypix_engine->height,
			ST_PIXEL_COLOR_RGB_TAG | 0x123456) == 0);
	/* Palette reset/SGR/default-font reset retain the selected depth. */
	feed(t, "\x1b[12!\x1b[0m\x1b[10;0!\x1b[19;100;40!\x1b[15;13!\x1b[18;9!");
	CHECK(t->skypix_engine->colours == 16);
	int x = t->skypix_engine->x, y = t->skypix_engine->y;
	uint8_t attr = t->attr;
	feed(t, "\x1b[99!\x1b[99;0!\x1b[99;1!\x1b[17;0!\x1b[17;3!");
	CHECK(t->skypix_engine->colours == 16 && t->attr == attr);
	CHECK(t->skypix_engine->x == x && t->skypix_engine->y == y);
	feed(t, "\x1b[17;1!");
	CHECK(t->skypix_engine->colours == 8 && t->attr == 2);
	struct st_console_cell cell;
	const struct st_console_rect first_cell = {0, 0, 1, 1}, phase_pixel = {200, 30, 1, 1};
	CHECK(st_console_read_cells(t->screen.console, &first_cell, &cell, 1) == 0);
	CHECK(cell.foreground == 519 && cell.background == 518);
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &phase_pixel, false) == 0);
	CHECK(pixels->primary[0] == 519 && pixels->alternate[0] == 518);
	st_pixel_buffer_destroy(pixels);
	if (status_line) {
		const struct st_console_rect status_pixel = {1, t->skypix_engine->height, 1, 1};
		CHECK(st_console_get_pixels(&pixels, t->screen.console, &status_pixel, false) == 0);
		CHECK(pixels->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | 0x123456));
		st_pixel_buffer_destroy(pixels);
	}
	/* Removing a plane discards bits in existing graphics permanently. */
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &row, false) == 0);
	for (unsigned pen = 0; pen < 16; ++pen)
		CHECK(pixels->primary[pen] == 512 + (pen & 7));
	st_pixel_buffer_destroy(pixels);
	feed(t, "\x1b[17;2!");
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &row, false) == 0);
	for (unsigned pen = 0; pen < 16; ++pen)
		CHECK(pixels->primary[pen] == 512 + (pen & 7));
	st_pixel_buffer_destroy(pixels);
	feed(t, "\x1b[17;1!\x1b[11;0;1;2;3;4;5;6;7;8;9;10;11;12;13;14;15!");
	CHECK(st_console_get_palette(t->screen.console, 512, colours, 16) == 0);
	for (unsigned pen = 0; pen < 16; ++pen)
		CHECK(colours[pen] == pen * 17);
	feed(t, "\x1b[17;2!\x1b[15;15!\x1b[1;40;20!");
	CHECK(reset_without_cursor(t) == 0 && t->skypix_engine->colours == 16);
	const struct st_console_rect reset_pixel = {40, 20, 1, 1};
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &reset_pixel, false) == 0);
	CHECK(pixels->primary[0] == 527 && pixels->alternate[0] == 527);
	st_pixel_buffer_destroy(pixels);
	feed(t, "\x1b[17;1!");
	CHECK(reset_without_cursor(t) == 0 && t->skypix_engine->colours == 16);
}
static void wrapping_tests(st_cterm_t *t)
{
	/* Native Diamond/20 advances with the selected measured-wrap policy.
	 * Writes, explicit flushes and Unicode revisions must agree. */
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		CHECK(st_cterm_set_encoding(t, utf8 ? ST_CTERM_ENCODING_UTF8
			: ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_CP437) == 0);
		/* RoF welcome lines from the user's 2026-11-08 capture. Native
		 * Times/15 declares XSize=7; its 14-pixel bounding box is not the
		 * nominal advance used by JR-Comm's receive forecast. */
		feed(t, "\f\x1b[10;15!Times.font!\x1b[19;85;95!"
			"JoinLink - Messages - Games - Warez - ROF FTN - irc.rofbbs.com");
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 488 && t->skypix_engine->y == 95);
		feed(t, "\x1b[19;75;127!CommodoreNet 64:500/1  *  DiscipleNet 12:316/1  *  FidoNet 1:154/50");
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 481 && t->skypix_engine->y == 127);
		feed(t, "\x1b[10;20!Diamond.font!\x1b[?7h");
		char run[41];
		memset(run, 'i', 33);
		run[33] = 0;
		for (size_t split = 0; split <= 33; ++split) {
			feed(t, "\f");
			size_t used;
			CHECK(st_cterm_write(t, run, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(t, run + split, 33 - split, &used) == 0 && used == 33 - split);
			CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 132 && t->skypix_engine->y == -5);
		}
		memset(run, 'W', 40);
		run[32] = 0;
		feed(t, "\f\x1b[19;100;40!");
		feed(t, run);
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 100 && t->skypix_engine->y == 60);
		run[32] = 'W';
		run[40] = 0;
		feed(t, "\f\x1b[19;0;40!");
		feed(t, run);
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 160 && t->skypix_engine->y == 60);
		feed(t, "\f\x1b[19;0;40!");
		for (unsigned i = 0; i < 40; ++i) {
			feed(t, "W");
			CHECK(st_cterm_finish(t) == 0);
		}
		CHECK(t->skypix_engine->x == 160 && t->skypix_engine->y == 60);
		feed(t, "\f\x1b[19;0;40!\x1b[?7l");
		run[33] = 0;
		feed(t, run);
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 640 && t->skypix_engine->y == 40);
		feed(t, "\x1b[?7h\f\x1b[19;32760;40!");
		feed(t, run);
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 20 && t->skypix_engine->y == 80);
		/* Controls preserve independent CR and LF positioning. */
		feed(t, "\f");
		memset(run, 'i', 32);
		run[32] = 0;
		feed(t, run);
		feed(t, "\ri\nW");
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->x == 24 && t->skypix_engine->y == 15);
		if (utf8) {
			/* Native i advances four pixels; composed acute-i advances
			 * eight. Revising the last glyph replaces its prior advance. */
			feed(t, "\f");
			feed(t, run);
			feed(t, "\xcc\x81i");
			CHECK(st_cterm_finish(t) == 0);
			CHECK(t->skypix_engine->x == 136 && t->skypix_engine->y == -5);
			feed(t, "\f\x1b[19;636;40!i\xcc\x81i");
			CHECK(st_cterm_finish(t) == 0);
			CHECK(t->skypix_engine->x == 4 && t->skypix_engine->y == 60);
		}
		/* H16:11a0 and H17:1c08/1ce0 erase a native-font strike at
		 * baseline-ascent through baseline+descent, preserving position. */
		for (unsigned mode = 0; mode <= 3; ++mode) {
			feed(t, "\f\x1b[15;3!\x1b[18;0!\x1b[4;0;40;639;70!\x1b[19;100;40!");
			char erase[16];
			snprintf(erase, sizeof(erase), "\x1b[%uK", mode);
			feed(t, erase);
			CHECK(st_cterm_finish(t) == 0);
			CHECK(t->skypix_engine->x == 100 && t->skypix_engine->y == 40);
			struct st_pixel_buffer *pixels = NULL;
			const struct st_console_rect rect = {0, 40, 640, 31};
			CHECK(st_console_get_pixels(&pixels, t->screen.console, &rect, false) == 0);
			for (unsigned y = 0; y < 31; ++y)
				for (unsigned x = 0; x < 640; ++x) {
					bool cleared = y >= 5 && y < 25 && (mode == 2 ||
						(mode == 0 && x >= 100) || (mode == 1 && x < 120));
					unsigned expected = cleared ? 512 : 515;
					if (pixels->primary[y * 640 + x] != expected)
						fprintf(stderr, "EL%u (%u,%u): %u, expected %u\n", mode, x, y + 40,
							pixels->primary[y * 640 + x], expected);
					CHECK(pixels->primary[y * 640 + x] == expected);
					CHECK(pixels->alternate[y * 640 + x] == expected);
				}
			st_pixel_buffer_destroy(pixels);
		}
	}
	CHECK(reset_without_cursor(t) == 0);
}
static void put_be32(uint8_t *out, size_t value)
{
	for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (24 - i * 8));
}
static size_t test_ilbm(uint8_t *data, unsigned width, unsigned depth, unsigned compression,
	const uint8_t *body, size_t body_size)
{
	size_t size = 62 + body_size + (body_size & 1);
	memset(data, 0, size);
	memcpy(data, "FORM", 4); put_be32(data + 4, size - 8);
	memcpy(data + 8, "ILBMBMHD", 8); put_be32(data + 16, 20);
	data[20] = (uint8_t)(width >> 8); data[21] = (uint8_t)width;
	data[23] = 1; data[28] = (uint8_t)depth; data[30] = (uint8_t)compression;
	memcpy(data + 40, "CMAP", 4); put_be32(data + 44, 6);
	const uint8_t colours[] = {0x12, 0x34, 0x56, 0xab, 0xcd, 0xef};
	memcpy(data + 48, colours, 6);
	memcpy(data + 54, "BODY", 4); put_be32(data + 58, body_size);
	memcpy(data + 62, body, body_size);
	return size;
}
static void brush_tests(st_cterm_t *t)
{
	feed(t, "\x1b[17;1!"); /* Exercise promotion from explicitly selected eight colours. */
	uint8_t data[256], literal[129];
	literal[0] = 127; memset(literal + 1, 0xff, 128);
	size_t size = test_ilbm(data, 1024, 1, 1, literal, sizeof(literal));
	CHECK(st_cterm_skypix_receive(t, data, size) == 0);
	for (unsigned x = 0; x < 1024; ++x) CHECK(t->skypix_engine->brush->primary[x] == 1);
	const uint8_t repeat[] = {128, 129, 0xff}; /* no-op, repeat 128 */
	size = test_ilbm(data, 1024, 1, 1, repeat, sizeof(repeat));
	CHECK(st_cterm_skypix_receive(t, data, size) == 0);
	for (unsigned x = 0; x < 1024; ++x) CHECK(t->skypix_engine->brush->primary[x] == 1);
	const uint8_t planar[] = {0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0};
	size = test_ilbm(data, 2, 4, 0, planar, sizeof(planar));
	uint32_t preserved = 0x123456, colours[16];
	CHECK(st_console_set_palette(t->screen.console, 516, &preserved, 1) == 0);
	CHECK(st_cterm_skypix_receive(t, data, size) == 0);
	CHECK(t->skypix_engine->colours == 8 && t->skypix_engine->brush_depth == 4);
	CHECK(t->skypix_engine->brush_palette_pending);
	CHECK(t->skypix_engine->brush_palette_size == 2);
	CHECK(t->skypix_engine->brush_palette[0] == 0x113355 && t->skypix_engine->brush_palette[1] == 0xaaccee);
	struct st_pixel_buffer *pending = t->skypix_engine->brush;
	CHECK(st_cterm_skypix_receive(t, "invalid", 7) == IO_ERROR_INVALID_FORMAT);
	CHECK(t->skypix_engine->brush == pending && t->skypix_engine->brush_palette_pending
		&& t->skypix_engine->brush_palette_size == 2);
	feed(t, "\x1b[7;0;0;100;100;2;1;192;255!");
	CHECK(t->skypix_engine->colours == 16 && !t->skypix_engine->brush_palette_pending);
	CHECK(st_console_get_palette(t->screen.console, 512, colours, 16) == 0);
	CHECK(colours[0] == 0x113355 && colours[1] == 0xaaccee && colours[4] == preserved);
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect row = {100, 100, 2, 1};
	CHECK(st_console_get_pixels(&pixels, t->screen.console, &row, false) == 0);
	CHECK(pixels->primary[0] == 527 && pixels->primary[1] == 512);
	st_pixel_buffer_destroy(pixels);
	preserved = 0x224466;
	CHECK(st_console_set_palette(t->screen.console, 512, &preserved, 1) == 0);
	feed(t, "\x1b[7;0;0;102;100;2;1;192;255!");
	CHECK(st_console_get_palette(t->screen.console, 512, colours, 16) == 0 && colours[0] == preserved);
	/* A captured four-plane brush owns the palette it was captured with. */
	feed(t, "\x1b[6;100;99;2;1!");
	CHECK(t->skypix_engine->brush_depth == 4 && t->skypix_engine->brush_palette_pending);
	CHECK(t->skypix_engine->brush_palette[0] == preserved);
	feed(t, "\x1b[17;1!\x1b[7;0;0;104;100;2;1;192;255!");
	CHECK(t->skypix_engine->colours == 16 && !t->skypix_engine->brush_palette_pending);
	CHECK(st_console_get_palette(t->screen.console, 512, colours, 16) == 0 && colours[0] == preserved);
	struct st_pixel_buffer *saved = t->skypix_engine->brush;
	const uint8_t malformed[] = {128, 129};
	size = test_ilbm(data, 1024, 1, 1, malformed, sizeof(malformed));
	CHECK(st_cterm_skypix_receive(t, data, size) == IO_ERROR_INVALID_FORMAT);
	CHECK(t->skypix_engine->brush == saved && t->skypix_engine->brush_depth == 4);
	CHECK(t->skypix_engine->brush_palette[0] == preserved && !t->skypix_engine->brush_palette_pending);
	CHECK(st_cterm_skypix_receive(t, NULL, 0) == 0 && t->skypix_engine->brush == saved);
	CHECK(reset_without_cursor(t) == 0);
}
static void short_delay_tests(st_cterm_t *t)
{
	for (unsigned ticks = 0; ticks < 3; ++ticks) {
		char wire[16];
		snprintf(wire, sizeof(wire), "\x1b[14;%u!", ticks);
		uint64_t before, after;
		CHECK(st_time_monotonic_ms(&before) == 0);
		feed(t, wire);
		CHECK(st_time_monotonic_ms(&after) == 0);
		if (ticks == 0) CHECK(t->skypix_engine->delay_until == 0);
		else CHECK(t->skypix_engine->delay_until >= before + ticks * 20
			&& t->skypix_engine->delay_until <= after + ticks * 20);
		CHECK(reset_without_cursor(t) == 0);
	}
}
struct mouse_output {
	uint8_t bytes[128];
	size_t size;
	enum st_cterm_output_origin origin;
};
static void mouse_capture(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	struct mouse_output *output = context;
	CHECK(size <= sizeof(output->bytes) - output->size);
	memcpy(output->bytes + output->size, bytes, size);
	output->size += size;
	output->origin = origin;
}
static void mouse_tests(st_cterm_t *terminal)
{
	struct mouse_output output = {0};
	st_cterm_output_callback_t saved = terminal->api_output;
	void *saved_context = terminal->api_output_context;
	terminal->api_output = mouse_capture;
	terminal->api_output_context = &output;
	CHECK(st_cterm_mouse_mode(terminal) == 2);
	struct st_console_event event = {.type = ST_CONSOLE_EVENT_MOUSE,
		.data.mouse = {.x = 39, .y = 17, .pixel_x = 317, .pixel_y = 143,
			.action = ST_CONSOLE_MOUSE_BUTTON, .buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT}};
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		CHECK(st_cterm_set_encoding(terminal, utf8 ? ST_CTERM_ENCODING_UTF8 :
			ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_ISO_8859_1) == 0);
		/* No enable/gadget command. ANSI mouse modes must not change
		 * SkyPix's unsolicited, raw-pixel controller-1 report. */
		feed(terminal, "\x1b[?1000h\x1b[?1006h\x1b[?1016h");
		CHECK(st_cterm_mouse_mode(terminal) == 2 && !st_cterm_mouse_sgr_pixels(terminal));
		st_cterm_sync_mouse_buttons(terminal, 0);
		output.size = 0;
		CHECK(st_cterm_handle_mouse(terminal, &event));
		const char report[] = "\x1b[21;1;317;143!";
		CHECK(output.origin == ST_CTERM_OUTPUT_USER && output.size == sizeof(report) - 1 &&
			memcmp(output.bytes, report, output.size) == 0);
		output.size = 0;
		CHECK(!st_cterm_handle_mouse(terminal, &event));
		event.data.mouse.action = ST_CONSOLE_MOUSE_MOVE;
		CHECK(!st_cterm_handle_mouse(terminal, &event));
		event.data.mouse.action = ST_CONSOLE_MOUSE_WHEEL;
		event.data.mouse.wheel_y = 1;
		CHECK(!st_cterm_handle_mouse(terminal, &event));
		event.data.mouse.action = ST_CONSOLE_MOUSE_BUTTON;
		event.data.mouse.buttons = 0;
		CHECK(!st_cterm_handle_mouse(terminal, &event));
		for (uint32_t button = ST_CONSOLE_MOUSE_BUTTON_MIDDLE;
			button <= ST_CONSOLE_MOUSE_BUTTON_RIGHT; button <<= 1) {
			event.data.mouse.buttons = button;
			CHECK(!st_cterm_handle_mouse(terminal, &event));
		}
		CHECK(output.size == 0);
		event.data.mouse.buttons = ST_CONSOLE_MOUSE_BUTTON_LEFT;
		const int32_t positions[][2] = {{-1, 143}, {317, -1}, {640, 143}, {317, 200}};
		for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
			st_cterm_sync_mouse_buttons(terminal, 0);
			event.data.mouse.pixel_x = positions[i][0];
			event.data.mouse.pixel_y = positions[i][1];
			CHECK(!st_cterm_handle_mouse(terminal, &event) && output.size == 0);
		}
		st_cterm_sync_mouse_buttons(terminal, 0);
		event.data.mouse.pixel_x = 0;
		event.data.mouse.pixel_y = 0;
		CHECK(st_cterm_handle_mouse(terminal, &event));
		CHECK(output.size == 11 && memcmp(output.bytes, "\x1b[21;1;0;0!", 11) == 0);
		st_cterm_sync_mouse_buttons(terminal, 0);
		output.size = 0;
		event.data.mouse.pixel_x = 639;
		event.data.mouse.pixel_y = 199;
		CHECK(st_cterm_handle_mouse(terminal, &event));
		CHECK(output.size == sizeof(report) - 1 && memcmp(output.bytes, "\x1b[21;1;639;199!", output.size) == 0);
		event.data.mouse.pixel_x = 317;
		event.data.mouse.pixel_y = 143;
		feed(terminal, "\x1b[?1000l\x1b[?1006l\x1b[?1016l");
	}
	CHECK(reset_without_cursor(terminal) == 0 && st_cterm_mouse_mode(terminal) == 2);
	CHECK(st_cterm_set_encoding(terminal, ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_ISO_8859_1) == 0);
	terminal->api_output = saved;
	terminal->api_output_context = saved_context;
}
static void gadget_tests(st_cterm_t *t)
{
	struct mouse_output output = {0};
	st_cterm_output_callback_t saved = t->api_output;
	void *saved_context = t->api_output_context;
	t->api_output = mouse_capture;
	t->api_output_context = &output;
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		CHECK(st_cterm_set_encoding(t, utf8 ? ST_CTERM_ENCODING_UTF8
			: ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_CP437) == 0);
		const char definition[] = "\x1b[22;1;65535;10;20;14;24!";
		for (size_t split = 0; split <= strlen(definition); ++split) {
			feed(t, "\f\x1b[15;3!");
			size_t used;
			CHECK(st_cterm_write(t, definition, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(t, definition + split, strlen(definition) - split, &used) == 0
				&& used == strlen(definition) - split);
			struct st_pixel_buffer *image = NULL;
			const struct st_console_rect area = {9, 19, 7, 7};
			CHECK(st_console_get_pixels(&image, t->screen.console, &area, false) == 0);
			for (unsigned y = 0; y < 7; ++y)
				for (unsigned x = 0; x < 7; ++x) {
					unsigned expected = x >= 1 && x <= 5 && y >= 1 && y <= 5 ? 515 : 512;
					CHECK(image->primary[y * 7 + x] == expected);
					CHECK(image->alternate[y * 7 + x] == expected);
				}
			st_pixel_buffer_destroy(image);
			const uint16_t expected[] = {65535, 10, 20, 14, 24};
			CHECK(memcmp(t->skypix_engine->gadgets[0], expected, sizeof(expected)) == 0);
		}
		/* Both range endpoints are usable; every slot owns five wire words.
		 * Replacing a record paints anew, without erasing its old pixels. */
		for (unsigned slot = 1; slot <= 20; ++slot) {
			char definition[80];
			snprintf(definition, sizeof(definition), "\x1b[22;%u;%u;30;40;30;40!", slot, 100 + slot);
			feed(t, definition);
			CHECK(t->skypix_engine->gadgets[slot - 1][0] == 100 + slot);
		}
		uint16_t records[20][5];
		memcpy(records, t->skypix_engine->gadgets, sizeof(records));
		feed(t, "\x1b[15;4!\x1b[22;0;1;10;20;14;24!\x1b[22;21;1;10;20;14;24!"
			"\x1b[22;65535;1;10;20;14;24!\x1b[22;1;1;10;20;14!"
			"\x1b[22;1;1;10;20;14;24;99!");
		CHECK(memcmp(records, t->skypix_engine->gadgets, sizeof(records)) == 0);
		const struct st_console_rect retained = {10, 20, 1, 1};
		struct st_pixel_buffer *image = NULL;
		CHECK(st_console_get_pixels(&image, t->screen.console, &retained, false) == 0);
		CHECK(image->primary[0] == 515 && image->alternate[0] == 515);
		st_pixel_buffer_destroy(image);
		image = NULL;
		feed(t, "\x1b[15;3!\x1b[22;20;65537;65535;199;0;201!");
		const uint16_t clipped[] = {1, 65535, 199, 0, 201};
		CHECK(memcmp(t->skypix_engine->gadgets[19], clipped, sizeof(clipped)) == 0);
		const struct st_console_rect bottom = {0, 198, 2, 2};
		CHECK(st_console_get_pixels(&image, t->screen.console, &bottom, false) == 0);
		CHECK(image->primary[0] == 512 && image->primary[2] == 515 && image->primary[3] == 512);
		st_pixel_buffer_destroy(image);
		/* Clear/font/palette changes do not discard retained records. */
		feed(t, "\f\x1b[10;0!\x1b[12!");
		CHECK(memcmp(t->skypix_engine->gadgets[19], clipped, sizeof(clipped)) == 0);
		CHECK(output.size == 0);
		CHECK(reset_without_cursor(t) == 0);
		const uint16_t empty[20][5] = {{0}};
		CHECK(memcmp(t->skypix_engine->gadgets, empty, sizeof(empty)) == 0);
	}
	CHECK(st_cterm_set_encoding(t, ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_ISO_8859_1) == 0);
	t->api_output = saved;
	t->api_output_context = saved_context;
}
static unsigned report_pixel(unsigned x, unsigned y)
{
	/* Independent literal rectangle/fill bounds, plus Pearl/8 strike rows. */
	if (x >= 100 && x <= 550 && y >= 70 && y <= 170)
		return x == 100 || x == 550 || y == 70 || y == 170 ? 515 : 516;
	static const uint8_t rows[3][8] = {
		{0x10, 0x38, 0x6c, 0xc6, 0xfe, 0xc6, 0xc6, 0},
		{0xfc, 0xc6, 0xc6, 0xfc, 0xc6, 0xc6, 0xfc, 0},
		{0xfc, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xfc, 0}};
	static const unsigned glyphs[][3] = {
		{8, 9, 0}, {40, 25, 1}, {72, 41, 0}, {80, 8, 0}, {88, 8, 1}, {96, 8, 2}};
	for (size_t i = 0; i < sizeof(glyphs) / sizeof(*glyphs); ++i) {
		unsigned gx = glyphs[i][0], gy = glyphs[i][1];
		if (x >= gx && x < gx + 8 && y >= gy && y < gy + 8
			&& (rows[glyphs[i][2]][y - gy] & (128u >> (x - gx)))) return 514;
	}
	return 512;
}
static void report_ordering_tests(st_cterm_t *t)
{
	/* Report 647972f: positioned text, two-bang font strings, reset/editor
	 * recovery and immediate outline/fill coherence must not depend on input
	 * segmentation. This is a synthetic fixture, not the missing ACIS capture.
	 * The legitimate filled area exceeds the report patch's 1/4-screen guard. */
	const char wire[] = "\f\x1b[15;3!\x1b[8;100;70!\x1b[2;550;70!\x1b[2;550;170!"
		"\x1b[2;100;170!\x1b[2;100;70!\x1b[15;4!\x1b[3;0;101;71!"
		"\x1b[15;2!\x1b[10;8!Pearl.font!\x1b[19;8;8!A\x1b[19;40;24!B\x1b[19;72;40!A"
		"\x1b[10;20!Diamond.font!\x1b[10;0!\x1b[2;11HABC\b\x1b[KD";
	size_t length = strlen(wire);
	uint32_t *expected = malloc(640 * 200 * sizeof(*expected));
	CHECK(expected != NULL);
	for (unsigned y = 0; y < 200; ++y) for (unsigned x = 0; x < 640; ++x)
		expected[(size_t)y * 640 + x] = report_pixel(x, y);
	for (size_t split = 0; split <= length + 1; ++split) {
		CHECK(reset_without_cursor(t) == 0);
		size_t used;
		if (split <= length) {
			CHECK(st_cterm_write(t, wire, split, &used) == 0 && used == split);
			CHECK(st_cterm_write(t, wire + split, length - split, &used) == 0 && used == length - split);
		} else {
			for (size_t i = 0; i < length; ++i)
				CHECK(st_cterm_write(t, wire + i, 1, &used) == 0 && used == 1);
		}
		CHECK(st_cterm_finish(t) == 0);
		struct st_pixel_buffer *pixels = NULL;
		const struct st_console_rect area = {0, 0, 640, 200};
		CHECK(st_console_get_pixels(&pixels, t->screen.console, &area, false) == 0);
		if (memcmp(pixels->primary, expected, 640 * 200 * sizeof(*expected)) != 0
			|| memcmp(pixels->alternate, expected, 640 * 200 * sizeof(*expected)) != 0) {
			for (unsigned y = 0; y < 200; ++y) for (unsigned x = 0; x < 640; ++x) {
				size_t index = (size_t)y * 640 + x;
				if (pixels->primary[index] != expected[index] || pixels->alternate[index] != expected[index]) {
					fprintf(stderr, "report split=%zu pixel=%u,%u actual=%u/%u expected=%u\n",
						split, x, y, pixels->primary[index], pixels->alternate[index], expected[index]);
					exit(1);
				}
			}
		}
		st_pixel_buffer_destroy(pixels);
		CHECK(!t->skypix_engine->custom && t->skypix_engine->font == t->skypix_engine->default_data);
	}
	free(expected);
}
static void engine_tests(bool status_line, enum st_cterm_graphics_mode graphics)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *t = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80,
											   .rows = status_line ? 26 : 25,
											   .cell_width = 8,
											   .cell_height = 8,
											   .wrap = true};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	struct st_console_font font;
	uint16_t identifier;
	CHECK(st_builtin_font_get(0, 8, 8, &font));
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console,
										   .fonts = fonts,
										   .emulation = ST_CTERM_ANSI_BBS,
										   .encoding = ST_CTERM_ENCODING_CODEPAGE,
										   .codepage = ST_CODEPAGE_ISO_8859_1,
										   .graphics_mode = graphics,
										   .status_line = status_line,
										   .scrollback_lines = 5,
										   .quiet = true};
	CHECK(st_cterm_create(&t, &config) == 0);
	feed(t, "\x1b[?25l");
	struct st_cterm_state state;
	CHECK(st_cterm_state(t, &state) == 0 && state.skypix && state.graphics_mode == graphics);
	CHECK(t->rip_engine == NULL && t->skypix_engine != NULL);
	CHECK(t->skypix_engine->colours == 16);
	mouse_tests(t);
	gadget_tests(t);
	depth_tests(t);
	brush_tests(t);
	short_delay_tests(t);
	report_ordering_tests(t);
	CHECK(st_cterm_resize(t, 100, geometry.rows) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_cterm_resize(t, 80, geometry.rows + 1) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_cterm_resize(t, 80, geometry.rows) == 0);
	CHECK(t->width == 80 && t->height == 25);
	CHECK(st_cterm_set_encoding(t, ST_CTERM_ENCODING_UTF8, ST_CODEPAGE_ISO_8859_1)
		== 0);
	CHECK(st_cterm_set_encoding(t, ST_CTERM_ENCODING_CODEPAGE, ST_CODEPAGE_ISO_8859_1) == 0);
	uint32_t palette[16];
	CHECK(st_console_get_palette(console, 512, palette, 16) == 0);
	CHECK(palette[1] == 0x1111ff && palette[3] == 0xff0000);
	feed(t, "\x1b[15;3!\x1b[4;10;10;14;14!\x1b[6;10;9;5;5!\x1b[7;0;0;20;20;5;5;192;255!");
	feed(t, "\x1b[15;4!\x1b[1;20;20!\x1b[7;0;0;20;20;1;1;96;1!"); /* XOR plane zero only: green 4 ->
																	 5 */
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect rect = {0, 0, 640, 200};
	CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
	CHECK(memcmp(pixels->primary, pixels->alternate, 640 * 200 * sizeof(*pixels->primary)) == 0);
	CHECK(pixels->primary[10 * 640 + 10] == 515 && pixels->primary[14 * 640 + 14] == 515 &&
		  pixels->primary[15 * 640 + 14] == 512);
	CHECK(pixels->primary[20 * 640 + 20] == 517 && pixels->primary[24 * 640 + 24] == 515);
	st_pixel_buffer_destroy(pixels);
	/* Standard ByteRun1 no-op, 128 literals and a 16-byte repeat. The
	 * brush can exceed the viewport; only matching planes participate. */
	uint8_t ilbm[194] = {0};
	memcpy(ilbm, "FORM", 4);
	ilbm[7] = 186;
	memcpy(ilbm + 8, "ILBMBMHD", 8);
	ilbm[19] = 20;
	ilbm[20] = 4;
	ilbm[21] = 128;
	ilbm[23] = 1;
	ilbm[28] = 1;
	ilbm[30] = 1;
	memcpy(ilbm + 40, "CMAP", 4);
	ilbm[47] = 6;
	memcpy(ilbm + 54, "BODY", 4);
	ilbm[61] = 132;
	ilbm[62] = 128;
	ilbm[63] = 127;
	memset(ilbm + 64, 0x55, 128);
	ilbm[192] = 241;
	ilbm[193] = 0x55;
	CHECK(st_cterm_skypix_receive(t, ilbm, sizeof(ilbm)) == 0);
	CHECK(t->skypix_engine->brush->width == 1152 && t->skypix_engine->brush_depth == 1);
	CHECK(t->skypix_engine->brush->primary[0] == 0 && t->skypix_engine->brush->primary[1] == 1);
	CHECK(t->skypix_engine->brush->primary[1031] == 1 &&
		  t->skypix_engine->brush->primary[1032] == 0);
	feed(t, "\x1b[15;4!\x1b[4;100;100;109;100!\x1b[7;0;0;100;100;8;1;192;255!");
	const struct st_console_rect planes = {100, 100, 8, 1};
	CHECK(st_console_get_pixels(&pixels, console, &planes, false) == 0);
	for (unsigned x = 0; x < 8; ++x)
		CHECK(pixels->primary[x] == 516 + (x & 1));
	st_pixel_buffer_destroy(pixels);
	memcpy(ilbm + 40, "JUNK", 4);
	CHECK(st_cterm_skypix_receive(t, ilbm, sizeof(ilbm)) == IO_ERROR_INVALID_FORMAT);
	CHECK(t->skypix_engine->brush->width == 1152);
	feed(t, "\x1b[11;3840;15;0;0;0;0;0;0;0;0;0;0;0;0;0;0!");
	CHECK(st_console_get_palette(console, 512, palette, 16) == 0 && palette[0] == 0xff0000 &&
		  palette[1] == 0x0000ff);
	feed(t, "\x1b[19;100;50!\x1b[10;19!Sapphire.font!R\r\n");
	CHECK(t->skypix_engine->custom && t->skypix_engine->x == 0 && t->skypix_engine->y == 69);
	feed(t, "\x1b[10;0!");
	CHECK(!t->skypix_engine->custom && t->skypix_engine->y == 69);
	feed(t, "\x1b[10;77!missing.font!");
	CHECK(t->skypix_engine->custom && st_amiga_font_height(t->skypix_engine->font) == 8);
	const char delay[] = "\x1b[14;60!\x1b[15;4!";
	size_t used;
	CHECK(st_cterm_write(t, delay, strlen(delay), &used) == 0 && used == 8 &&
		  st_cterm_input_paused(t));
	CHECK(reset_without_cursor(t) == 0 && !st_cterm_input_paused(t));
	feed(t, "\x1b[2J\x1b[H\x1b[19;16;21!\x1b[10;20!Diamond.font!<");
	CHECK(st_cterm_finish(t) == 0);
	const struct st_console_rect overhang = {26, 29, 1, 9};
	CHECK(st_console_get_pixels(&pixels, console, &overhang, false) == 0);
	unsigned ink = graphics == ST_CTERM_GRAPHICS_SKYPIX_13 ? 512 : 514;
	CHECK(pixels->primary[0] == ink && pixels->primary[8] == ink);
	st_pixel_buffer_destroy(pixels);
	feed(t, " ");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &overhang, false) == 0);
	CHECK(pixels->primary[0] == ink && pixels->primary[8] == ink);
	st_pixel_buffer_destroy(pixels);
	/* Text-position probes remain independent of cursor presentation. Named
	 * fonts disable SkyTERM's bitmap cursor; its primitive has a focused test. */
	feed(t, "\x1b[10;20!Diamond.font!\f");
	CHECK(t->skypix_engine->x == 0 && t->skypix_engine->y == -5);
	feed(t, "i");
	CHECK(st_cterm_finish(t) == 0 && t->skypix_engine->x == 4);
	feed(t, "W");
	CHECK(st_cterm_finish(t) == 0 && t->skypix_engine->x == 24);
	feed(t, "\x1b[19;101;40!");
	CHECK(t->skypix_engine->x == 101 && t->skypix_engine->y == 40);
	feed(t, "\x1b[12!\x1b[10;0!");
	/* JR-Comm position probes: CR/LF are independent and BS/HT use
	 * nominal Diamond/20 width, never the last proportional advance. */
	feed(t, "\x1b[19;100;40!\x1b[10;20!Diamond.font!i\r");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(t->skypix_engine->x == 0 && t->skypix_engine->y == 40);
	feed(t, "\n");
	CHECK(t->skypix_engine->x == 0 && t->skypix_engine->y == 60);
	feed(t, "\x1b[19;0;40!i\b");
	CHECK(t->skypix_engine->x == -16 && t->skypix_engine->y == 40);
	feed(t, "\b");
	CHECK(t->skypix_engine->x == -16);
	feed(t, "\x1b[19;100;40!\t");
	CHECK(t->skypix_engine->x == 160 && t->skypix_engine->y == 40);
	feed(t, "\v");
	CHECK(t->skypix_engine->x == 160 && t->skypix_engine->y == 40);
	feed(t, "\x1b[19;13;51!\x1b[10;0!B\rC\nD");
	CHECK(st_cterm_finish(t) == 0);
	CHECK(t->skypix_engine->x == 16 && t->skypix_engine->y == 59);
	CHECK(st_cterm_finish(t) == 0);
	uint32_t width, height;
	wrapping_tests(t);
	CHECK(st_console_pixel_size(console, &width, &height) == 0 && width == 640 &&
		  height == (status_line ? 208u : 200u));
	st_cterm_destroy(&t);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}
static uint32_t stripe_colour(int y, bool alternate, unsigned frozen_before)
{
	if (y < 0)
		return ST_PIXEL_COLOR_RGB_TAG;
	if (!alternate && y >= 16 && y < 24)
		return ST_PIXEL_COLOR_RGB_TAG | ((unsigned)y < frozen_before ? 0xff0000 : 0x0000ff);
	return ST_PIXEL_COLOR_RGB_TAG | (alternate ? 0x020005 : 0x010003) | (uint32_t)y << 8;
}
static void pixel_scroll_test(bool status_line, enum st_cterm_graphics_mode graphics,
	bool utf8, unsigned capacity, unsigned first_distance, unsigned font_height)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *t = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = 80, .rows = status_line ? 26 : 25,
		.cell_width = 8, .cell_height = 8, .wrap = true};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_CP437, .graphics_mode = graphics, .status_line = status_line,
		.amiga_font = st_amiga_font_builtin(font_height == 20 ? "Diamond.font" : "Ruby.font", font_height),
		.scrollback_lines = capacity, .quiet = true};
	CHECK(st_cterm_create(&t, &config) == 0);
	feed(t, "\x1b[?25l");
	CHECK(t->backfilled == 0);
	feed(t, "\x1b[17;2!\x1b[99;0!"); /* Neither command disables normal scrolling. */
	for (unsigned i = 0; i < (font_height == 20 ? 9u : 15u); ++i)
		feed(t, "\r\n");
	if (first_distance == 19)
		feed(t, "\x1b[19;0;174!"); /* One pixel above the last nominal baseline. */
	struct st_pixel_buffer *scene = NULL;
	CHECK(st_pixel_buffer_create(&scene, 640, 200, true) == 0);
	for (unsigned y = 0; y < 200; ++y)
		for (unsigned x = 0; x < 640; ++x) {
			scene->primary[y * 640 + x] = x == 0 && y >= 16 && y < 24 ? 200
				: ST_PIXEL_COLOR_RGB_TAG | 0x010003 | y << 8;
			scene->alternate[y * 640 + x] = ST_PIXEL_COLOR_RGB_TAG | 0x020005 | y << 8;
		}
	uint32_t red = 0xff0000, blue = 0x0000ff;
	CHECK(st_console_set_palette(console, 200, &red, 1) == 0);
	const struct st_pixel_blit blit = {.width = 640, .height = 200, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(console, scene, NULL, &blit) == 0);
	st_pixel_buffer_destroy(scene);
	if (status_line)
		CHECK(st_console_set_pixel(console, 1, 201, ST_PIXEL_COLOR_RGB_TAG | 0x123456) == 0);
	st_session_history_t *detached = NULL;
	CHECK(st_session_history_create(&detached, 100) == 0);
	size_t detached_row = 0;
	for (unsigned scroll = 1; scroll <= 4; ++scroll) {
		unsigned retired = first_distance + (scroll - 1) * font_height;
		CHECK(st_console_begin_update(console) == 0);
		feed(t, "\r\n");
		CHECK(st_cterm_finish(t) == 0);
		CHECK(st_console_end_update(console) == 0);
		CHECK(t->skypix_engine->y + (int)font_height == (font_height == 20 ? 195 : 190));
		if (scroll == 1)
			CHECK(st_console_set_palette(console, 200, &blue, 1) == 0);
		uint32_t columns;
		size_t rows;
		CHECK(st_cterm_scrollback_size(t, &columns, &rows) == 0);
		size_t expected_rows = (retired + 7) / 8;
		if (expected_rows > capacity) expected_rows = capacity;
		if (rows != expected_rows)
			fprintf(stderr, "capacity=%u scroll=%u rows=%zu expected=%zu physical=%d pending=%u\n",
				capacity, scroll, rows, expected_rows, t->backfilled, t->backpartial_rows);
		CHECK(rows == expected_rows);
		int start = t->backstart, position = t->backpos;
		for (size_t row = 0; row < rows; ++row) {
			struct st_pixel_buffer *image = NULL;
			CHECK(st_cterm_copy_history_pixels(t, row, &image) == 0 && image != NULL);
			CHECK(image->width == 640 && image->height == 8);
			for (unsigned y = 0; y < 8; ++y) {
				int original = (int)retired - (int)rows * 8 + (int)row * 8 + (int)y;
				CHECK(image->primary[y * 640] == stripe_colour(original, false, first_distance));
				CHECK(image->alternate != NULL && image->alternate[y * 640] == stripe_colour(original, true, first_distance));
			}
			st_pixel_buffer_destroy(image);
		}
		CHECK(t->backstart == start && t->backpos == position);
		struct st_console_cell *cells = NULL;
		size_t all_rows;
		CHECK(st_cterm_copy_history(t, &cells, &columns, &all_rows) == 0 && all_rows == rows + 25);
		if (scroll == 1 && rows != 0) {
			struct st_pixel_buffer **images = calloc(all_rows, sizeof(*images));
			CHECK(images != NULL);
			for (size_t row = 0; row < all_rows; ++row)
				CHECK(st_cterm_copy_history_pixels(t, row, &images[row]) == 0);
			CHECK(st_session_history_publish(detached, cells, columns, all_rows,
				ST_SCREEN_MODE_AMIGA_640X200, status_line, images) == 0);
			detached_row = rows - 1;
			/* Reset retains history, including an unfinished pixel strip. */
			CHECK(reset_without_cursor(t) == 0);
			feed(t, font_height == 20 ? "\x1b[19;0;175!" : "\x1b[19;0;178!");
		}
		free(cells);
		const struct st_console_rect live = {0, 0, 1, 1};
		struct st_pixel_buffer *image = NULL;
		CHECK(st_console_get_pixels(&image, console, &live, false) == 0);
		uint32_t live_colour = image->primary[0];
		if (!ST_PIXEL_COLOR_IS_RGB(live_colour)) {
			uint32_t rgb;
			CHECK(st_console_get_palette(console, live_colour, &rgb, 1) == 0);
			live_colour = ST_PIXEL_COLOR_RGB_TAG | rgb;
		}
		CHECK(live_colour == stripe_colour((int)retired, false, first_distance));
		CHECK(image->alternate[0] == stripe_colour((int)retired, true, first_distance));
		st_pixel_buffer_destroy(image);
		if (font_height == 12) {
			/* The font's last nominal row does not shorten the raster scroll.
			 * SkyTERM scrolls through the drawable bottom, exposing background. */
			const struct st_console_rect padding = {0, 195, 1, 1};
			image = NULL;
			CHECK(st_console_get_pixels(&image, console, &padding, false) == 0);
			CHECK(image->primary[0] == 512);
			CHECK(image->alternate[0] == 512);
			st_pixel_buffer_destroy(image);
		}
		if (status_line) {
			const struct st_console_rect status_pixel = {1, 201, 1, 1};
			image = NULL;
			CHECK(st_console_get_pixels(&image, console, &status_pixel, false) == 0);
			CHECK(image->primary[0] == (ST_PIXEL_COLOR_RGB_TAG | 0x123456));
			st_pixel_buffer_destroy(image);
		}
	}
	if (capacity != 0) {
		struct st_pixel_buffer *image = NULL;
		CHECK(st_session_history_copy_pixels(detached, detached_row, &image) == 0 && image != NULL);
		for (unsigned y = 0; y < 8; ++y) {
			CHECK(image->primary[y * 640] == stripe_colour((int)first_distance - 8 + (int)y, false, first_distance));
			CHECK(image->alternate[y * 640] == stripe_colour((int)first_distance - 8 + (int)y, true, first_distance));
		}
		st_pixel_buffer_destroy(image);
	}
	st_session_history_destroy(detached);
	if (!status_line && capacity != 0) {
		/* A later ordinary eight-pixel cell scroll must merge the same
		 * pending prefix, rather than lose it or publish a padded gap. */
		CHECK(st_cterm_prepare_disconnect(t) == 0);
		uint32_t columns;
		size_t rows;
		CHECK(st_cterm_scrollback_size(t, &columns, &rows) == 0 && rows > 0);
		struct st_pixel_buffer *image = NULL;
		CHECK(st_cterm_copy_history_pixels(t, rows - 1, &image) == 0 && image != NULL);
		for (unsigned y = 0; y < 8; ++y) {
			int original = (int)(first_distance + 3 * font_height) + (int)y;
			CHECK(image->primary[y * 640] == stripe_colour(original, false, first_distance));
			CHECK(image->alternate[y * 640] == stripe_colour(original, true, first_distance));
		}
		st_pixel_buffer_destroy(image);
	}
	if (font_height == 20 && capacity == 50) {
		/* Measured wrapping uses the same exact pixel LF at
		 * the bottom, including when a partial strip is already pending. */
		char narrow[162];
		memset(narrow, 'i', 161);
		narrow[161] = 0;
		feed(t, narrow);
		CHECK(st_cterm_finish(t) == 0);
		CHECK(t->skypix_engine->y + 20 == 195);
		uint32_t columns;
		size_t rows;
		CHECK(st_cterm_scrollback_size(t, &columns, &rows) == 0 && rows > 0);
		struct st_pixel_buffer *image = NULL;
		CHECK(st_cterm_copy_history_pixels(t, rows - 1, &image) == 0 && image != NULL);
		for (unsigned y = 0; y < 8; ++y) {
			int original = (int)first_distance + 60 + (status_line ? 0 : 8) + 20 - 8 + (int)y;
			CHECK(image->primary[y * 640] == stripe_colour(original, false, first_distance));
			CHECK(image->alternate[y * 640] == stripe_colour(original, true, first_distance));
		}
		st_pixel_buffer_destroy(image);
	}
	CHECK(reset_without_cursor(t) == 0);
	st_cterm_destroy(&t);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}
static void graphics_history_test(void)
{
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 8, .wrap = true};
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_cterm_config config = {.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_ISO_8859_1, .graphics_mode = ST_CTERM_GRAPHICS_SKYPIX_31,
		.scrollback_lines = 3};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	feed(terminal, "\x1b[?25l");
	feed(terminal, "\x1b[10;8!Pearl.font!\x1b[H");
	for (unsigned row = 0; row < 30; ++row)
		feed(terminal, "A\r\n");
	uint32_t columns;
	size_t lines;
	CHECK(st_cterm_scrollback_size(terminal, &columns, &lines) == 0 && lines == 3);
	struct st_pixel_buffer *pixels = NULL;
	CHECK(st_cterm_copy_history_pixels(terminal, 0, &pixels) == 0 && pixels != NULL);
	CHECK(pixels->width == 640 && pixels->height == 8);
	/* Pearl A, first source row 0x10. Historical pixels are RGB snapshots. */
	CHECK(pixels->primary[3] == ST_PIXEL_COLOR_RGB_VALUE(221, 221, 221));
	CHECK(pixels->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(0, 0, 0));
	st_pixel_buffer_destroy(pixels);
	int position = terminal->backpos;
	feed(terminal, "\x1b[19;0;32700!\n");
	CHECK(terminal->backpos == position);
	/* A later fractional font must be able to join older grid text without
	 * rerendering it using the newly selected font or palette. */
	CHECK(reset_without_cursor(terminal) == 0);
	for (unsigned row = 0; row < 25; ++row) feed(terminal, "A\r\n");
	struct st_console_cell text_row[80];
	CHECK(st_cterm_copy_scrollback_rows(terminal, text_row, 80, 1, 2, 1, 0) == 1);
	char text[8];
	size_t required;
	CHECK(st_console_font_glyph_text(console, text_row[0].glyph_font, text_row[0].glyph,
		text, sizeof(text), &required) == 0 && required == 1 && text[0] == 'A');
	feed(terminal, "\x1b[10;20!Diamond.font!\n");
	const unsigned pearl[] = {0x10, 0x38, 0x6c, 0xc6, 0xfe, 0xc6, 0xc6, 0};
	for (size_t row = 0; row < 3; ++row) {
		pixels = NULL;
		CHECK(st_cterm_copy_history_pixels(terminal, row, &pixels) == 0 && pixels != NULL);
		for (unsigned y = 0; y < 8; ++y)
			for (unsigned x = 0; x < 8; ++x) {
				uint32_t expected = ST_PIXEL_COLOR_RGB_TAG |
					(pearl[(4 + y) % 8] & (0x80u >> x) ? 0xdddddd : 0);
				CHECK(pixels->primary[y * pixels->width + x] == expected);
			}
		st_pixel_buffer_destroy(pixels);
	}
	st_cterm_destroy(&terminal);
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
}
int st_cterm_skypix_test_main(void)
{
	for (unsigned graphics = ST_CTERM_GRAPHICS_SKYPIX_13; graphics <= ST_CTERM_GRAPHICS_SKYPIX_31; ++graphics)
		for (unsigned status = 0; status < 2; ++status)
			for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
				pixel_scroll_test(status, graphics, utf8, 0, 20, 20);
				pixel_scroll_test(status, graphics, utf8, 1, 20, 20);
				pixel_scroll_test(status, graphics, utf8, 3, 20, 20);
				pixel_scroll_test(status, graphics, utf8, 50, 20, 20);
				pixel_scroll_test(status, graphics, utf8, 50, 19, 20);
				pixel_scroll_test(status, graphics, utf8, 3, 12, 12);
			}
	graphics_history_test();
	parser_tests();
	engine_tests(false, ST_CTERM_GRAPHICS_SKYPIX_13);
	engine_tests(true, ST_CTERM_GRAPHICS_SKYPIX_13);
	engine_tests(false, ST_CTERM_GRAPHICS_SKYPIX_31);
	engine_tests(true, ST_CTERM_GRAPHICS_SKYPIX_31);
	return 0;
}
