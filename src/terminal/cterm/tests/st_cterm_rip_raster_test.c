/* Native RIP raster arithmetic smoke tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_raster.h"
#include "cterm_rip_fonts.h"
#include "cterm_rip_icons.h"

#include "st_pixels.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
pixel(st_console_t *console, uint32_t x, uint32_t y, uint32_t *result)
{
	struct st_pixel_buffer *buffer = NULL;
	const struct st_console_rect rect = {x, y, 1, 1};
	int status = st_console_get_pixels(&buffer, console, &rect, false);

	if (status == 0)
		*result = buffer->primary[0];
	st_pixel_buffer_destroy(buffer);
	return status;
}

static int
test_reserved_fields_required(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "010101",
	    &handled) == 0 && handled);
	CHECK(rip.font_number == 0 && !rip.font_vertical
	    && rip.font_size == 1);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "01010100",
	    &handled) == 0 && handled);
	CHECK(rip.font_number == 1 && rip.font_vertical
	    && rip.font_size == 1);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'T', "0A0A1414",
	    &handled) == 0 && handled);
	CHECK(rip.text_region.sx == -1);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'T', "0A0A141400",
	    &handled) == 0 && handled);
	CHECK(rip.text_region.sx == 10 && rip.text_region.sy == 10
	    && rip.text_region.ex == 40 && rip.text_region.ey == 40);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_empty_rip_text_advances(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "01000300",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '@', "157E",
	    &handled) == 0 && handled);
	/* v1.10a substitutes one space for an empty ++ text argument. */
	CHECK(rip.x == 53 && rip.y == 266);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'T', "Im",
	    &handled) == 0 && handled);
	CHECK(rip.x == 84 && rip.y == 266);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'T', "",
	    &handled) == 0 && handled);
	CHECK(rip.x == 96 && rip.y == 266);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_vertical_button_shadow(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	uint32_t colour;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'f', "SGLC",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "02010400",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "GH11020080000F080F080000000F07000000",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U',
	    "61J500000000<>UP/DOWN   Moves Lightbar   ENTER to View   ESC to Quit<>",
	    &handled) == 0 && handled);
	/* v1.10a's button shadow writes each vertical glyph directly. */
	CHECK(pixel(console, 523, 177, &colour) == 0
	    && colour == rip.palette[8]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_vertical_button_highlight(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	uint32_t colour;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "01010100",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "280U020074000F080F080000020407000000",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U',
	    "2S2S503M1U00<>AB<>", &handled) == 0);
	/* v1.10a's vertical prefix glyph leaves Y for the hotkey glyph. */
	CHECK(pixel(console, 147, 81, &colour) == 0
	    && colour == rip.palette[4]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_initial_borders(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	uint32_t before, after;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(!rip.borders);
	rip.colour = 15;
	CHECK(pixel(console, 355, 103, &before) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'C', "A13J0W",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 355, 103, &after) == 0 && after == before);
	CHECK(cterm_rip_raster_reset_windows(&rip) == 0);
	CHECK(rip.borders);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'C', "A13J0W",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 355, 103, &after) == 0
	    && after == rip.palette[15]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_capture_reserved_field_width(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 25,
		.cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	uint32_t colour;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(st_console_set_pixel(console, 1, 1, rip.palette[4]) == 0);
	/* v1.10a parses the one-digit reserved field and ignores the ! that
	 * follows a swallowed LF in an LF-only stream. */
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'C', "010101010!",
	    &handled) == 0 && handled);
	CHECK(rip.clipboard != NULL && rip.clipboard->width == 1
	    && rip.clipboard->height == 1);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "0201000",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 2, 1, &colour) == 0
	    && colour == rip.palette[4]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_world_frame(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	uint32_t colour;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(!rip.borders);
	/* 800x700 virtual pixels map to the 640x350 physical screen. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'f', "M8JG",
	    &handled) == 0 && handled);
	CHECK(rip.x_dim == 800 && rip.y_dim == 700
	    && rip.x_max == 640 && rip.y_max == 350);
	CHECK(rip.viewport.ex == 799 && rip.viewport.ey == 699);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'v', "0000P0M8",
	    &handled) == 0 && handled);
	CHECK(rip.viewport.ex == 799 && rip.viewport.ey == 699);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'X', "2S2S",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 80, 50, &colour) == 0
	    && colour == rip.palette[1]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'W', "1",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "02",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'j', "2S2S",
	    &handled) == 0 && handled);
	/* Legacy draw_pixel reads XOR source at unscaled (100,100), then
	 * rip_setpixel writes the result at mapped (80,50). */
	CHECK(pixel(console, 80, 50, &colour) == 0
	    && colour == rip.palette[2]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'j', "2S2S",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 80, 50, &colour) == 0
	    && colour == rip.palette[2]);
	CHECK(st_console_set_pixel(console, 100, 100, rip.palette[4]) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'j', "2S2S",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 80, 50, &colour) == 0
	    && colour == rip.palette[6]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'W', "0",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "01",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 100, 100, &colour) == 0
	    && colour != rip.palette[1]);
	cterm_rip_raster_fill_style(&rip, 1, 14);
	cterm_rip_raster_bar(&rip, 10, 20, 12, 22);
	CHECK(pixel(console, 8, 10, &colour) == 0
	    && colour == rip.palette[14]);
	CHECK(pixel(console, 10, 11, &colour) == 0
	    && colour == rip.palette[14]);
	cterm_rip_raster_fill_style(&rip, 3, 12);
	cterm_rip_raster_bar(&rip, 15, 20, 17, 20);
	CHECK(pixel(console, 13, 10, &colour) == 0
	    && colour == rip.palette[12]);
	CHECK(pixel(console, 12, 10, &colour) == 0
	    && colour == rip.palette[0]);
	cterm_rip_raster_fill_style(&rip, 1, 14);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'p',
	    "04" "2S2S" "322S" "3232" "2S32",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 84, 52, &colour) == 0
	    && colour == rip.palette[14]);
	/* Legacy flood capture refuses a viewport wider than the screen. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'F', "2S2S01",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'f', "0000",
	    &handled) == 0 && handled);
	CHECK(rip.x_dim == 800 && rip.y_dim == 700);
	CHECK(cterm_rip_raster_reset_windows(&rip) == 0);
	CHECK(rip.borders);
	CHECK(rip.x_dim == 640 && rip.y_dim == 350
	    && rip.x_max == 640 && rip.y_max == 350);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'X', "2S2S",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 100, 100, &colour) == 0
	    && colour == rip.palette[1]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_polygon_extra_numeric_pairs(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	uint32_t colour;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	rip.colour = 15;
	rip.fill_colour = 15;
	/* Legacy validates all numeric fields but draws only the declared
	 * vertices, even when extra complete pairs follow the polygon. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'p',
	    "05DQ1RDL1YE11ZE31WE31Wxp04DK1YDH1QDU1TDU1T",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 493, 64, &colour) == 0
	    && colour == rip.palette[15]);
	/* A declared count larger than the supplied pairs is malformed;
	 * never read uninitialized coordinates to complete it. */
	CHECK(pixel(console, 110, 110, &colour) == 0
	    && colour != rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'p',
	    "052S2S322S3232", &handled) == 0 && handled);
	CHECK(pixel(console, 110, 110, &colour) == 0
	    && colour != rip.palette[15]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_poly_beziers(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	struct st_pixel_buffer *single = NULL;
	struct st_pixel_buffer *poly = NULL;
	uint32_t colour;
	bool handled;

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	/* The legacy handler's first type-0 block supplies the base point;
	 * the following blocks draw the visible polyline. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 't',
	    "03010" "2S2S" "03C2S" "03C3C", &handled) == 0 && handled);
	CHECK(pixel(console, 110, 100, &colour) == 0);
	CHECK(colour == rip.palette[1]);
	CHECK(pixel(console, 120, 110, &colour) == 0);
	CHECK(colour == rip.palette[1]);
	CHECK(pixel(console, 110, 110, &colour) == 0);
	CHECK(colour != rip.palette[1]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'z',
	    "03010" "2S2S" "03C2S" "03C3C", &handled) == 0 && handled);
	CHECK(pixel(console, 110, 110, &colour) == 0);
	CHECK(colour == rip.palette[1]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'x',
	    "03010" "5K2S" "0642S" "0643C", &handled) == 0 && handled);
	CHECK(pixel(console, 215, 102, &colour) == 0);
	CHECK(colour == rip.palette[7]);
	CHECK(pixel(console, 190, 100, &colour) == 0);
	CHECK(colour != rip.palette[7]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Z',
	    "8C2S8M2S8M328C2S08", &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 't',
	    "0108" "1B42SBE2SBE32", &handled) == 0 && handled);
	const struct st_console_rect single_rect = {300, 95, 25, 25};
	const struct st_console_rect poly_rect = {400, 95, 25, 25};
	CHECK(st_console_get_pixels(&single, console, &single_rect, false) == 0);
	CHECK(st_console_get_pixels(&poly, console, &poly_rect, false) == 0);
	CHECK(memcmp(single->primary, poly->primary,
	    25u * 25u * sizeof(single->primary[0])) == 0);
	st_pixel_buffer_destroy(single);
	st_pixel_buffer_destroy(poly);
	/* Curved segments do not set the filled-polygon boundary marks. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'x',
	    "0108" "1DW2SDW2SDS32", &handled) == 0 && handled);
	CHECK(pixel(console, 500, 105, &colour) == 0);
	CHECK(colour != rip.palette[7]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "0000020074000F000F080700000008000000",
	    &handled) == 0 && handled);
	CHECK(rip.button_style.flags == 256);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "000002007C000F000F080700000008ZZZZZZ",
	    &handled) == 0 && handled);
	CHECK(rip.button_style.flags == 256);
	CHECK(rip.poly_fill_marks == NULL && rip.error == 0);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43,
		.cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	struct st_pixel_buffer *before = NULL;
	struct st_pixel_buffer *after = NULL;
	uint32_t colour;
	uint32_t palette[32];
	const uint8_t *bitmap_font;
	size_t bitmap_size;
	bool handled;

	CHECK(test_initial_borders() == 0);
	CHECK(test_reserved_fields_required() == 0);
	CHECK(test_empty_rip_text_advances() == 0);
	CHECK(test_vertical_button_shadow() == 0);
	CHECK(test_vertical_button_highlight() == 0);
	CHECK(test_world_frame() == 0);
	CHECK(test_polygon_extra_numeric_pairs() == 0);

	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	cterm_rip_raster_line(&rip, 10, 10, 13, 10);
	CHECK(rip.error == 0);
	for (uint32_t x = 10; x <= 13; x++) {
		CHECK(pixel(console, x, 10, &colour) == 0);
		CHECK(colour == 0x14);
	}
	CHECK(pixel(console, 14, 10, &colour) == 0);
	CHECK(colour != 0x14);

	rip.line_pattern = 0x0001;
	cterm_rip_raster_line(&rip, 20, 10, 23, 10);
	CHECK(rip.error == 0);
	CHECK(pixel(console, 20, 10, &colour) == 0);
	CHECK(colour == 0x14);
	CHECK(pixel(console, 21, 10, &colour) == 0);
	CHECK(colour != 0x14);

	rip.line_pattern = 0xffff;
	rip.line_width = 3;
	cterm_rip_raster_line(&rip, 30, 10, 33, 10);
	CHECK(rip.error == 0);
	for (uint32_t y = 9; y <= 11; y++) {
		CHECK(pixel(console, 31, y, &colour) == 0);
		CHECK(colour == 0x14);
	}

	cterm_rip_raster_fill_style(&rip, 1, 14);
	cterm_rip_raster_bar(&rip, 40, 10, 42, 11);
	CHECK(rip.error == 0);
	CHECK(pixel(console, 41, 10, &colour) == 0);
	CHECK(colour == rip.palette[14]);
	cterm_rip_raster_fill_style(&rip, 3, 12);
	cterm_rip_raster_bar(&rip, 48, 16, 55, 23);
	CHECK(rip.error == 0);
	CHECK(pixel(console, 55, 16, &colour) == 0);
	CHECK(colour == rip.palette[12]);
	CHECK(pixel(console, 48, 16, &colour) == 0);
	CHECK(colour == rip.palette[0]);

	rip.line_width = 1;
	cterm_rip_raster_rectangle(&rip, 60, 10, 62, 12);
	CHECK(rip.error == 0);
	CHECK(pixel(console, 60, 11, &colour) == 0);
	CHECK(colour == rip.palette[1]);
	CHECK(pixel(console, 61, 11, &colour) == 0);
	CHECK(colour != rip.palette[1]);

	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "0f",
	    &handled) == 0 && handled);
	CHECK(rip.colour == 15);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'W', "1",
	    &handled) == 0 && handled);
	CHECK(rip.xor_mode);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'L', "2S1E2S1E",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 100, 50, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'L', "2S1E2S1E",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 100, 50, &colour) == 0);
	CHECK(colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'X', "2S1E",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 100, 50, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'W', "0",
	    &handled) == 0 && handled);
	CHECK(!rip.xor_mode);
	CHECK(cterm_rip_raster_command(&rip, 9, 0, 0x1b, "",
	    &handled) == 0 && !handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'S', "0102",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'p', "03461E4A1E461I",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 151, 51, &colour) == 0);
	CHECK(colour == rip.palette[2]);
	CHECK(pixel(console, 150, 50, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 's',
	    "01010101010101010F", &handled) == 0 && handled);
	CHECK(rip.fill_style == -1);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'B', "5K2S5R2Z",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 207, 100, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(pixel(console, 200, 100, &colour) == 0);
	CHECK(colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'm', "5K2S",
	    &handled) == 0 && handled);
	CHECK(rip.x == 200 && rip.y == 100);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'l',
	    "035K3W5K3Y5M3Y", &handled) == 0 && handled);
	CHECK(pixel(console, 200, 142, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'v', "0A0A1E1E",
	    &handled) == 0 && handled);
	CHECK(rip.viewport.sx == 10 && rip.viewport.sy == 10);
	CHECK(rip.viewport.ex == 50 && rip.viewport.ey == 50);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'X', "0000",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 10, 10, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'v', "00000000",
	    &handled) == 0 && handled);
	CHECK(rip.viewport.ex == 0 && rip.viewport.ey == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'v', "0A0A1E1E",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'E', "",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 10, 10, &colour) == 0);
	CHECK(colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'a', "0100",
	    &handled) == 0 && handled);
	CHECK(st_console_get_palette(console, 0, palette, 32) == 0);
	CHECK(palette[rip.palette[1]] == 0x000000);
	CHECK(pixel(console, 10, 10, &colour) == 0);
	CHECK(colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Q',
	    "00000000000000000000000000000000",
	    &handled) == 0 && handled);
	CHECK(st_console_get_palette(console, 0, palette, 32) == 0);
	for (size_t i = 0; i < 16; i++)
		CHECK(palette[rip.palette[i]] == 0x000000);
	rip.viewport.sx = 0;
	rip.viewport.sy = 0;
	rip.viewport.ex = 639;
	rip.viewport.ey = 349;
	const struct st_console_rect text_rect = {300, 200, 16, 8};
	CHECK(st_console_get_pixels(&before, console, &text_rect, false) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "00000100",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '@', "8C5KAB",
	    &handled) == 0 && handled);
	CHECK(rip.x == 316 && rip.y == 200);
	CHECK(st_console_get_pixels(&after, console, &text_rect, false) == 0);
	bitmap_font = cterm_rip_font_data(0, &bitmap_size);
	CHECK(bitmap_font != NULL && bitmap_size == 2048);
	for (size_t y = 0; y < 8; y++) {
		for (size_t x = 0; x < 16; x++) {
			uint8_t bits = bitmap_font[(x < 8 ? 'A' : 'B') * 8 + y];
			uint32_t expected = bits & (0x80 >> (x & 7))
			    ? rip.palette[15] : before->primary[y * 16 + x];

			CHECK(after->primary[y * 16 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(before);
	st_pixel_buffer_destroy(after);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "01000100",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '@', "8C78A",
	    &handled) == 0 && handled);
	CHECK(rip.font_number == 1 && rip.font_size == 1);
	CHECK(rip.error == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'S', "0102",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'N', "01",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'K', "5M5M5K5K",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 201, 201, &colour) == 0);
	CHECK(colour == rip.palette[2]);
	CHECK(pixel(console, 200, 200, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'N', "00",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'K', "5U5K5W5M",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 210, 200, &colour) == 0);
	CHECK(colour == rip.palette[2]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'j', "6464",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 220, 220, &colour) == 0);
	CHECK(colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'S', "010E",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'B', "8C6Y8E70",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'C', "8C6Y8E7000",
	    &handled) == 0 && handled);
	CHECK(rip.clipboard != NULL);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "8M6Y000",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 311, 251, &colour) == 0);
	CHECK(colour == rip.palette[14]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "8M6Y010",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 311, 251, &colour) == 0);
	CHECK(colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "8W6Y020",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 321, 251, &colour) == 0);
	CHECK(colour == rip.palette[14]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "8W6Y030",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 321, 251, &colour) == 0);
	CHECK(colour == rip.palette[14]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "966Y040",
	    &handled) == 0 && handled);
	CHECK(pixel(console, 331, 251, &colour) == 0);
	CHECK(colour == rip.palette[1]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "00000200E9010F000F080700000F07000000",
	    &handled) == 0 && handled);
	CHECK(rip.button_style.type == 1);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U',
	    "9Q6Y00000000<>", &handled) == 0 && handled);
	CHECK(pixel(console, 351, 251, &colour) == 0);
	CHECK(colour == rip.palette[14]);
	CHECK(pixel(console, 349, 249, &colour) == 0);
	CHECK(colour == rip.palette[7]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U',
	    "AA6Y00000010<>", &handled) == 0 && handled);
	CHECK(pixel(console, 371, 251, &colour) == 0);
	CHECK(colour == rip.palette[1]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "00000200N4000F000F080700000F07000000",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U',
	    "2S2S3C320000<>", &handled) == 0 && handled);
	CHECK(rip.clipboard != NULL);
	CHECK(rip.clipboard->width == 23 && rip.clipboard->height == 13);
	CHECK(rip.button_style.type == 1);
	CHECK((rip.button_style.flags & (8 | 64 | 256 | 512 | 32768)) == 0);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U',
	    "462S00000000<>", &handled) == 0 && handled);
	CHECK(pixel(console, 150, 100, &colour) == 0);
	CHECK(colour == rip.palette[7]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'I',
	    "A06Y00110XBUTTON", &handled) == 0 && handled);
	CHECK(rip.clipboard != NULL);
	CHECK(rip.clipboard->width == 22 && rip.clipboard->height == 17);
	const struct builtin_icon *icon = NULL;
	for (size_t i = 0; i < builtin_icon_count; i++) {
		if (strcmp(builtin_icons[i].filename, "XBUTTON.ICN") == 0) {
			icon = &builtin_icons[i];
			break;
		}
	}
	CHECK(icon != NULL);
	const struct st_console_rect icon_rect = {360, 250, 22, 17};
	CHECK(st_console_get_pixels(&after, console, &icon_rect, false) == 0);
	for (size_t i = 0; i < 22u * 17u; i++) {
		uint8_t byte = icon->data[i / 2];
		int index = (i & 1) != 0 ? byte & 15 : byte >> 4;

		CHECK(after->primary[i] == rip.palette[index]);
	}
	st_pixel_buffer_destroy(after);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "00000100",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'T', "B46YCI8C00",
	    &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 't', "0AB",
	    &handled) == 0 && handled);
	CHECK(rip.text_region.ypos == 11);
	const struct st_console_rect region_rect = {400, 250, 16, 8};
	CHECK(st_console_get_pixels(&after, console, &region_rect, false) == 0);
	for (size_t y = 0; y < 8; y++) {
		for (size_t x = 0; x < 16; x++) {
			uint8_t bits = bitmap_font[(x < 8 ? 'A' : 'B') * 8 + y];

			if ((bits & (0x80 >> (x & 7))) != 0)
				CHECK(after->primary[y * 16 + x]
				    == rip.palette[15]);
		}
	}
	st_pixel_buffer_destroy(after);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 't', "1A B",
	    &handled) == 0 && handled);
	CHECK(rip.text_region.ypos == 22 && rip.error == 0);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'E', "",
	    &handled) == 0 && handled);
	CHECK(rip.text_region.sx == -1 && rip.text_region.ypos == 0);
	cterm_rip_raster_destroy(&rip);

	st_console_destroy(console);
	CHECK(test_poly_beziers() == 0);
	CHECK(test_capture_reserved_field_width() == 0);
	return 0;
}
