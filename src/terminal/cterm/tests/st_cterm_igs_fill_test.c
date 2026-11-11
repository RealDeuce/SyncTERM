/* A fill selection and bounded user banks. Reference 15/29/123/124/130.
 * Random rows are literal results derived from the reference recurrences. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "st_cterm.h"
#include "st_graphics_parser.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static const uint16_t default_rows[16] = {0,0,0x00c0,0x01e0,0x03b0,0x07b8,0x0fbc,0x0e0c,
    0x1f5e,0x1ffe,0x1f8e,0x1e06,0x1c06,0x1802,0x1000,0};
static const uint16_t first_rows[16] = {0xfa4f,0x6e42,0xcb83,0xd43e,0x6197,0x01da,0xf18b,0xee16,
    0xa85f,0xb7f2,0xeb13,0x666e,0x96a7,0xa88a,0x401b,0x9546};
static const uint16_t third_rows[16] = {0xb2dd,0xcd7c,0x0d61,0xf4e8,0xac45,0xe674,0xb889,0xd520,
    0x332d,0xbbec,0x3531,0x1dd8,0x2f95,0xc5e4,0x2b59,0x8710};

struct fixture { st_console_t *console; st_font_catalog_t *fonts; st_cterm_t *terminal; };
static uint64_t local_ms;
static int now(uint64_t *value) { *value = local_ms; return 0; }

static void
create(struct fixture *f, unsigned resolution)
{
	memset(f, 0, sizeof(*f));
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = resolution == 0 ? 40 : 80,
	    .rows = 25, .cell_width = 8, .cell_height = resolution == 2 ? 16 : 8};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {.console = f->console, .fonts = f->fonts, .font_selector = 44,
	    .emulation = ST_CTERM_ATARI_ST_VT52, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
	    .encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_ATARI_ST, .quiet = true};
	CHECK(st_cterm_create(&f->terminal, &config) == 0);
	f->terminal->igs_engine->now_ms = now; local_ms = 1000;
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
selection(void)
{
	const int interiors[] = {-32768,-1,0,1,2,3,5,9999};
	const int normalized[] = {0,0,0,1,2,3,0,0};
	const int indices[] = {-32768,-1,0,1,7,12,13,24,25,9999};
	const int styles[2][10] = {{1,1,1,1,7,12,1,1,1,1},{1,1,1,1,7,12,13,24,1,1}};
	const int borders[] = {0,1,-1,-32768,9999};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->attributes.writing_mode = 4; igs->attributes.fill_pen = 0;
		igs->attributes.user_line = 0xa5a5; igs->draw_x = 17; igs->draw_y = 23;
		igs->clipping = false; igs->memory_route = 5; igs->double_step = 4;
		uint32_t palette[16]; memcpy(palette, igs->palette, sizeof(palette));
		const struct st_console_rect rect = {100,100,1,1};
		CHECK(st_console_set_pixel(f.console, 100, 100, CTERM_IGS_PALETTE_FIRST + 1) == 0);
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			feed(&f, scaling == 0 ? "G#g 0:" : scaling == 1 ? "G#g 1:" : "G#g 2:");
			struct cterm_igs_attributes wanted;
			memcpy(&wanted, &igs->attributes, sizeof(wanted));
			for (size_t i = 0; i < sizeof(interiors)/sizeof(*interiors); ++i)
				for (size_t s = 0; s < sizeof(indices)/sizeof(*indices); ++s)
					for (size_t b = 0; b < sizeof(borders)/sizeof(*borders); ++b) {
						igs->math[0] = (int16_t)interiors[i]; igs->math[1] = (int16_t)indices[s];
						igs->math[2] = (int16_t)borders[b];
						feed(&f, "G#A 0],1],2]:");
						wanted.fill_interior = normalized[i];
						wanted.fill_index = styles[normalized[i] == 2][s];
						wanted.fill_perimeter = borders[b] != 0;
						CHECK(memcmp(&wanted, &igs->attributes, sizeof(wanted)) == 0);
					}
			feed(&f, "G#A 2,24,1:");
			/* Every source bank has known independent contents. A makes a copy. */
			for (unsigned bank = 0; bank < 8; ++bank)
				for (unsigned row = 0; row < 16; ++row)
					igs->patterns[bank][row] = (uint16_t)((bank + 1) * 256 + row);
			for (unsigned bank = 0; bank < 8; ++bank) {
				char wire[32]; (void)snprintf(wire, sizeof(wire), "G#A 4,%u,0:", bank); feed(&f, wire);
				CHECK(igs->attributes.fill_interior == 4 && igs->attributes.fill_index == 24);
				CHECK(!igs->attributes.fill_perimeter);
				for (unsigned row = 0; row < 16; ++row)
					CHECK(igs->attributes.user_fill[row] == (bank + 1) * 256 + row);
				igs->patterns[bank][0] ^= 0xffff;
				CHECK(igs->attributes.user_fill[0] == (bank + 1) * 256);
				igs->patterns[bank][0] ^= 0xffff;
			}
			igs->math[0] = -1; feed(&f, "G#A 4,0],1:");
			CHECK(igs->attributes.user_fill[0] == 256 && igs->attributes.fill_index == 24);
			CHECK(igs->random_seed == 1 && igs->pattern_seed == 0);
			CHECK(igs->attributes.writing_mode == 4 && igs->attributes.fill_pen == 0);
			CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
			CHECK(igs->draw_x == 17 && igs->draw_y == 23 && !igs->clipping);
			CHECK(igs->memory_route == 5 && igs->double_step == 4);
			CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution);
		}
		struct st_pixel_buffer *pixels = NULL;
		CHECK(st_console_get_pixels(&pixels, f.console, &rect, false) == 0);
		CHECK(pixels->primary[0] == CTERM_IGS_PALETTE_FIRST + 1 && pixels->alternate[0] == pixels->primary[0]);
		st_pixel_buffer_destroy(pixels); destroy(&f);
	}
}

