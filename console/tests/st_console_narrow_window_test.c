/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#define main original_console_test_main
#include "st_console_test.c"
#undef main

int
main(void)
{
	static const uint8_t narrow[] = {0};
	static const uint8_t wide[] = {0xff, 0xff};
	static const struct st_console_font_mapping map[] = {
		{' ', 0}, {0x4e00, 1},
	};
	const struct st_console_font font = {
		.bitmap = narrow, .bitmap_size = sizeof(narrow),
		.width = 8, .height = 1, .glyph_count = 1,
		.row_stride = 1, .glyph_stride = 1,
		.wide_bitmap = wide, .wide_bitmap_size = sizeof(wide),
		.wide_glyph_count = 1, .wide_row_stride = 2,
		.wide_glyph_stride = 2,
		.mapping = map, .mapping_count = 2,
	};
	const struct st_console_rect window = {1, 0, 1, 3};
	const struct st_console_rect row = {0, 1, 4, 1};
	struct st_console_cell cells[4];
	st_console_t *console = create_headless(4, 3);
	bool preserved;

	CHECK(register_static_font(console, &font) == 0);
	CHECK(st_console_set_window(console, &window) == 0);
	CHECK(st_console_put_codepoint(console, 0x4e00) == 0);
	CHECK(st_console_read_cells(console, &row, cells, 4) == 0);
	preserved = cells[2].codepoint == ' '
	    && (cells[2].flags & ST_CONSOLE_CELL_CONTINUATION) == 0;
	fprintf(stderr, "outside window: U+%04x, continuation=%d\n",
	    (unsigned int)cells[2].codepoint,
	    (cells[2].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	st_console_destroy(console);
	return preserved ? EXIT_SUCCESS : EXIT_FAILURE;
}
