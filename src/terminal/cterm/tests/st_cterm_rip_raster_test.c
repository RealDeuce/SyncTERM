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
test_signed_arguments(void)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {.columns = 80, .rows = 43,
	    .cell_width = 8, .cell_height = 8, .pixel_width = 640, .pixel_height = 350};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	bool handled;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '=', "04-00101", &handled) == 0 && handled);
	CHECK(rip.line_style == 4 && rip.line_pattern == 0xffff && rip.line_width == 1);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '=', "041EKF01", &handled) == 0 && handled);
	CHECK(rip.line_pattern == 0xffff);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '=', "-1000001", &handled) == 0);
	CHECK(rip.line_style == 4 && rip.line_pattern == 0xffff);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '=', "040000-1", &handled) == 0);
	CHECK(rip.line_width == 1 && rip.line_pattern == 0xffff);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '=', "041EKG01", &handled) == 0);
	CHECK(rip.line_pattern == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "-1", &handled) == 0);
	CHECK(rip.colour == 15);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "A-", &handled) == 0);
	CHECK(rip.colour == 10);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "74", &handled) == 0);
	CHECK(rip.colour == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'S', "01-1", &handled) == 0);
	CHECK(rip.fill_style == 1 && rip.fill_colour == 15);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'S', "-102", &handled) == 0);
	CHECK(rip.fill_style == 1 && rip.fill_colour == 15);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 's', "-1-A0073747501A-0F", &handled) == 0);
	const uint8_t expected_pattern[8] = {255, 246, 0, 255, 0, 1, 1, 10};
	CHECK(memcmp(rip.fill_pattern, expected_pattern, 8) == 0 && rip.fill_colour == 15);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 's', "0000000000000000-1", &handled) == 0);
	CHECK(memcmp(rip.fill_pattern, expected_pattern, 8) == 0 && rip.fill_colour == 15);
	uint8_t palette[16];
	memcpy(palette, rip.master_palette, 16);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'a', "-10F", &handled) == 0);
	CHECK(memcmp(palette, rip.master_palette, 16) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'a', "00-1", &handled) == 0);
	CHECK(rip.master_palette[0] == 63);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Q',
	    "-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1-1", &handled) == 0);
	for (size_t i = 0; i < 16; ++i) CHECK(rip.master_palette[i] == 63);
	CHECK(cterm_rip_raster_reset_windows(&rip) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "01000100", &handled) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "-1-17500", &handled) == 0);
	CHECK(rip.font_number == 1 && rip.font_size == 1 && rip.font_vertical == 255);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Y', "00000100", &handled) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '@', "000", &handled) == 0);
	CHECK(rip.x == 8 && rip.y == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'm', "-101", &handled) == 0);
	CHECK(rip.x == -1 && rip.y == 1);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, '=', "00000001", &handled) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "0F", &handled) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'L', "-10A0A0A", &handled) == 0);
	uint32_t colour;
	CHECK(pixel(console, 0, 10, &colour) == 0 && colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'l', "02-10B0A0B-1-1", &handled) == 0);
	CHECK(pixel(console, 0, 11, &colour) == 0 && colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'P', "-1-10C0A0C", &handled) == 0);
	CHECK(pixel(console, 0, 12, &colour) == 0 && colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'v', "-1000A0A", &handled) == 0);
	CHECK(rip.viewport.sx == 0 && rip.viewport.ex == 639);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'C', "000A000A0", &handled) == 0);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "0K0A740", &handled) == 0);
	CHECK(pixel(console, 20, 10, &colour) == 0 && colour == rip.palette[15]);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', "0L0A-10", &handled) == 0);
	CHECK(pixel(console, 21, 10, &colour) == 0 && colour == rip.palette[0]);
	/* Both radii are unsigned ffff; this enormous outline lies outside
	 * the viewport. Minus-as-zero would draw the tiny radius-one ellipse. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'O', "2S2S00A0-1-1", &handled) == 0);
	CHECK(pixel(console, 101, 100, &colour) == 0 && colour == rip.palette[0]);
	/* Both flags spellings produce the same high-bit WORD; no signed flag
	 * or colour can become a negative palette/font-table index. */
	const char *styles[] = {
		"0000020PHC000F000F080700000008000000",
		"000002-P34000F000F080700000008000000",
	};
	for (size_t i = 0; i < sizeof(styles) / sizeof(*styles); ++i) {
		CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B', styles[i], &handled) == 0);
		CHECK(rip.button_style.flags == 0x8100);
	}
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "0000-10074000F000F080700000008000000", &handled) == 0);
	CHECK(rip.button_style.flags == 0x8100 && rip.button_style.orientation == 2);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "0000020074-10F000F080700000008000000", &handled) == 0);
	CHECK(rip.button_style.flags == 0x8100 && rip.button_style.bevel_size == 0);
	/* A negative fixed size selects the endpoint-sized plain button in
	 * both its drawing and its automatic clipboard capture. */
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'B',
	    "-1-102008W000F000F080700000008000000", &handled) == 0);
	CHECK(rip.button_style.width == -1 && rip.button_style.height == -1);
	CHECK(cterm_rip_raster_command(&rip, 1, 0, 'U', "2S2S322U0000", &handled) == 0);
	CHECK(rip.clipboard != NULL && rip.clipboard->width == 11 && rip.clipboard->height == 3);
	CHECK(rip.error == 0);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
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
	/* The reference has three reserved 2# fields, not one overflowing 6#.
	 * They are valid and ignored, so the flags word 007C is installed. */
	CHECK(rip.button_style.flags == 264);
	CHECK(rip.poly_fill_marks == NULL && rip.error == 0);
	/* Independent pixels from RIPterm's test08_bezier capture: these
	 * integer-boundary samples distinguish double rounding from FMA and
	 * extended-register evaluation. Neighbouring pixels stay clear. */
	CHECK(cterm_rip_raster_reset_windows(&rip) == 0);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "0E", &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Z',
	    "1E7S5K5K1E5K5K7S0U", &handled) == 0 && handled);
	CHECK(pixel(console, 193, 276, &colour) == 0 && colour == rip.palette[14]);
	CHECK(pixel(console, 194, 276, &colour) == 0 && colour == rip.palette[0]);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'c', "0F", &handled) == 0 && handled);
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'Z',
	    "BO1EDW1EDW8CG48C0K", &handled) == 0 && handled);
	CHECK(pixel(console, 484, 130, &colour) == 0 && colour == rip.palette[15]);
	CHECK(pixel(console, 483, 130, &colour) == 0 && colour == rip.palette[0]);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

