/* IGS 2.21 I selectors: independent state, raw palette and scene expectations.
 * Reference 16, 28, 117, 120, 123; capture bugs are deliberately corrected. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "st_cterm.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; };
static uint64_t local_ms;
static int now(uint64_t *value) { *value = local_ms; return 0; }

static void
create(struct fixture *f, unsigned resolution, bool status)
{
	memset(f, 0, sizeof(*f));
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = resolution == 0 ? 40 : 80, .rows = 25, .cell_width = 8,
		.cell_height = resolution == 2 ? 16 : 8,
	};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {
		.console = f->console, .fonts = f->fonts, .font_selector = 44,
		.emulation = ST_CTERM_ATARI_ST_VT52, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
		.encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_ATARI_ST,
		.status_line = status, .quiet = true, .scrollback_lines = 2,
	};
	CHECK(st_cterm_create(&f->terminal, &config) == 0);
	f->terminal->igs_engine->now_ms = now;
	local_ms = 1000;
}

static void
destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal);
	st_font_catalog_destroy(f->fonts);
	st_console_destroy(f->console);
}

static void
feed(struct fixture *f, const char *wire)
{
	size_t used;
	CHECK(st_cterm_write(f->terminal, wire, strlen(wire), &used) == 0 && used == strlen(wire));
}

static void
mutate(struct cterm_igs *igs)
{
	igs->attributes.marker_type = 4;
	igs->attributes.marker_pen = 0;
	igs->attributes.marker_height = 33;
	igs->attributes.line_type = 3;
	igs->attributes.line_pen = 0;
	igs->attributes.line_width = 5;
	igs->attributes.line_begin = 1;
	igs->attributes.line_end = 2;
	igs->attributes.writing_mode = 3;
	igs->attributes.fill_interior = 4;
	igs->attributes.fill_index = 6;
	igs->attributes.fill_pen = 0;
	igs->attributes.text_pen = 0;
	igs->attributes.text_rotation = 900;
	igs->attributes.text_effects = 15;
	igs->attributes.text_height = 30;
	igs->attributes.text_point_mode = true;
	igs->attributes.fill_perimeter = false;
	igs->attributes.user_line = 0xaaaa;
	igs->attributes.user_fill[0] = 0x1234;
	igs->attributes.text_align_h = 2;
	igs->attributes.text_align_v = 3;
	igs->attributes.text_face = 42;
	igs->patterns[0][0] = 0xffff;
	igs->patterns[1][0] = 0xdead;
	igs->memory_route = 5;
	igs->double_step = 4;
	igs->draw_x = 17;
	igs->draw_y = 23;
	igs->scaling_mode = 1;
	igs->scaling_resolution = 2;
	igs->clipping = false;
	igs->clip_x1 = 42;
}

static void
check_attributes(struct cterm_igs *igs, unsigned resolution)
{
	CHECK(igs->attributes.marker_type == 1 && igs->attributes.marker_pen == 1);
	CHECK(igs->attributes.marker_height == 11 && igs->attributes.writing_mode == 1);
	CHECK(igs->attributes.line_type == 1 && igs->attributes.line_width == 1);
	CHECK(igs->attributes.line_pen == 1 && igs->attributes.fill_pen == 1);
	CHECK(igs->attributes.line_begin == 0 && igs->attributes.line_end == 0);
	CHECK(igs->attributes.fill_interior == 1 && igs->attributes.fill_index == 1);
	CHECK(igs->attributes.text_pen == 1 && igs->attributes.text_rotation == 0);
	CHECK(igs->attributes.text_effects == 0 && !igs->attributes.text_point_mode);
	CHECK(igs->attributes.text_height == (resolution == 2 ? 13 : 6));
	CHECK(igs->memory_route == 0 && igs->double_step == 0);
	/* I2 does not restore these native attributes or IGS interpreter state. */
	CHECK(!igs->attributes.fill_perimeter && igs->attributes.user_line == 0xaaaa);
	CHECK(igs->attributes.user_fill[0] == 0x1234 && igs->patterns[1][0] == 0xdead);
	CHECK(igs->attributes.text_align_h == 2 && igs->attributes.text_align_v == 3);
	CHECK(igs->attributes.text_face == 42 && igs->marker_pen_cache == 1);
	CHECK(!igs->clipping && igs->clip_x1 == 42);
	CHECK(igs->patterns[0][0] == 0 && igs->patterns[0][2] == 0x00c0);
	CHECK(igs->draw_x == 17 && igs->draw_y == 23);
	CHECK(igs->scaling_mode == 1 && igs->scaling_resolution == 2);
}

