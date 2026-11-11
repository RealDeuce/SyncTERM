/* IGS 2.21 R: independent transitions, palette order and retained native data.
 * Reference 16, 18 and 28. No external reference assets are needed at runtime. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "st_graphics_parser.h"
#include "st_igs_image.h"
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
static const uint32_t desktop[16] = {
	0xffffff,0xff0000,0x00ff00,0xffff00,0x0000ff,0xff00ff,0x00ffff,0xb6b6b6,
	0x6d6d6d,0xff6d6d,0x6dff6d,0xffff6d,0x6d6dff,0xff6dff,0x6dffff,0,
};
static const uint16_t igs_words[16] = {
	0x777,0x700,0x070,0,0x007,0x770,0x077,0x546,0x532,0x743,0x043,0x030,0x333,0x210,0x134,0,
};
static const uint16_t vdi_words[16] = {
	0x777,0x700,0x070,0x770,0x007,0x707,0x077,0x555,0x333,0x733,0x373,0x773,0x337,0x737,0x377,0,
};

static uint32_t
rgb(uint16_t word)
{
	return (((word >> 8) & 7u) * 255u + 3u) / 7u << 16
	    | (((word >> 4) & 7u) * 255u + 3u) / 7u << 8
	    | ((word & 7u) * 255u + 3u) / 7u;
}

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
	if (status) {
		f->terminal->status_display_type = 2;
		f->terminal->status_display_cb(f->terminal, 1, 2, f->terminal->status_display_cbdata);
		CHECK(f->terminal->status_sub != NULL);
	}
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
point(struct fixture *f, uint32_t expected)
{
	struct st_pixel_buffer *pixels = NULL;
	const struct st_console_rect rect = {100,100,1,1};
	CHECK(st_console_get_pixels(&pixels, f->console, &rect, false) == 0);
	CHECK(pixels->primary[0] == expected && pixels->alternate[0] == expected);
	st_pixel_buffer_destroy(pixels);
}

static void
matrix(void)
{
	const int targets[] = {-1,0,1,2,9999}, palettes[] = {-1,0,1,2,3,4,9999};
	for (unsigned entry = 0; entry < 3; ++entry)
		for (unsigned status = 0; status < 2; ++status)
			for (size_t r = 0; r < sizeof(targets)/sizeof(*targets); ++r)
				for (size_t p = 0; p < sizeof(palettes)/sizeof(*palettes); ++p) {
					struct fixture f;
					create(&f, entry, status != 0);
					struct cterminal *t = f.terminal;
					struct cterm_igs *igs = t->igs_engine;
					feed(&f, "G#I 3:\033Y#%\033p\033e\033vA");
					CHECK(st_cterm_finish(t) == 0);
					igs->attributes.line_width = 5;
					igs->patterns[3][2] = 0xdead;
					igs->draw_x = 17; igs->draw_y = 23;
					igs->scaling_mode = 1; igs->scaling_resolution = 2;
					igs->memory_route = 5; igs->double_step = 4;
					igs->clipping = false; igs->clip_x1 = 42;
					igs->math[0] = (int16_t)targets[r]; igs->math[1] = (int16_t)palettes[p];
					uint32_t before[16], expected[16], actual[16];
					CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, before, 16) == 0);
					memcpy(expected, before, sizeof(expected));
					const uint32_t sentinel = ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x123456);
					CHECK(st_console_set_pixel(f.console, 100, 100, sentinel) == 0);
					unsigned target = entry;
					if (entry != 2 && targets[r] >= 0 && targets[r] <= 1)
						target = (unsigned)targets[r];
					feed(&f, "G#R 0],1]:");
					CHECK(st_cterm_input_paused(t) == (target != entry));
					if (target != entry) {
						CHECK(igs->resolution == entry && !igs->clipping);
						CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, actual, 16) == 0);
						CHECK(memcmp(before, actual, sizeof(actual)) == 0);
						local_ms = 1016;
						CHECK(st_cterm_poll(t) == 0 && st_cterm_input_paused(t));
						point(&f, sentinel);
						local_ms = 1017;
						CHECK(st_cterm_poll(t) == 0 && !st_cterm_input_paused(t));
						CHECK(t->xpos == 1 && t->ypos == 1 && !t->negative && t->attr == 15);
						CHECK(t->cursor == CTERM_CURSOR_HIDDEN);
						CHECK(((t->extattr & CTERM_EXTATTR_AUTOWRAP) != 0) == (target == 0));
					} else {
						CHECK(t->xpos == 7 && t->ypos == 4 && t->negative);
						CHECK(t->cursor != CTERM_CURSOR_HIDDEN && (t->extattr & CTERM_EXTATTR_AUTOWRAP) != 0);
					}
					CHECK(igs->resolution == target && t->width == (target == 0 ? 40 : 80));
					CHECK(t->height == (status ? 24 : 25) && t->backwidth == t->width);
					if (status) {
						CHECK(t->status_sub->width == t->width && t->status_sub->y == 25);
						CHECK(memcmp(t->screen.mode_palette, t->status_sub->screen.mode_palette,
						    sizeof(t->screen.mode_palette)) == 0);
					}
					CHECK(igs->clipping && igs->clip_x1 == 0 && igs->clip_y1 == 0);
					CHECK(igs->clip_x2 == (target == 0 ? 319 : 639) && igs->clip_y2 == (target == 2 ? 399 : 199));
					CHECK(igs->attributes.line_width == 5 && igs->patterns[3][2] == 0xdead);
					CHECK(igs->draw_x == 17 && igs->draw_y == 23 && igs->scaling_mode == 1 && igs->scaling_resolution == 2);
					CHECK(igs->memory_route == 5 && igs->double_step == 4 && igs->saved_resolution == entry);
					point(&f, target != entry ? CTERM_IGS_PALETTE_FIRST : sentinel);
					unsigned count = target == 0 ? 16 : target == 1 ? 4 : 2;
					for (unsigned i = 0; i < count; ++i) {
						if (palettes[p] == 1) {
							/* Saved medium desktop colours repeat if restored in
							 * low mode; the port never reads undefined saved pens. */
							expected[i] = target == 2 ? (i == 0 ? 0xffffff : 0)
							    : (entry == 1 || target == 1) ? (i % 4 == 3 ? 0 : desktop[i % 4]) : desktop[i];
						} else if (palettes[p] == 2 || palettes[p] == 3) {
							expected[i] = target == 2 ? (i == 0 ? 0xffffff : 0)
							    : target == 1 && i == 3 ? 0 : rgb(palettes[p] == 2 ? igs_words[i] : vdi_words[i]);
						}
					}
					CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, actual, 16) == 0);
					CHECK(memcmp(expected, actual, sizeof(actual)) == 0);
					destroy(&f);
				}
}

