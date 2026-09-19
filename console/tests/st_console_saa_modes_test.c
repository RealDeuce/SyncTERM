/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

static bool
check_mode(bool prestel)
{
	static const uint8_t glyphs[] = {0, 0, 0x80, 0x01};
	static const struct st_console_font_mapping mapping[] = {{0, 0}, {1, 1}};
	const struct st_console_font font = {
		.bitmap = glyphs, .bitmap_size = sizeof(glyphs),
		.width = 8, .height = 2, .glyph_count = 2,
		.row_stride = 1, .glyph_stride = 2,
		.mapping = mapping, .mapping_count = 2,
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 2, .rows = 2, .cell_width = 8, .cell_height = 2,
	};
	const struct st_console_rect grid = {0, 0, 2, 2};
	const struct st_console_rect raster = {0, 0, 16, 4};
	struct st_console_cell cells[4] = {
		{.codepoint = 1, .foreground = 7,
		 .flags = ST_CONSOLE_CELL_DOUBLE_HEIGHT},
		{.codepoint = 0, .foreground = 7, .background = 1},
		{.codepoint = 0, .foreground = 7,
		 .flags = ST_CONSOLE_CELL_DOUBLE_HEIGHT},
		{.codepoint = 0, .foreground = 7, .background = 2},
	};
	st_console_t *console = NULL;
	struct st_pixel_buffer *pixels = NULL;
	uint16_t identifier;
	bool glyph_ok;
	bool background_ok;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	for (size_t i = 0; i < 4; ++i) {
		cells[i].font = identifier;
		cells[i].flags |= ST_CONSOLE_CELL_SAA5050;
		if (prestel)
			cells[i].flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
	}
	CHECK(st_console_write_cells(console, &grid, cells, 2) == 0);
	CHECK(st_console_get_pixels(&pixels, console, &raster, true) == 0);
	/* Legacy Prestel borrows the upper glyph; Mode 7 uses the lower glyph. */
	glyph_ok = pixels->primary[2 * 16 + 7]
	    == (prestel ? 7U : 0U);
	/* A non-double upper cell still supplies Prestel's blank background. */
	background_ok = pixels->primary[2 * 16 + 8]
	    == (prestel ? 1U : 2U);
	fprintf(stderr, "%s: glyph source %s (%08x); blank background %s (%08x)\n",
	    prestel ? "Prestel" : "Mode 7", glyph_ok ? "PASS" : "FAIL",
	    (unsigned)pixels->primary[2 * 16 + 7],
	    background_ok ? "PASS" : "FAIL",
	    (unsigned)pixels->primary[2 * 16 + 8]);
	st_pixel_buffer_destroy(pixels);
	st_console_destroy(console);
	return glyph_ok && background_ok;
}

int
main(void)
{
	bool prestel = check_mode(true);
	bool mode7 = check_mode(false);

	return prestel && mode7 ? EXIT_SUCCESS : EXIT_FAILURE;
}
