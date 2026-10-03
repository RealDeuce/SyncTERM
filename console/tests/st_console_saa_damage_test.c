/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

int
main(void)
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
		.columns = 1, .rows = 2, .cell_width = 8, .cell_height = 2,
	};
	const struct st_console_rect grid = {0, 0, 1, 2};
	const struct st_console_rect top = {0, 0, 1, 1};
	const struct st_console_rect raster = {0, 0, 8, 4};
	struct st_console_cell cells[2] = {
		{.glyph = 1, .foreground = 7,
		 .flags = ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_DOUBLE_HEIGHT},
		{.glyph = 0, .foreground = 7,
		 .flags = ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_TERMINAL_FONT},
	};
	st_console_t *console = NULL;
	struct st_pixel_buffer *incremental = NULL;
	struct st_pixel_buffer *forced = NULL;
	uint16_t identifier;
	bool equal;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &identifier) == 0);
	cells[0].font = identifier;
	cells[1].font = identifier;
	CHECK(st_console_write_cells(console, &grid, cells, 1) == 0);
	CHECK(st_console_present(console) == 0);
	/* Change the glyph but retain double-height mode in the upper row. */
	cells[0].glyph = 0;
	CHECK(st_console_write_cells(console, &top, cells, 1) == 0);
	CHECK(st_console_get_pixels(&incremental, console, &raster, false) == 0);
	CHECK(st_console_get_pixels(&forced, console, &raster, true) == 0);
	equal = memcmp(incremental->primary, forced->primary,
	    32 * sizeof(incremental->primary[0])) == 0
	    && memcmp(incremental->alternate, forced->alternate,
	    32 * sizeof(incremental->alternate[0])) == 0;
	st_pixel_buffer_destroy(incremental);
	st_pixel_buffer_destroy(forced);
	st_console_destroy(console);
	CHECK(equal);
	return EXIT_SUCCESS;
}