static void
selectors(void)
{
	/* Native words in physical-register order, manually mapped from 2.21's
	 * original IGS/VDI palette triples with the documented pen mapping. */
	const uint16_t igs_palette[16] = {0x777,0x700,0x070,0,0x007,0x770,0x077,0x546,
	    0x532,0x743,0x043,0x030,0x333,0x210,0x134,0};
	const uint16_t vdi_palette[16] = {0x777,0x700,0x070,0x770,0x007,0x707,0x077,0x555,
	    0x333,0x733,0x373,0x773,0x337,0x737,0x377,0};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		uint32_t saved[16], actual[16];
		CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, saved, 16) == 0);
		mutate(igs);
		feed(&f, "G#I 3:");
		CHECK(igs->attributes.line_width == 5 && igs->memory_route == 5);
		if (resolution == 0) {
			CHECK(memcmp(igs->raw_palette, igs_palette, sizeof(igs_palette)) == 0);
			CHECK(igs->palette[7] == 0xb692db);
		} else if (resolution == 1) {
			CHECK(igs->raw_palette[0] == 0x777 && igs->raw_palette[1] == 0x700);
			CHECK(igs->raw_palette[2] == 0x070 && igs->raw_palette[3] == 0);
		} else {
			CHECK(igs->palette[0] == 0xffffff && igs->palette[1] == 0);
		}
		feed(&f, "G#I 4:");
		if (resolution == 0)
			CHECK(memcmp(igs->raw_palette, vdi_palette, sizeof(vdi_palette)) == 0);
		feed(&f, "G#I 1:");
		CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, actual, 16) == 0);
		unsigned colours = resolution == 0 ? 16 : resolution == 1 ? 4 : 2;
		CHECK(memcmp(actual, saved, colours * sizeof(*actual)) == 0);
		CHECK(igs->attributes.line_width == 5 && igs->double_step == 4);
		feed(&f, "G#I 2:");
		check_attributes(igs, resolution);
		mutate(igs);
		igs->math[0] = -1;
		feed(&f, "G#I 0]:");
		CHECK(igs->attributes.line_width == 5 && igs->memory_route == 5);
		feed(&f, "G#I 6:");
		check_attributes(igs, resolution);
		CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, actual, 16) == 0);
		CHECK(memcmp(actual, saved, colours * sizeof(*actual)) == 0);
		destroy(&f);
	}
}

static void
restore_resolution(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned status = 0; status < 2; ++status) {
			struct fixture f;
			create(&f, resolution, status != 0);
			struct cterm_igs *igs = f.terminal->igs_engine;
			mutate(igs);
			const struct st_console_rect point = {100,100,1,1};
			const uint32_t sentinel = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x123456);
			CHECK(st_console_set_pixel(f.console, 100, 100, sentinel) == 0);
			size_t used;
			CHECK(st_cterm_write(f.terminal, "G#I 5:Z", 7, &used) == 0);
			CHECK(used == (resolution == 2 ? 7u : 6u));
			if (resolution != 2) {
				CHECK(st_cterm_input_paused(f.terminal));
				local_ms = 1016;
				CHECK(st_cterm_poll(f.terminal) == 0 && st_cterm_input_paused(f.terminal));
				struct st_pixel_buffer *pixels = NULL;
				CHECK(st_console_get_pixels(&pixels, f.console, &point, false) == 0);
				CHECK(pixels->primary[0] == sentinel);
				st_pixel_buffer_destroy(pixels);
				local_ms = 1017;
				CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
				CHECK(st_console_get_pixels(&pixels, f.console, &point, false) == 0);
				CHECK(pixels->primary[0] == CTERM_IGS_PALETTE_FIRST);
				CHECK(pixels->alternate[0] == CTERM_IGS_PALETTE_FIRST);
				st_pixel_buffer_destroy(pixels);
				feed(&f, "Z");
			} else {
				struct st_pixel_buffer *pixels = NULL;
				CHECK(st_console_get_pixels(&pixels, f.console, &point, false) == 0);
				CHECK(pixels->primary[0] == sentinel);
				st_pixel_buffer_destroy(pixels);
			}
			CHECK(igs->clipping && igs->clip_x2 == (resolution == 0 ? 319 : 639));
			CHECK(igs->clip_y2 == (resolution == 2 ? 399 : 199));
			CHECK(igs->attributes.line_width == 5 && igs->memory_route == 5);
			CHECK(igs->scaling_mode == 1 && igs->draw_x == 17);
			CHECK(st_cterm_finish(f.terminal) == 0);
			destroy(&f);
		}
}

