/* Shared source-derived indexed and cell fixtures for VT52 scrolling. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef IGS_CONSOLE_SCROLL_TEST_H
#define IGS_CONSOLE_SCROLL_TEST_H
#include "igs_console_test_model.h"

static void
no_reply(const char *bytes, size_t size, void *context)
{
	(void)bytes;
	(void)context;
	CHECK(size == 0);
}

static void
prepare_scroll(struct fixture *f)
{
	feed(f, "\033q\033w\033fG#c>1,15:c 0,0:\033E"
	    "G#c>1,4:c 0,3:");
}

static unsigned
pattern(unsigned x, unsigned y, unsigned ch, unsigned mask)
{
	return (x / 5 + y / ch + y % ch) & mask;
}

static void
seed_scroll_scene(struct fixture *f, unsigned resolution)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	const unsigned ch = resolution == 2 ? 16 : 8;
	const unsigned mask = resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
	const unsigned rows = (unsigned)t->height;
	for (unsigned row = 0; row < rows; ++row) {
		char wire[24];
		(void)snprintf(wire, sizeof(wire), "G#p 3,%u:%c", row, 'A' + (int)row);
		feed(f, wire);
	}
	/* Keep the cells as landmarks but overwrite their raster in both
	 * phases with different scanline data, including the status row. */
	struct st_pixel_buffer *source = NULL;
	CHECK(st_pixel_buffer_create(&source, igs->width, igs->height, true) == 0);
	for (unsigned y = 0; y < igs->height; ++y)
		for (unsigned x = 0; x < igs->width; ++x) {
			unsigned pen = pattern(x, y, ch, mask);
			size_t i = (size_t)y * igs->width + x;
			source->primary[i] = 768 + pen;
			source->alternate[i] = 768 + (pen ^ mask);
		}
	const struct st_pixel_blit blit = {.width = igs->width, .height = igs->height, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_blit_pixels(f->console, source, NULL, &blit) == 0);
	st_pixel_buffer_destroy(source);
}

static void
check_scroll_scene(struct fixture *f, unsigned resolution, unsigned top, int displacement)
{
	struct cterminal *t = f->terminal;
	struct cterm_igs *igs = t->igs_engine;
	const unsigned ch = resolution == 2 ? 16 : 8;
	const unsigned mask = resolution == 0 ? 15 : resolution == 1 ? 3 : 1;
	const unsigned rows = (unsigned)t->height;
	const unsigned blank = 768 + (3 & mask);
	const struct st_console_rect full = {0, 0, igs->width, igs->height};
	struct st_pixel_buffer *actual = NULL;
	CHECK(st_console_get_pixels(&actual, f->console, &full, false) == 0);
	for (unsigned y = 0; y < igs->height; ++y) {
		unsigned row = y / ch;
		bool affected = row >= top && row < rows;
		int source_row = affected ? (int)row - displacement : (int)row;
		bool blanked = affected && (source_row < (int)top || source_row >= (int)rows);
		unsigned source_y = blanked ? y : (unsigned)source_row * ch + y % ch;
		for (unsigned x = 0; x < igs->width; ++x) {
			unsigned pen = pattern(x, source_y, ch, mask);
			size_t i = (size_t)y * igs->width + x;
			CHECK(actual->primary[i] == (blanked ? blank : 768 + pen));
			CHECK(actual->alternate[i] == (blanked ? blank : 768 + (pen ^ mask)));
		}
	}
	st_pixel_buffer_destroy(actual);
	const struct st_console_rect column = {3, 0, 1, rows};
	struct st_console_cell cells[25];
	CHECK(st_console_read_cells(f->console, &column, cells, 1) == 0);
	for (unsigned row = 0; row < rows; ++row) {
		int source_row = row < top ? (int)row : (int)row - displacement;
		bool blanked = row >= top && (source_row < (int)top || source_row >= (int)rows);
		CHECK(cells[row].glyph == (uint32_t)(blanked ? ' ' : 'A' + source_row));
		if (blanked)
			CHECK(cells[row].background == blank && (cells[row].flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) == 0);
	}
}

static void
check_cell(struct fixture *f, unsigned x, unsigned y, unsigned glyph)
{
	const struct st_console_rect area = {x, y, 1, 1};
	struct st_console_cell cell;
	CHECK(st_console_read_cells(f->console, &area, &cell, 1) == 0 && cell.glyph == glyph);
}

#endif
