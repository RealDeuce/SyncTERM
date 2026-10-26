/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "console_internal.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "failed at %d: %s\n", __LINE__, #c); \
	exit(EXIT_FAILURE); } } while (0)

int
main(void)
{
	static const uint8_t bits[] = {0xff, 0xff};
	static const struct st_console_font_mapping mapping[] = {{65, 0}};
	const struct st_console_font font = {
		.bitmap = bits, .bitmap_size = sizeof(bits),
		.width = 8, .height = 2, .glyph_count = 1,
		.row_stride = 1, .glyph_stride = 2,
		.mapping = mapping, .mapping_count = 1,
	};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 2, .cell_width = 8, .cell_height = 2,
		.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL,
	};
	struct st_console_cell cells[2] = {
		{.glyph = 0, .foreground = 7, .flags =
		 ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_TERMINAL_FONT
		 | ST_CONSOLE_CELL_DOUBLE_HEIGHT | ST_CONSOLE_CELL_BLINK},
		{.glyph = 0, .foreground = 7, .flags =
		 ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_TERMINAL_FONT},
	};
	const struct st_console_rect grid = {0, 0, 1, 2};
	struct st_console_bitmap_presenter *presenter = NULL;
	struct st_console_bitmap_frame frame;
	st_console_t *console = NULL;
	uint16_t id;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(st_console_register_static_font(console, &font, &id) == 0);
	cells[0].font = cells[1].font = id;
	cells[0].glyph_font = cells[1].glyph_font = id;
	CHECK(st_console_write_cells(console, &grid, cells, 1) == 0);
	CHECK(st_console_set_blink_mode(console, ST_CONSOLE_BLINK_PRESTEL) == 0);
	/* Presenter tests run without backend threads; hold the state lock. */
	CHECK(st_console_begin_update(console) == 0);
	console->cursor_visible = false;
	CHECK(st_console_rasterize_locked(console, true) == 0);
	CHECK(st_console_bitmap_presenter_create(&presenter, console) == 0);
	CHECK(st_console_bitmap_presenter_frame(presenter, console, 8, 4,
	    1000, NULL, NULL, &frame) == 0);
	CHECK(frame.pixels->pixels[0] == frame.pixels->pixels[16]);
	/* Timer-only frame: both halves must enter the same blink phase. */
	CHECK(st_console_bitmap_presenter_frame(presenter, NULL, 8, 4,
	    1400, NULL, NULL, &frame) == 0);
	fprintf(stderr, "upper=%06x lower=%06x\n",
	    (unsigned)frame.pixels->pixels[0],
	    (unsigned)frame.pixels->pixels[16]);
	bool equal = frame.pixels->pixels[0] == frame.pixels->pixels[16];
	CHECK(st_console_end_update(console) == 0);
	st_console_bitmap_presenter_destroy(presenter);
	st_console_destroy(console);
	CHECK(equal);
	return EXIT_SUCCESS;
}