static void
partitions_and_cancel(void)
{
	const char wire[] = "AG#I>3:I>2:I 4:B";
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f;
			create(&f, resolution, false);
			mutate(f.terminal->igs_engine);
			size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire) - 1; ++i)
					CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal, wire + split, sizeof(wire) - 1 - split, &used) == 0
				    && used == sizeof(wire) - 1 - split);
			}
			CHECK(st_cterm_finish(f.terminal) == 0);
			check_attributes(f.terminal->igs_engine, resolution);
			struct st_console_cell cells[2];
			const struct st_console_rect rect = {0,0,2,1};
			CHECK(st_console_read_cells(f.console, &rect, cells, 2) == 0);
			CHECK(cells[0].glyph == 'A' && cells[1].glyph == 'B');
			destroy(&f);
		}
	for (unsigned cancel = 0; cancel < 3; ++cancel) {
		struct fixture f;
		create(&f, 0, false);
		struct cterm_igs *igs = f.terminal->igs_engine;
		const uint32_t sentinel = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x123456);
		CHECK(st_console_set_pixel(f.console, 100, 100, sentinel) == 0);
		feed(&f, "G#I 5:");
		CHECK(st_cterm_input_paused(f.terminal));
		if (cancel == 0) CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		else if (cancel == 1) CHECK(st_cterm_finish(f.terminal) == 0);
		else CHECK(st_cterm_prepare_disconnect(f.terminal) == 0);
		CHECK(!st_cterm_input_paused(f.terminal));
		local_ms = 9000;
		CHECK(st_cterm_poll(f.terminal) == 0 && igs->pending_resolution == -1);
		feed(&f, "G#I 3:A");
		destroy(&f);
	}
	/* A mode restore is unconditional for saved colour, even when the
	 * current colour resolution differs. Exercise width/history changes. */
	for (unsigned saved = 0; saved < 2; ++saved) {
		struct fixture f;
		create(&f, saved, true);
		struct cterm_igs *igs = f.terminal->igs_engine;
		CHECK(cterm_igs_set_resolution(f.terminal, 1 - saved) == 0);
		CHECK(igs->width == (saved == 0 ? 640u : 320u));
		feed(&f, "G#I 5:");
		local_ms = 1017;
		CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(igs->resolution == saved && f.terminal->width == (saved == 0 ? 40 : 80));
		CHECK(f.terminal->height == 24 && f.terminal->backwidth == f.terminal->width);
		CHECK(f.terminal->status_display_type == 1);
		CHECK(igs->clip_x2 == (saved == 0 ? 319 : 639));
		CHECK(st_cterm_resize(f.terminal, 132, 25) == IO_ERROR_NOT_SUPPORTED);
		destroy(&f);
	}
}

static void
deadline_bounds(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	igs->sync_wait_hz = 50;
	feed(&f, "G#I 5:");
	local_ms = 1019;
	CHECK(st_cterm_poll(f.terminal) == 0 && st_cterm_input_paused(f.terminal));
	local_ms = 1020;
	CHECK(st_cterm_poll(f.terminal) == 0 && !st_cterm_input_paused(f.terminal));
	local_ms = UINT64_MAX - 19;
	size_t used;
	CHECK(st_cterm_write(f.terminal, "G#I 5:A", 7, &used) == IO_ERROR_OUT_OF_RANGE && used == 6);
	CHECK(!st_cterm_input_paused(f.terminal));
	local_ms = 1000;
	feed(&f, "G#I 4:A");
	destroy(&f);
}

int
st_cterm_igs_init_test_main(void)
{
	selectors();
	restore_resolution();
	partitions_and_cancel();
	deadline_bounds();
	return 0;
}
