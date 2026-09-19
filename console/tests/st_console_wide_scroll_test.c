/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define main original_console_test_main
#include "st_console_test.c"
#undef main

int
main(int argc, char **argv)
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
		.wide_glyph_stride = 2, .mapping = map, .mapping_count = 2,
	};
	const struct st_console_rect window = {1, 0, 2, 2};
	const struct st_console_rect row = {0, 0, 4, 1};
	struct st_console_cell cells[4];
	st_console_t *console = create_headless(4, 2);
	bool paired;
	bool deferred = argc == 2 && strcmp(argv[1], "registration") == 0;

	if (deferred) {
		const struct st_console_rect single = {0, 0, 1, 1};
		const struct st_console_cell pending = {
			.codepoint = 0x4e00, .font = 0,
		};
		CHECK(st_console_write_cells(console, &single, &pending, 1) == 0);
	}

	CHECK(register_static_font(console, &font) == 0);
	if (!deferred)
		CHECK(st_console_put_codepoint(console, 0x4e00) == 0);
	CHECK(st_console_read_cells(console, &row, cells, 4) == 0);
	CHECK(cells[0].codepoint == 0x4e00);
	if (deferred) {
		paired = (cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0;
		fprintf(stderr, "after registration: next=U+%04x continuation=%d\n",
		    (unsigned)cells[1].codepoint, paired);
		st_console_destroy(console);
		CHECK(paired);
		return EXIT_SUCCESS;
	}
	CHECK((cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	/* A vertical subwindow cuts through the existing pair at its left edge. */
	CHECK(st_console_set_window(console, &window) == 0);
	CHECK(st_console_scroll(console, 1, ' ') == 0);
	CHECK(st_console_read_cells(console, &row, cells, 4) == 0);
	paired = cells[0].codepoint != 0x4e00
	    || (cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0;
	fprintf(stderr, "after scroll: lead=U+%04x next=U+%04x continuation=%d\n",
	    (unsigned)cells[0].codepoint, (unsigned)cells[1].codepoint,
	    (cells[1].flags & ST_CONSOLE_CELL_CONTINUATION) != 0);
	st_console_destroy(console);
	CHECK(paired);
	return EXIT_SUCCESS;
}
