/* T conditional arity and marker/line state. Reference 15/29/52/128.
 * Literal expected widths/end styles and source masks; no runtime assets. */
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
attributes(void)
{
	const int styles[] = {-32768,-1,0,1,2,3,4,5,6,7,8,9999};
	const int marker_types[] = {1,1,1,1,2,3,4,5,6,3,1,1};
	const int line_types[] = {1,1,1,1,2,3,4,5,6,7,1,1};
	const int sizes[] = {-32768,-1,0,1,2,7,8,9,9999};
	const int heights[] = {11,11,11,11,22,77,88,11,11};
	/* Zero width denotes no width call; the old width is five. */
	const struct { int size, width, begin, end; } lines[] = {
		{-32768,0,2,2},{-1,0,2,2},{0,0,0,0},{1,1,2,2},{2,1,2,2},
		{3,3,2,2},{4,3,2,2},{15,15,2,2},{16,15,2,2},{17,17,2,2},
		{31,31,2,2},{32,31,2,2},{33,33,2,2},{39,39,2,2},{40,39,2,2},{41,39,2,2},
		{42,0,2,2},{49,0,2,2},{50,0,1,1},{51,0,1,0},{52,0,0,1},
		{53,0,1,2},{54,0,2,1},{55,0,2,2},{60,0,2,2},{61,0,2,0},
		{62,0,0,2},{63,0,2,1},{64,0,1,2},{65,0,2,2},{9999,0,2,2},
	};
	const uint16_t masks[32] = {0xaaaa,0x0101,0x0102,0x0103,0x0104,0x0105,0x0106,0x0107,
	    0x0108,0x0109,0x010a,0x010b,0x010c,0x010d,0x010e,0xaaaa,
	    0x5555,0x8001,0x8002,0x8003,0x8004,0x8005,0x8006,0x8007,
	    0x8008,0x8009,0x800a,0x800b,0x800c,0x800d,0x800e,0x5555};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f; create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->attributes.fill_interior = 4; igs->attributes.fill_pen = 0;
		igs->attributes.writing_mode = 4; igs->attributes.user_fill[0] = 0x4321;
		igs->draw_x = 17; igs->draw_y = 23; igs->clipping = false;
		igs->memory_route = 5; igs->double_step = 4;
		for (unsigned r = 0; r < 16; ++r) {
			igs->patterns[6][r] = r == 0 || r == 15 ? 0 : (uint16_t)(0x0100 + r);
			igs->patterns[7][r] = r == 0 || r == 15 ? 0 : (uint16_t)(0x8000 + r);
		}
		uint16_t banks[8][16]; memcpy(banks, igs->patterns, sizeof(banks));
		uint32_t palette[16]; memcpy(palette, igs->palette, sizeof(palette));
		const struct st_console_rect rect = {100,100,1,1};
		CHECK(st_console_set_pixel(f.console, 100, 100, CTERM_IGS_PALETTE_FIRST + 1) == 0);
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			feed(&f, scaling == 0 ? "G#g 0:" : scaling == 1 ? "G#g 1:" : "G#g 2:");
			struct cterm_igs_attributes wanted;
			memcpy(&wanted, &igs->attributes, sizeof(wanted));
			for (size_t s = 0; s < sizeof(styles)/sizeof(*styles); ++s)
				for (size_t n = 0; n < sizeof(sizes)/sizeof(*sizes); ++n) {
					igs->math[0] = (int16_t)styles[s]; igs->math[1] = (int16_t)sizes[n];
					feed(&f, "G#T 1,0],1]:");
					wanted.marker_type = marker_types[s]; wanted.marker_height = heights[n];
					CHECK(memcmp(&wanted, &igs->attributes, sizeof(wanted)) == 0);
				}
			for (size_t s = 0; s < sizeof(styles)/sizeof(*styles); ++s)
				for (size_t n = 0; n < sizeof(lines)/sizeof(*lines); ++n) {
					igs->attributes.line_type = 4; igs->attributes.line_width = 5;
					igs->attributes.line_begin = igs->attributes.line_end = 2;
					igs->attributes.user_line = 0x1357;
					memcpy(&wanted, &igs->attributes, sizeof(wanted));
					igs->math[0] = (int16_t)styles[s]; igs->math[1] = (int16_t)lines[n].size;
					feed(&f, "G#T 2,0],1]:");
					wanted.line_type = line_types[s] == 7 && lines[n].width == 0 ? 4 : line_types[s];
					wanted.line_width = lines[n].width == 0 ? 5 : line_types[s] == 1 ? lines[n].width : 1;
					wanted.line_begin = lines[n].begin; wanted.line_end = lines[n].end;
					if (line_types[s] == 7 && lines[n].size >= 1 && lines[n].size <= 32)
						wanted.user_line = masks[lines[n].size - 1];
					CHECK(memcmp(&wanted, &igs->attributes, sizeof(wanted)) == 0);
				}
			for (unsigned r = 0; r < 32; ++r) {
				char wire[32]; (void)snprintf(wire, sizeof(wire), "G#T 2,7,%u:", r + 1); feed(&f, wire);
				CHECK(igs->attributes.user_line == masks[r] && igs->attributes.line_type == 7 && igs->attributes.line_width == 1);
			}
			CHECK(memcmp(banks, igs->patterns, sizeof(banks)) == 0);
			CHECK(memcmp(palette, igs->palette, sizeof(palette)) == 0);
			CHECK(igs->random_seed == 1 && igs->pattern_seed == 0);
			CHECK(igs->draw_x == 17 && igs->draw_y == 23 && !igs->clipping);
			CHECK(igs->memory_route == 5 && igs->double_step == 4);
			CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution);
		}
		struct st_pixel_buffer *pixels = NULL;
		CHECK(st_console_get_pixels(&pixels, f.console, &rect, false) == 0);
		CHECK(pixels->primary[0] == CTERM_IGS_PALETTE_FIRST + 1 && pixels->alternate[0] == pixels->primary[0]);
		st_pixel_buffer_destroy(pixels);
		feed(&f, "G#T>1,6,8:T>2,7,1:T>2,7,53:I>1:I 5:");
		local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
		struct cterm_igs_attributes selected; memcpy(&selected, &igs->attributes, sizeof(selected));
		feed(&f, resolution == 0 ? "G#R 1,0:" : "G#R 0,0:");
		local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(memcmp(&selected, &igs->attributes, sizeof(selected)) == 0);
		feed(&f, "G#I 2:");
		CHECK(igs->attributes.marker_type == 1 && igs->attributes.marker_height == 11);
		CHECK(igs->attributes.line_type == 1 && igs->attributes.line_width == 1);
		CHECK(igs->attributes.line_begin == 0 && igs->attributes.line_end == 0 && igs->attributes.user_line == 0xaaaa);
		feed(&f, "G#T>2,7,33:I 0:"); CHECK(igs->attributes.line_type == 1 && igs->attributes.user_line == 0xaaaa);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->attributes.user_line == 0xffff);
		destroy(&f);
	}
}

