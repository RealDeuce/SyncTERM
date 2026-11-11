/* M is an unscaled workstation attribute, with no immediate raster effect.
 * Reference 15 and 129; independent literal state/lifecycle expectations. */
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
selection_and_preservation(void)
{
	const int requests[] = {1,2,3,4,0,5,9999,-1,-32768,32767};
	const int expected[] = {1,2,3,4,1,1,1,1,1,1};
	for (unsigned resolution = 0; resolution < 3; ++resolution) {
		struct fixture f;
		create(&f, resolution);
		struct cterm_igs *igs = f.terminal->igs_engine;
		igs->attributes.marker_type = 4; igs->attributes.marker_pen = 0;
		igs->attributes.line_width = 5; igs->attributes.line_begin = 2;
		igs->attributes.fill_interior = 4; igs->attributes.fill_perimeter = false;
		igs->attributes.user_line = 0xa5a5; igs->attributes.user_fill[3] = 0x1234;
		igs->attributes.text_effects = 15;
		igs->draw_x = 17; igs->draw_y = 23;
		igs->clipping = false; igs->clip_x1 = 42;
		igs->memory_route = 5; igs->double_step = 4;
		const struct st_console_rect rect = {100,100,1,1};
		const uint32_t sentinel = CTERM_IGS_PALETTE_FIRST + (resolution == 0 ? 9 : 1);
		CHECK(st_console_set_pixel(f.console, 100, 100, sentinel) == 0);
		uint32_t palette[16]; memcpy(palette, igs->palette, sizeof(palette));
		struct cterm_igs_attributes wanted = igs->attributes;
		for (unsigned scaling = 0; scaling < 3; ++scaling) {
			feed(&f, scaling == 0 ? "G#g 0:" : scaling == 1 ? "G#g 1:" : "G#g 2:");
			for (size_t i = 0; i < sizeof(requests) / sizeof(*requests); ++i) {
				/* Indirection supplies signed values without a fake wire syntax. */
				igs->math[0] = (int16_t)requests[i];
				feed(&f, "G#M 0]:");
				wanted.writing_mode = expected[i];
				CHECK(memcmp(&igs->attributes, &wanted, sizeof(wanted)) == 0);
				CHECK(memcmp(igs->palette, palette, sizeof(palette)) == 0);
				CHECK(igs->resolution == resolution && igs->saved_resolution == resolution);
				CHECK(igs->draw_x == 17 && igs->draw_y == 23);
				CHECK(!igs->clipping && igs->clip_x1 == 42);
				CHECK(igs->memory_route == 5 && igs->double_step == 4);
				CHECK(igs->scaling_mode == (int)scaling && igs->scaling_resolution == (int)resolution);
				struct st_pixel_buffer *pixels = NULL;
				CHECK(st_console_get_pixels(&pixels, f.console, &rect, false) == 0);
				CHECK(pixels->primary[0] == sentinel && pixels->alternate[0] == sentinel);
				st_pixel_buffer_destroy(pixels);
			}
			/* Literal and random operands also stay unscaled. */
			feed(&f, "G#M 4:"); CHECK(igs->attributes.writing_mode == 4);
			igs->random_range[0][0] = igs->random_range[0][1] = 2;
			igs->random_range[1][0] = igs->random_range[1][1] = 3;
			feed(&f, "G#M r:"); CHECK(igs->attributes.writing_mode == 2);
			feed(&f, "G#M R:"); CHECK(igs->attributes.writing_mode == 3);
		}
		feed(&f, "G#I 1:G#I 3:G#I 4:"); CHECK(igs->attributes.writing_mode == 3);
		feed(&f, "G#I 2:"); CHECK(igs->attributes.writing_mode == 1);
		feed(&f, "G#M 4:G#I 0:"); CHECK(igs->attributes.writing_mode == 1);
		feed(&f, "G#M 3:G#I 5:");
		local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
		CHECK(igs->attributes.writing_mode == 3);
		/* R pauses before a trailing M, without changing the old mode. */
		const char *wire = resolution == 0 ? "G#R 1,0:G#M 4:" : "G#R 0,0:G#M 4:";
		size_t used;
		CHECK(st_cterm_write(f.terminal, wire, strlen(wire), &used) == 0);
		if (resolution != 2) {
			CHECK(used == 8 && igs->attributes.writing_mode == 3);
			local_ms += 17; CHECK(st_cterm_poll(f.terminal) == 0);
			CHECK(igs->attributes.writing_mode == 3);
			feed(&f, wire + used);
		}
		CHECK(igs->attributes.writing_mode == 4);
		CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->attributes.writing_mode == 1);
		destroy(&f);
	}
}

static void
partitions_and_recovery(void)
{
	const char wire[] = "AG#M>2:M>4:g>2:M 3:B";
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
			CHECK(st_cterm_finish(f.terminal) == 0 && igs->attributes.writing_mode == 3);
			struct st_console_cell cells[2];
			const struct st_console_rect rect = {0,0,2,1};
			CHECK(st_console_read_cells(f.console, &rect, cells, 2) == 0 && cells[0].glyph == 'A' && cells[1].glyph == 'B');
			feed(&f, "G#M 4"); CHECK(igs->attributes.writing_mode == 3);
			CHECK(st_cterm_finish(f.terminal) == 0 && igs->attributes.writing_mode == 3);
			feed(&f, "G#M 4"); local_ms += 5000;
			CHECK(st_cterm_poll(f.terminal) == 0 && igs->attributes.writing_mode == 3);
			CHECK(st_cterm_write(f.terminal, "G#M 99999:", 10, &used) == 0);
			CHECK(igs->attributes.writing_mode == 3);
			feed(&f, "G#M 4:"); CHECK(igs->attributes.writing_mode == 4);
			feed(&f, "G#M ");
			CHECK(st_cterm_reset_terminal(f.terminal) == 0 && igs->attributes.writing_mode == 1);
			feed(&f, "G#M 2:"); CHECK(igs->attributes.writing_mode == 2);
			destroy(&f);
		}
}

int
st_cterm_igs_writing_mode_test_main(void)
{
	selection_and_preservation();
	partitions_and_recovery();
	return 0;
}