static void
replay(struct fixture *f, const char *wire, size_t size)
{
	size_t offset = 0;
	do {
		size_t used = 0;
		CHECK(st_cterm_write(f->terminal, wire + offset, size - offset, &used) == 0);
		offset += used;
		if (st_cterm_input_paused(f->terminal)) {
			local_ms += 17;
			CHECK(st_cterm_poll(f->terminal) == 0 && !st_cterm_input_paused(f->terminal));
		} else CHECK(used != 0 || offset == size);
	} while (offset < size);
}

static void
partitions(void)
{
	const char wire[] = "AG#R>1,2:R>0,3:I 1:B";
	for (unsigned entry = 0; entry < 3; ++entry)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f;
			create(&f, entry, false);
			if (split == sizeof(wire))
				for (size_t i = 0; i < sizeof(wire) - 1; ++i) replay(&f, wire + i, 1);
			else {
				replay(&f, wire, split);
				replay(&f, wire + split, sizeof(wire) - 1 - split);
			}
			CHECK(st_cterm_finish(f.terminal) == 0);
			CHECK(f.terminal->igs_engine->resolution == (entry == 2 ? 2u : 0u));
			struct st_console_cell cells[2];
			const struct st_console_rect rect = {0,0,2,1};
			CHECK(st_console_read_cells(f.console, &rect, cells, 2) == 0);
			CHECK(cells[entry == 2 ? 1 : 0].glyph == 'B');
			destroy(&f);
		}
}