static int
test_offset_viewport_line_clipping(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 80, .rows = 43, .cell_width = 8, .cell_height = 8,
		.pixel_width = 640, .pixel_height = 350,
	};
	const struct st_console_rect rect = {8, 8, 16, 6};

	for (int width = 1; width <= 3; width += 2) {
		st_console_t *console = NULL;
		struct cterm_rip_raster rip;
		struct st_pixel_buffer *pixels = NULL;
		bool handled;

		CHECK(st_console_create(&console, &display, &config) == 0);
		CHECK(cterm_rip_raster_init(&rip, console) == 0);
		CHECK(cterm_rip_raster_reset_windows(&rip) == 0);
		CHECK(cterm_rip_raster_command(&rip, 0, 0, 'v',
		    "0A0A0K0K", &handled) == 0 && handled);
		rip.colour = 15;
		rip.line_width = width;
		/* BGI applies the viewport offset before clipping in screen
		 * space. Clipping these relative coordinates must produce the
		 * same result. A thick line's center can be completely outside
		 * while one of its independently clipped side passes is visible. */
		cterm_rip_raster_line(&rip, 0, width == 1 ? 0 : -1,
		    30, width == 1 ? 0 : -1);
		CHECK(st_console_get_pixels(&pixels, console, &rect, false) == 0);
		for (uint32_t y = 0; y < rect.height; y++) {
			for (uint32_t x = 0; x < rect.width; x++) {
				uint32_t expected = y == 2 && x >= 2 && x <= 12
				    ? rip.palette[15] : rip.palette[0];

				CHECK(pixels->primary[y * rect.width + x] == expected);
			}
		}
		st_pixel_buffer_destroy(pixels);
		cterm_rip_raster_destroy(&rip);
		st_console_destroy(console);
	}
	return 0;
}