static void
partitions_and_arity(void)
{
	const char *wires[] = {"BG#T>1,7,8:T>0:M 4:C", "BG#T>r,1,4:T>0:M 4:C",
	    "BG#T>r:M 4:C", "BG#T 3,7,8:C"};
	for (unsigned resolution = 0; resolution < 3; ++resolution)
		for (unsigned w = 0; w < 4; ++w)
			for (size_t split = 0; split <= strlen(wires[w]) + 1; ++split) {
				struct fixture f; create(&f, resolution); size_t used;
				struct cterm_igs *igs = f.terminal->igs_engine;
				/* This seed advances to 0x200, returning kind 2 immediately. */
				igs->random_seed = w == 1 ? UINT32_C(0x9334f05b) : 1;
				igs->random_range[0][0] = 0; igs->random_range[0][1] = 255;
				const char *wire = wires[w]; size_t size = strlen(wire);
				if (split == size + 1) {
					for (size_t i = 0; i < size; ++i)
						CHECK(st_cterm_write(f.terminal, wire + i, 1, &used) == 0 && used == 1);
				} else {
					CHECK(st_cterm_write(f.terminal, wire, split, &used) == 0 && used == split);
					CHECK(st_cterm_write(f.terminal, wire + split, size - split, &used) == 0 && used == size - split);
				}
				CHECK(st_cterm_finish(f.terminal) == 0);
				const char *text = w == 3 ? "B7,8:C" : "BC";
				struct st_console_cell cells[6]; const struct st_console_rect rect = {0,0,(uint32_t)strlen(text),1};
				CHECK(st_console_read_cells(f.console, &rect, cells, strlen(text)) == 0);
				for (size_t i = 0; i < strlen(text); ++i) CHECK(cells[i].glyph == (unsigned char)text[i]);
				CHECK(igs->attributes.marker_type == (w == 0 ? 3 : 1));
				CHECK(igs->attributes.marker_height == (w == 0 ? 88 : 11));
				CHECK(igs->attributes.line_width == (w == 1 ? 3 : 1));
				CHECK(igs->random_seed == (w == 1 ? UINT32_C(0x200) : w == 2 ? UINT32_C(0xbb40e62e) : 1));
				CHECK(igs->attributes.writing_mode == (w == 3 ? 1 : 4));
				const char *partial[] = {"G#T 1", "G#T 1,7", "G#T 1,7,1"};
				struct cterm_igs_attributes saved; memcpy(&saved, &igs->attributes, sizeof(saved));
				for (unsigned p = 0; p < 3; ++p) {
					feed(&f, partial[p]); CHECK(st_cterm_finish(f.terminal) == 0);
					feed(&f, partial[p]); local_ms += 5000; CHECK(st_cterm_poll(f.terminal) == 0);
					CHECK(memcmp(&saved, &igs->attributes, sizeof(saved)) == 0);
				}
				const char invalid[] = "G#T 1,7,99999:";
				CHECK(st_cterm_write(f.terminal, invalid, sizeof(invalid) - 1, &used) == IO_ERROR_OUT_OF_RANGE);
				CHECK(memcmp(&saved, &igs->attributes, sizeof(saved)) == 0);
				feed(&f, "G#T 1,2,3:"); CHECK(igs->attributes.marker_type == 2 && igs->attributes.marker_height == 33);
				feed(&f, "G#T 2,"); CHECK(st_cterm_reset_terminal(f.terminal) == 0);
				destroy(&f);
			}
	/* Signed invalid kinds consume no extra fields either. */
	struct fixture f; create(&f, 0);
	const int kinds[] = {-32768,-1,0,3,9999};
	for (unsigned i = 0; i < 5; ++i) {
		f.terminal->igs_engine->math[0] = (int16_t)kinds[i];
		feed(&f, "G#T 0]:");
		CHECK(f.terminal->igs_engine->attributes.line_type == 1);
		CHECK(st_cterm_finish(f.terminal) == 0);
	}
	struct cterm_igs *igs = f.terminal->igs_engine;
	igs->random_seed = UINT32_C(0x9334f05b);
	igs->random_range[0][0] = 0; igs->random_range[0][1] = 255;
	feed(&f, "G#T r,1");
	CHECK(igs->random_seed == UINT32_C(0x9334f05b) && igs->attributes.line_width == 1);
	CHECK(st_cterm_finish(f.terminal) == 0 && igs->random_seed == UINT32_C(0x9334f05b));
	feed(&f, "G#T r,1,4:");
	CHECK(igs->random_seed == UINT32_C(0x200) && igs->attributes.line_width == 3);
	destroy(&f);
}

int
st_cterm_igs_type_test_main(void)
{
	attributes(); partitions_and_arity(); return 0;
}
