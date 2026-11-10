/* S component mapping, physical palettes and unchanged indexed pixels.
 * Literal expectations from reference 15/28; no external runtime assets. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_igs.h"
#include "st_cterm.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
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
create(struct fixture *f, unsigned resolution)
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
		.quiet = true,
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
components_and_pens(void)
{
	const int input[][3] = {{0,7,3},{1,6,3},{2,5,3},{3,4,3},{4,3,3},{5,2,3},{6,1,3},{7,0,3},
	    {8,2,7},{-1,7,4},{32767,9999,-32768}};
	const int requested[][3] = {{0,1000,428},{142,857,428},{285,714,428},{428,571,428},
	    {571,428,428},{714,285,428},{857,142,428},{1000,0,428},{0,285,1000},{0,1000,571},{0,0,0}};
	const uint16_t raw[] = {0x073,0x163,0x253,0x343,0x433,0x523,0x613,0x703,0x027,0x074,0};
	const uint32_t rgb[] = {0x00ff6d,0x24db6d,0x49b66d,0x6d926d,0x926d6d,0xb6496d,0xdb246d,
	    0xff006d,0x0049ff,0x00ff92,0};
	const int pens[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,9999,-1,-256,-32768};
	const int physical[3][21] = {
		{0,15,1,2,4,6,3,5,7,8,9,10,12,14,11,13,0,0,-1,-1,-1},
		{0,3,1,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,0,-1,-1,-1},
		{0,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,0,-1,-1,-1},
	};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		feed(&f, "G#M>4:C 0,0:");
		struct cterm_igs_attributes attributes;
		memcpy(&attributes, &igs->attributes, sizeof(attributes));
		uint32_t saved[16]; memcpy(saved, igs->saved_palette, sizeof(saved));
		igs->draw_x = 17; igs->draw_y = 23; igs->clipping = false;
		igs->memory_route = 5; igs->double_step = 4;
		const struct st_console_rect rect = {100,100,1,1};
		CHECK(st_console_set_pixel(f.console, 100, 100, CTERM_IGS_PALETTE_FIRST + 1) == 0);
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			feed(&f, scaling == 0 ? "G#g 0:" : scaling == 1 ? "G#g 1:" : "G#g 2:");
			for (size_t i = 0; i < sizeof(input)/sizeof(*input); ++i) {
				feed(&f, "G#S 0,0,0,0:");
				for (unsigned c = 0; c < 3; ++c) igs->math[c] = (int16_t)input[i][c];
				feed(&f, "G#S 0,0],1],2]:");
				CHECK(memcmp(igs->requested_palette[0], requested[i], sizeof(requested[i])) == 0);
				CHECK(igs->raw_palette[0] == (resolution == 2 ? 0 : raw[i]));
				CHECK(igs->palette[0] == (resolution == 2 ? 0 : rgb[i]));
			}
			for (size_t i = 0; i < sizeof(pens)/sizeof(*pens); ++i) {
				uint32_t expected[16]; uint16_t expected_raw[16]; int expected_request[16][3];
				memcpy(expected, igs->palette, sizeof(expected));
				memcpy(expected_raw, igs->raw_palette, sizeof(expected_raw));
				memcpy(expected_request, igs->requested_palette, sizeof(expected_request));
				igs->math[0] = (int16_t)pens[i];
				feed(&f, "G#S 0],1,2,3:");
				if (physical[resolution][i] >= 0) {
					unsigned pen = pens[i] > 15 ? 0 : (unsigned)pens[i];
					expected_request[pen][0] = 142; expected_request[pen][1] = 285; expected_request[pen][2] = 428;
					if (resolution != 2) {
						expected[physical[resolution][i]] = 0x24496d;
						expected_raw[physical[resolution][i]] = 0x123;
					}
				}
				CHECK(memcmp(expected_request, igs->requested_palette, sizeof(expected_request)) == 0);
				CHECK(memcmp(expected_raw, igs->raw_palette, sizeof(expected_raw)) == 0);
				CHECK(memcmp(expected, igs->palette, sizeof(expected)) == 0);
				uint32_t published[16];
				CHECK(st_console_get_palette(f.console, CTERM_IGS_PALETTE_FIRST, published, 16) == 0);
				CHECK(memcmp(expected, published, sizeof(expected)) == 0);
			}
			igs->random_range[0][0] = igs->random_range[0][1] = 1;
			igs->random_range[1][0] = igs->random_range[1][1] = 7;
			feed(&f, "G#S r,R,R,R:");
			CHECK(igs->requested_palette[1][0] == 1000 && igs->requested_palette[1][1] == 1000
			    && igs->requested_palette[1][2] == 1000);
			CHECK(memcmp(&attributes, &igs->attributes, sizeof(attributes)) == 0);
			CHECK(memcmp(saved, igs->saved_palette, sizeof(saved)) == 0);
			CHECK(igs->draw_x == 17 && igs->draw_y == 23 && !igs->clipping);
			CHECK(igs->memory_route == 5 && igs->double_step == 4 && igs->marker_pen_cache == 0);
			CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution);
		}
		struct st_pixel_buffer *pixels = NULL;
		CHECK(st_console_get_pixels(&pixels, f.console, &rect, false) == 0);
		CHECK(pixels->primary[0] == CTERM_IGS_PALETTE_FIRST + 1 && pixels->alternate[0] == pixels->primary[0]);
		st_pixel_buffer_destroy(pixels);
		feed(&f, "G#I 2:");
		CHECK(igs->requested_palette[1][0] == 1000);
		feed(&f, "G#I 1:");
		CHECK(memcmp(saved, igs->palette, sizeof(saved)) == 0);
		destroy(&f);
	}
}

static void
monochrome(void)
{
	struct fixture f;
	create(&f, 2);
	struct cterm_igs *igs = f.terminal->igs_engine;
	const char *wire[] = {"G#S 0,0,0,0:","G#S 0,7,7,7:","G#S 1,7,7,7:","G#S 1,0,0,0:",
	    "G#S 1,3,3,3:","G#S 0,0,7,7:"};
	const uint16_t raw[] = {0,0xffff,0xfffe,1,1,1};
	const uint32_t first[] = {0,0xffffff,0,0xffffff,0xffffff,0xffffff};
	for (size_t i = 0; i < sizeof(wire)/sizeof(*wire); ++i) {
		feed(&f, wire[i]);
		CHECK(igs->raw_palette[0] == raw[i] && igs->palette[0] == first[i]);
		CHECK(igs->palette[1] == (first[i] ^ UINT32_C(0xffffff)));
	}
	CHECK(igs->requested_palette[1][0] == 428 && igs->requested_palette[0][0] == 0);
	CHECK(igs->requested_palette[0][1] == 1000 && igs->requested_palette[0][2] == 1000);
	destroy(&f);
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "AG#S>0,7,7,7:S 1,0,0,0:B";
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (size_t split = 0; split <= sizeof(wire); ++split) {
			struct fixture f;
			create(&f, resolution);
			size_t used;
			if (split == sizeof(wire)) {
				for (size_t i = 0; i < sizeof(wire) - 1; ++i)
					CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
			} else {
				CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
				CHECK(st_cterm_write(f.terminal, wire + split, sizeof(wire) - 1 - split, &used) == 0
				    && used == sizeof(wire) - 1 - split);
			}
			struct cterm_igs *igs = f.terminal->igs_engine;
			CHECK(st_cterm_finish(f.terminal) == 0 && igs->palette[0] == 0xffffff);
			CHECK(igs->palette[resolution == 0 ? 15 : resolution == 1 ? 3 : 1] == 0);
			struct st_console_cell cells[2]; const struct st_console_rect rect = {0,0,2,1};
			CHECK(st_console_read_cells(f.console, &rect, cells, 2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			uint32_t complete[16]; int requested[16][3];
			memcpy(complete, igs->palette, sizeof(complete));
			memcpy(requested, igs->requested_palette, sizeof(requested));
			const char *partial[] = {"G#S 0", "G#S 0,7", "G#S 0,7,7", "G#S 0,7,7,0"};
			for (size_t i = 0; i < sizeof(partial)/sizeof(*partial); ++i) {
				feed(&f, partial[i]); CHECK(st_cterm_finish(f.terminal) == 0);
				CHECK(memcmp(complete, igs->palette, sizeof(complete)) == 0);
				feed(&f, partial[i]); local_ms += 5000; CHECK(st_cterm_poll(f.terminal) == 0);
				CHECK(memcmp(complete, igs->palette, sizeof(complete)) == 0);
				CHECK(memcmp(requested, igs->requested_palette, sizeof(requested)) == 0);
			}
			const char invalid[] = "G#S 0,7,7,99999:";
			CHECK(st_cterm_write(f.terminal, invalid, sizeof(invalid) - 1, &used) == IO_ERROR_OUT_OF_RANGE);
			CHECK(memcmp(complete, igs->palette, sizeof(complete)) == 0);
			feed(&f, "G#S 0,0,0,0:"); CHECK(igs->palette[0] == 0);
			feed(&f, "G#S 0,7,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
			CHECK(igs->palette[0] == 0xffffff);
			destroy(&f);
		}
}

int
st_cterm_igs_palette_test_main(void)
{
	components_and_pens();
	monochrome();
	partitions_and_recovery();
	return 0;
}