static int
test_in_place_pixels(void)
{
	/* BGI colour numbers and the installed native console indexes. */
	static const uint32_t indexes[16] = {
		16, 20, 18, 22, 17, 21, 19, 23, 24, 28, 26, 30, 25, 29, 27, 31
	};
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config config = {.columns = 81, .rows = 45,
		.cell_width = 8, .cell_height = 8, .pixel_width = 648, .pixel_height = 360};
	const uint32_t primary = ST_PIXEL_COLOR_RGB_VALUE(1, 2, 3);
	const uint32_t alternate = ST_PIXEL_COLOR_RGB_VALUE(4, 5, 6);
	const struct st_pixel_blit full_blit = {.width = 648, .height = 360, .scale_x = 1, .scale_y = 1};
	const struct st_console_rect full = {0, 0, 648, 360};
	st_console_t *console = NULL;
	struct cterm_rip_raster rip;
	struct st_pixel_buffer *base = NULL, *image = NULL, *row = NULL;
	bool handled;
	CHECK(st_console_create(&console, &display, &config) == 0);
	CHECK(cterm_rip_raster_init(&rip, console) == 0);
	CHECK(st_pixel_buffer_create(&base, 648, 360, true) == 0);
	CHECK(st_pixel_buffer_fill(base, primary, alternate) == 0);
	CHECK(st_console_blit_pixels(console, base, NULL, &full_blit) == 0);
	/* Reproduce the previous flood publication sequence explicitly. A
	 * snapshot owns two planes: modifying primary does not discard alternate,
	 * and the blitter copies that supplied alternate rather than primary. */
	const struct st_console_rect single = {0, 0, 1, 1};
	const struct st_pixel_blit single_blit = {.width = 1, .height = 1, .scale_x = 1, .scale_y = 1};
	CHECK(st_console_get_pixels(&image, console, &single, false) == 0);
	CHECK(image->alternate != NULL && image->alternate[0] == alternate);
	image->primary[0] = 17;
	CHECK(st_console_blit_pixels(console, image, NULL, &single_blit) == 0);
	st_pixel_buffer_destroy(image);
	CHECK(st_console_get_pixels(&image, console, &single, false) == 0);
	CHECK(image->primary[0] == 17 && image->alternate[0] == alternate);
	/* v1.10a explicitly removes that plane before publication. */
	st_pixel_buffer_remove_alternate(image);
	CHECK(st_console_blit_pixels(console, image, NULL, &single_blit) == 0);
	st_pixel_buffer_destroy(image);
	CHECK(st_console_get_pixels(&image, console, &single, false) == 0);
	CHECK(image->primary[0] == 17 && image->alternate[0] == 17);
	st_pixel_buffer_destroy(image);
	CHECK(st_console_blit_pixels(console, base, NULL, &full_blit) == 0);
	rip.viewport.sx = 9; rip.viewport.sy = 7; rip.viewport.ex = 15; rip.viewport.ey = 12;
	cterm_rip_raster_fill_style(&rip, 1, 4);
	cterm_rip_raster_bar(&rip, 0, 0, 6, 5);
	CHECK(rip.error == 0 && rip.drawing_pixels == NULL && rip.drawing_damage == NULL);
	CHECK(st_console_get_pixels(&image, console, &full, false) == 0);
	for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 648; ++x) {
		bool painted = x >= 9 && x <= 15 && y >= 7 && y <= 12;
		CHECK(image->primary[y * 648 + x] == (painted ? 17 : primary));
		CHECK(image->alternate[y * 648 + x] == (painted ? 17 : alternate));
	}
	st_pixel_buffer_destroy(image);
	/* Preserve the legacy unscaled XOR read and independently rounded write.
	 * Virtual X=21 and X=22 both write physical X=11; the latter wins. */
	rip.viewport.sx = rip.viewport.sy = 0; rip.viewport.ex = 1279; rip.viewport.ey = 699;
	rip.x_dim = 1280; rip.y_dim = 700;
	base->primary[20 * 648 + 20] = indexes[4];
	base->primary[20 * 648 + 21] = indexes[5];
	base->primary[20 * 648 + 22] = indexes[6];
	CHECK(st_console_blit_pixels(console, base, NULL, &full_blit) == 0);
	rip.colour = 2; rip.xor_mode = true;
	cterm_rip_raster_line(&rip, 20, 20, 22, 20);
	CHECK(rip.error == 0);
	CHECK(cterm_rip_raster_invert_rectangle(&rip, 10, 10, 12, 11) == 0);
	CHECK(st_console_get_pixels(&image, console, &full, false) == 0);
	for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 648; ++x) {
		uint32_t ink = x == 10 && y == 10 ? 28u : x == 11 && y == 10 ? 30u : 0;
		CHECK(image->primary[y * 648 + x] == (ink ? ink : base->primary[y * 648 + x]));
		CHECK(image->alternate[y * 648 + x] == (ink ? ink : base->alternate[y * 648 + x]));
	}
	st_pixel_buffer_destroy(image);
	rip.x_dim = rip.x_max = 640; rip.y_dim = rip.y_max = 350;
	rip.viewport.ex = 639; rip.viewport.ey = 349;
	rip.xor_mode = false;
	/* Duplicate visible RGB values must not merge distinct indexed colours. */
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'a', "0400", &handled) == 0 && handled);
	CHECK(st_pixel_buffer_create(&rip.clipboard, 256, 1, false) == 0);
	CHECK(st_pixel_buffer_create(&row, 256, 1, true) == 0);
	const struct st_pixel_blit row_blit = {.width = 256, .height = 1,
		.destination_x = 10, .destination_y = 50, .scale_x = 1, .scale_y = 1};
	for (unsigned mode = 1; mode <= 4; ++mode) for (unsigned rgb = 0; rgb < 2; ++rgb) {
		for (unsigned at = 0; at < 256; ++at) {
			rip.clipboard->primary[at] = indexes[at / 16];
			row->primary[at] = indexes[at % 16];
			row->alternate[at] = indexes[(at + 3) % 16];
		}
		/* RGB fallback: an unrecognized RGB maps to 0; native extended
		 * default colour 18 is literal 0x202020 before Boolean masking. */
		if (rgb) {
			rip.clipboard->primary[0] = primary;
			rip.clipboard->primary[1] = ST_PIXEL_COLOR_RGB_VALUE(32, 32, 32);
			row->primary[2] = primary;
			row->alternate[3] = ST_PIXEL_COLOR_RGB_VALUE(32, 32, 32);
		}
		CHECK(st_console_blit_pixels(console, base, NULL, &full_blit) == 0);
		CHECK(st_console_blit_pixels(console, row, NULL, &row_blit) == 0);
		char arguments[8];
		snprintf(arguments, sizeof(arguments), "0A1E0%u0", mode);
		CHECK(cterm_rip_raster_command(&rip, 1, 0, 'P', arguments, &handled) == 0 && handled);
		CHECK(rip.drawing_pixels == NULL && rip.drawing_damage == NULL);
		CHECK(st_console_get_pixels(&image, console, &full, false) == 0);
		for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 648; ++x) {
			uint32_t expected[2] = {base->primary[y * 648 + x], base->alternate[y * 648 + x]};
			if (y == 50 && x >= 10 && x < 266) {
				unsigned at = x - 10, src = rgb && at == 0 ? 0 : rgb && at == 1 ? 18 : at / 16;
				unsigned dst[2] = {rgb && at == 2 ? 0 : at % 16, rgb && at == 3 ? 18 : (at + 3) % 16};
				for (unsigned phase = 0; phase < 2; ++phase) {
					unsigned value = mode == 1 ? src ^ dst[phase] : mode == 2 ? src | dst[phase]
					    : mode == 3 ? src & dst[phase] : src ^ 15;
					expected[phase] = indexes[value & 15];
				}
			}
			if (image->primary[y * 648 + x] != expected[0])
				fprintf(stderr, "paste mode %u at %u,%u: %u expected %u\n", mode, x, y,
				    image->primary[y * 648 + x], expected[0]);
			CHECK(image->primary[y * 648 + x] == expected[0]);
			CHECK(image->alternate[y * 648 + x] == expected[1]);
		}
		st_pixel_buffer_destroy(image);
	}
	/* Flood uses a viewport view with the full raster stride. v1.10a's
	 * single-plane publication synchronizes the entire viewport, including
	 * its untouched border. Separate phases outside the viewport survive. */
	CHECK(st_pixel_buffer_fill(base, primary, alternate) == 0);
	for (unsigned y = 7; y <= 12; ++y) for (unsigned x = 9; x <= 15; ++x)
		base->primary[y * 648 + x] = x == 9 || x == 15 || y == 7 || y == 12 ? 22 : 16;
	CHECK(st_console_blit_pixels(console, base, NULL, &full_blit) == 0);
	rip.viewport.sx = 9; rip.viewport.sy = 7; rip.viewport.ex = 15; rip.viewport.ey = 12;
	CHECK(cterm_rip_raster_command(&rip, 0, 0, 'F', "010103", &handled) == 0 && handled);
	CHECK(st_console_get_pixels(&image, console, &full, false) == 0);
	for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 648; ++x) {
		bool painted = x > 9 && x < 15 && y > 7 && y < 12;
		bool inside = x >= 9 && x <= 15 && y >= 7 && y <= 12;
		uint32_t expected = painted ? 17 : base->primary[y * 648 + x];
		CHECK(image->primary[y * 648 + x] == expected);
		CHECK(image->alternate[y * 648 + x] == (inside ? expected : alternate));
	}
	st_pixel_buffer_destroy(image);
	st_pixel_buffer_destroy(base);
	st_pixel_buffer_destroy(row);
	cterm_rip_raster_destroy(&rip);
	st_console_destroy(console);
	return 0;
}

int
st_cterm_rip_raster_test_main(void)
{
	CHECK(test_signed_arguments() == 0);
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

	CHECK(test_in_place_pixels() == 0);
	CHECK(test_offset_viewport_line_clipping() == 0);
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