static void
memory_and_failures(void)
{
	struct fixture f;
	create(&f, 0, false);
	struct cterm_igs *igs = f.terminal->igs_engine;
	/* Fill and wrap the two-row history ring before changing widths. */
	for (unsigned y = 0; y < 25; ++y)
		CHECK(st_console_set_pixel(f.console, 16, y * 8, ST_PIXEL_COLOR_RGB_TAG | UINT32_C(0x123456)) == 0);
	for (unsigned i = 0; i < 27; ++i) {
		char line[] = {(char)('a' + i), '\r', '\n', 0};
		feed(&f, line);
	}
	CHECK(f.terminal->backfilled == 2 && f.terminal->backpixels != NULL);
	uint8_t native[ST_IGS_IMAGE_BYTES] = {0}, output[ST_IGS_IMAGE_BYTES];
	const uint8_t first[] = {0x80,1,0x40,2,0x20,4,0x10,8};
	memcpy(native, first, sizeof(first));
	native[31999] = 0xa5;
	for (unsigned i = 0; i < 2; ++i) {
		CHECK(st_igs_image_create(&igs->memory[i], 4) == 0);
		CHECK(st_igs_image_import(igs->memory[i], 0, native, sizeof(native)) == 0);
	}
	for (unsigned target = 1; ; target = 0) {
		feed(&f, target == 1 ? "G#R 1,2:" : "G#R 0,3:");
		CHECK(igs->memory[0]->planes == (target == 1 ? 4u : 2u));
		local_ms += 17;
		CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(f.terminal->backfilled == 2 && f.terminal->backstart == 0);
		CHECK(f.terminal->scrollback[0].glyph == 'b'
		    && f.terminal->scrollback[f.terminal->backwidth].glyph == 'c');
		CHECK(f.terminal->backpixels[0]->width == (target == 1 ? 640u : 320u));
		for (unsigned i = 0; i < 2; ++i) {
			CHECK(igs->memory[i]->planes == (target == 1 ? 2u : 4u));
			CHECK(st_igs_image_export(igs->memory[i], 0, output, sizeof(output)) == 0);
			CHECK(memcmp(native, output, sizeof(native)) == 0);
		}
		CHECK(igs->memory[0]->pixels->primary[0] == 1 && igs->memory[0]->pixels->primary[1] == 2);
		if (target == 1) CHECK(igs->memory[0]->pixels->primary[18] == 1 && igs->memory[0]->pixels->primary[19] == 2);
		else { CHECK(igs->memory[0]->pixels->primary[2] == 4 && igs->memory[0]->pixels->primary[3] == 8); break; }
	}
	/* Invalid decoded image values must fail before console/palette commit,
	 * including when preparing the second image after the first succeeded. */
	igs->memory[1]->pixels->primary[0] = ST_PIXEL_COLOR_RGB_TAG;
	struct st_pixel_buffer *old = igs->memory[0]->pixels;
	uint32_t palette[16];
	memcpy(palette, igs->palette, sizeof(palette));
	igs->clipping = false; igs->clip_x1 = 17;
	feed(&f, "G#R 1,2:");
	local_ms += 17;
	CHECK(st_cterm_poll(f.terminal) == IO_ERROR_INVALID_FORMAT);
	CHECK(!st_cterm_input_paused(f.terminal) && igs->resolution == 0);
	CHECK(igs->memory[0]->pixels == old && igs->memory[0]->planes == 4 && igs->memory[1]->planes == 4);
	CHECK(!igs->clipping && igs->clip_x1 == 17 && memcmp(palette, igs->palette, sizeof(palette)) == 0);
	igs->memory[1]->pixels->primary[0] = 1;
	feed(&f, "G#R 1,");
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->resolution == 0 && !st_cterm_input_paused(f.terminal));
	feed(&f, "G#R 1,");
	local_ms += 4999;
	CHECK(st_cterm_poll(f.terminal) == 0 && st_graphics_parser_in_command(igs->parser));
	++local_ms;
	CHECK(st_cterm_poll(f.terminal) == 0 && !st_graphics_parser_in_command(igs->parser));
	CHECK(!igs->clipping && igs->resolution == 0 && memcmp(palette, igs->palette, sizeof(palette)) == 0);
	size_t used;
	CHECK(st_cterm_write(f.terminal, "G#R 1,99999:", 12, &used) == 0 && used == 12);
	CHECK(igs->resolution == 0 && !st_cterm_input_paused(f.terminal));
	feed(&f, "G#R 1,3:");
	CHECK(st_cterm_finish(f.terminal) == 0);
	local_ms += 100;
	CHECK(st_cterm_poll(f.terminal) == 0 && igs->resolution == 0);
	CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
	feed(&f, "G#R 1,3:");
	CHECK(st_cterm_reset_terminal(f.terminal) == 0);
	local_ms += 100;
	CHECK(st_cterm_poll(f.terminal) == 0 && igs->resolution == 0);
	feed(&f, "G#R 1,3:");
	CHECK(st_cterm_prepare_disconnect(f.terminal) == 0);
	local_ms += 100;
	CHECK(st_cterm_poll(f.terminal) == 0 && igs->resolution == 0);
	destroy(&f);
}

int
st_cterm_igs_resolution_test_main(void)
{
	matrix();
	partitions();
	memory_and_failures();
	return 0;
}
