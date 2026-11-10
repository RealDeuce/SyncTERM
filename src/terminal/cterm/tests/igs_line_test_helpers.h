/* Shared IGS line/Drawto fixtures and independent vector observations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_LINE_TEST_HELPERS_H
#define IGS_LINE_TEST_HELPERS_H

#include "cterm.h"
#include "cterm_igs.h"
#include "st_cterm.h"
#include "io_error.h"

#include <stdbool.h>
#include <inttypes.h>
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
create(struct fixture *f, unsigned resolution, bool status)
{
	memset(f, 0, sizeof(*f));
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {.columns = resolution == 0 ? 40 : 80,
	    .rows = 25, .cell_width = 8, .cell_height = resolution == 2 ? 16 : 8};
	CHECK(st_console_create(&f->console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&f->fonts, f->console) == 0);
	const struct st_cterm_config config = {.console = f->console, .fonts = f->fonts, .font_selector = 44,
	    .terminal_rows = status ? 24 : 25, .status_line = status,
	    .emulation = ST_CTERM_ATARI_ST_VT52, .graphics_mode = ST_CTERM_GRAPHICS_IGS_221,
	    .encoding = ST_CTERM_ENCODING_CODEPAGE, .codepage = ST_CODEPAGE_ATARI_ST, .quiet = true};
	CHECK(st_cterm_create(&f->terminal, &config) == 0);
	f->terminal->igs_engine->now_ms = now; local_ms = 1000;
}

static void destroy(struct fixture *f)
{
	st_cterm_destroy(&f->terminal); st_font_catalog_destroy(f->fonts); st_console_destroy(f->console);
}

static void feed(struct fixture *f, const char *wire)
{
	size_t used;
	int status = st_cterm_write(f->terminal, wire, strlen(wire), &used);
	if (status != 0 || used != strlen(wire))
		fprintf(stderr,"feed %s: status %d used %zu\n",wire,status,used);
	CHECK(status == 0 && used == strlen(wire));
}

static void
seed(struct fixture *f, const struct st_console_rect *rect, unsigned primary, unsigned alternate)
{
	struct st_pixel_buffer *b = NULL;
	CHECK(st_pixel_buffer_create(&b, rect->width, rect->height, true) == 0);
	CHECK(st_pixel_buffer_fill(b, primary, alternate) == 0);
	const struct st_pixel_blit blit = {.width = rect->width, .height = rect->height,
	    .destination_x = (int)rect->x, .destination_y = (int)rect->y, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(f->console, b, NULL, &blit) == 0);
	st_pixel_buffer_destroy(b);
}

struct line_case {
	int resolution, style, width, begin, end, mode, clipping, initial_last;
	int x1, y1, x2, y2, left, top, right, bottom;
	uint64_t hashes[2];
};
#include "data/igs_line_vectors.h"
_Static_assert(sizeof(line_cases)/sizeof(*line_cases) == 3321, "complete source-derived line matrix");

static uint64_t
native_hash(const struct st_pixel_buffer *b, unsigned planes, bool alternate)
{
	uint64_t hash = UINT64_C(14695981039346656037);
	const uint32_t *pixels = alternate ? b->alternate : b->primary;
	for (unsigned y = 0; y < b->height; ++y)
		for (unsigned group = 0; group < b->width/16; ++group) {
			uint16_t words[4] = {0};
			for (unsigned bit = 0; bit < 16; ++bit) {
				uint32_t colour = pixels[y*b->width+group*16+bit];
				CHECK(colour >= 768 && colour < 768+(1u << planes));
				for (unsigned p = 0; p < planes; ++p)
					if ((colour-768) & (1u << p)) words[p] |= (uint16_t)(0x8000u >> bit);
			}
			for (unsigned p = 0; p < planes; ++p) {
				hash = (hash ^ (words[p] >> 8))*UINT64_C(1099511628211);
				hash = (hash ^ (words[p] & 255))*UINT64_C(1099511628211);
			}
		}
	return hash;
}

#endif