static void
random_and_lifetime(void)
{
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f, "G#A>2,24,1:A 4,8,0:");
		CHECK(igs->random_seed == UINT32_C(0xbb40e62e) && igs->pattern_seed == UINT32_C(0x762e9546));
		CHECK(memcmp(igs->patterns[0], first_rows, sizeof(first_rows)) == 0);
		CHECK(memcmp(igs->attributes.user_fill, first_rows, sizeof(first_rows)) == 0);
		CHECK(igs->attributes.fill_index == 24);
		feed(&f, "G#A 4,9,1:");
		CHECK(igs->random_seed == UINT32_C(0x5eb5ca17) && igs->pattern_seed == UINT32_C(0x5eb5ca));
		CHECK(memcmp(igs->attributes.user_fill, default_rows, sizeof(default_rows)) == 0);
		feed(&f, "G#A 4,8,0:");
		CHECK(igs->random_seed == UINT32_C(0x4c45300c) && igs->pattern_seed == UINT32_C(0x5f9b8710));
		CHECK(memcmp(igs->attributes.user_fill, third_rows, sizeof(third_rows)) == 0);
		igs->random_range[0][0] = 0; igs->random_range[0][1] = 255;
		const struct st_graphics_argument r = {.kind = ST_GRAPHICS_NUMBER, .operand = {.source = ST_GRAPHICS_RANDOM_R}};
		int result = -1;
		CHECK(cterm_igs_argument_value(igs, &r, &result) == 0 && result == 58);
		CHECK(igs->random_seed == UINT32_C(0x58543a1d));
		feed(&f, "G#I 2:");
		CHECK(memcmp(igs->patterns[0], default_rows, sizeof(default_rows)) == 0);
		CHECK(memcmp(igs->attributes.user_fill, third_rows, sizeof(third_rows)) == 0);
		CHECK(igs->pattern_seed == UINT32_C(0x5f9b8710));
		feed(&f, "G#A 4,0,0:");
		CHECK(memcmp(igs->attributes.user_fill, default_rows, sizeof(default_rows)) == 0);
		feed(&f, "G#A 4,9999,1:");
		CHECK(igs->random_seed == UINT32_C(0xe944451a) && igs->pattern_seed == UINT32_C(0xe94445));
		uint16_t banks[8][16]; memcpy(banks, igs->patterns, sizeof(banks));
		feed(&f, "G#I>1:I 5:"); local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(igs->attributes.fill_interior == 4 && igs->attributes.fill_index == 1 && igs->attributes.fill_perimeter);
		CHECK(memcmp(banks, igs->patterns, sizeof(banks)) == 0);
		CHECK(memcmp(default_rows, igs->attributes.user_fill, sizeof(default_rows)) == 0);
		CHECK(igs->random_seed == UINT32_C(0xe944451a) && igs->pattern_seed == UINT32_C(0xe94445));
		CHECK(st_cterm_reset_terminal(f.terminal) == 0);
		CHECK(igs->random_seed == 1 && igs->pattern_seed == 0);
		for (unsigned i = 0; i < 16; ++i) CHECK(igs->attributes.user_fill[i] == 0xffff);
		CHECK(memcmp(default_rows, igs->patterns[0], sizeof(default_rows)) == 0);
		for (unsigned b = 1; b < 8; ++b)
			for (unsigned i = 0; i < 16; ++i) CHECK(igs->patterns[b][i] == 0);
		/* The shared port stream is deterministic and does not emulate a clock. */
		const uint32_t seeds[] = {0xbb40e62e,0x5eb5ca17,0x4c45300c,0x58543a1d,0xe944451a,0x9b958193,0x7171d8d8,0x37d52df9};
		const int bytes[] = {230,202,48,58,69,129,216,45};
		igs->random_range[0][0] = 0; igs->random_range[0][1] = 255;
		for (unsigned i = 0; i < 8; ++i) {
			CHECK(cterm_igs_argument_value(igs, &r, &result) == 0 && result == bytes[i]);
			CHECK(igs->random_seed == seeds[i]);
		}
		igs->random_seed = 0;
		CHECK(cterm_igs_argument_value(igs, &r, &result) == 0 && result == 0 && igs->random_seed == 1);
		destroy(&f);
	}
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "BG#A>2,24,1:A 4,8,0:C";
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f; create(&f, resolution); size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire) - 1; ++i)
					CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal, wire + split, sizeof(wire) - 1 - split, &used) == 0 && used == sizeof(wire) - 1 - split);
			}
			struct cterm_igs *igs = f.terminal->igs_engine;
			CHECK(st_cterm_finish(f.terminal) == 0 && igs->attributes.fill_interior == 4 && igs->attributes.fill_index == 24);
			CHECK(memcmp(first_rows, igs->attributes.user_fill, sizeof(first_rows)) == 0);
			struct st_console_cell cells[2]; const struct st_console_rect rect = {0,0,2,1};
			CHECK(st_console_read_cells(f.console, &rect, cells, 2) == 0 && cells[0].glyph == 'B' && cells[1].glyph == 'C');
			const char *partial[] = {"G#A 4", "G#A 4,9", "G#A 4,9,1"};
			for (unsigned i = 0; i < 3; ++i) {
				feed(&f, partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
				feed(&f, partial[i]); local_ms += 5000; CHECK(st_cterm_poll(f.terminal) == 0);
				CHECK(igs->random_seed == UINT32_C(0xbb40e62e) && igs->pattern_seed == UINT32_C(0x762e9546));
				CHECK(memcmp(first_rows, igs->attributes.user_fill, sizeof(first_rows)) == 0);
			}
			const char invalid[] = "G#A 4,99999,1:";
			CHECK(st_cterm_write(f.terminal, invalid, sizeof(invalid) - 1, &used) == 0);
			CHECK(igs->random_seed == UINT32_C(0xbb40e62e));
			feed(&f, "G#A 4,9,1:"); CHECK(igs->pattern_seed == UINT32_C(0x5eb5ca));
			feed(&f, "G#A 4,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->pattern_seed == 0);
			destroy(&f);
		}
}

int
st_cterm_igs_fill_test_main(void)
{
	selection(); random_and_lifetime(); partitions_and_recovery(); return 0;
}
