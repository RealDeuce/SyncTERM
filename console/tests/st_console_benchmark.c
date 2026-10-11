/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"
#include "st_time.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define COLUMNS 80U
#define ROWS 25U
#define FONT_HEIGHT 16U
#define GLYPHS 256U

static void
fail(const char *operation, int status)
{
	fprintf(stderr, "%s failed: %d\n", operation, status);
	exit(EXIT_FAILURE);
}

static uint64_t
monotonic_ns(void)
{
	uint64_t result;
	int status = st_time_monotonic_ns(&result);

	if (status != 0)
		fail("st_time_monotonic_ns", status);
	return result;
}

static double
milliseconds(uint64_t start, uint64_t end)
{
	return (double)(end - start) / 1000000.0;
}

static uint64_t
checksum(st_console_t *console)
{
	const struct st_console_rect raster = {0, 0, COLUMNS * 8U,
	    ROWS * FONT_HEIGHT + 6U};
	struct st_pixel_buffer *pixels = NULL;
	uint64_t result = 0;
	int status = st_console_get_pixels(&pixels, console, &raster, false);

	if (status != 0)
		fail("st_console_get_pixels", status);
	for (size_t i = 0; i < (size_t)pixels->width * pixels->height; ++i)
		result = result * UINT64_C(33) + pixels->primary[i];
	st_pixel_buffer_destroy(pixels);
	return result;
}

int
main(void)
{
	uint8_t bitmap[GLYPHS * FONT_HEIGHT];
	uint8_t wide_bitmap[2U * 2U * FONT_HEIGHT];
	static const struct st_console_font_mapping wide_mapping[] = {
		{'?', 0},
		{UINT32_C(0x4e00), 1},
		{UINT32_C(0x4e01), 2},
	};
	struct st_console_cell *screen;
	struct st_console_cell line[COLUMNS];
	struct st_console_cell fill;
	struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	struct st_console_config config = {
		.columns = COLUMNS,
		.rows = ROWS,
		.cell_width = 8,
		.cell_height = FONT_HEIGHT,
		.pixel_width = COLUMNS * 8U,
		.pixel_height = ROWS * FONT_HEIGHT + 6U,
	};
	struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = FONT_HEIGHT,
		.glyph_count = GLYPHS,
		.row_stride = 1,
		.glyph_stride = FONT_HEIGHT,
	};
	struct st_console_font wide_font = {
		.bitmap = bitmap,
		.bitmap_size = FONT_HEIGHT,
		.width = 8,
		.height = FONT_HEIGHT,
		.glyph_count = 1,
		.row_stride = 1,
		.glyph_stride = FONT_HEIGHT,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 2,
		.wide_row_stride = 2,
		.wide_glyph_stride = 2U * FONT_HEIGHT,
		.mapping = wide_mapping,
		.mapping_count = sizeof(wide_mapping) / sizeof(wide_mapping[0]),
		.fallback_glyph = 0,
	};
	const struct st_console_rect full = {0, 0, COLUMNS, ROWS};
	const struct st_console_rect last = {0, ROWS - 1, COLUMNS, 1};
	const struct st_console_rect terminal_scroll = {0, 1, COLUMNS,
	    ROWS - 2};
	const struct st_console_rect terminal_last = {0, ROWS - 2,
	    COLUMNS, 1};
	st_console_t *console = NULL;
	uint64_t start;
	uint64_t end;
	const uint32_t raster_passes = 200;
	const uint32_t scroll_passes = 2000;
	double update_milliseconds;
	uint16_t font_identifier;
	uint16_t wide_font_identifier;
	int status;

	for (uint32_t glyph = 0; glyph < GLYPHS; ++glyph)
		for (uint32_t y = 0; y < FONT_HEIGHT; ++y)
			bitmap[(size_t)glyph * FONT_HEIGHT + y]
			    = (uint8_t)(glyph * 29U + y * 17U);
	for (size_t i = 0; i < sizeof(wide_bitmap); ++i)
		wide_bitmap[i] = (uint8_t)(i * 37U + 11U);
	screen = malloc((size_t)COLUMNS * ROWS * sizeof(screen[0]));
	if (screen == NULL)
		fail("malloc", -1);
	for (size_t i = 0; i < (size_t)COLUMNS * ROWS; ++i) {
		screen[i] = (struct st_console_cell){
			.glyph = (uint32_t)(i & 255U),
			.foreground = 15,
			.background = 0,
			.font = 1,
		};
	}
	status = st_console_create(&console, &config_display, &config);
	if (status != 0)
		fail("st_console_create", status);
	status = st_console_register_static_font(console, &font,
	    &font_identifier);
	if (status != 0)
		fail("st_console_register_static_font", status);
	for (size_t i = 0; i < (size_t)COLUMNS * ROWS; ++i) {
		screen[i].font = font_identifier;
		screen[i].glyph_font = font_identifier;
	}
	status = st_console_begin_update(console);
	if (status != 0)
		fail("st_console_begin_update", status);
	start = monotonic_ns();
	for (uint32_t pass = 0; pass < raster_passes; ++pass) {
		for (size_t i = 0; i < (size_t)COLUMNS * ROWS; ++i)
			screen[i].glyph = (uint32_t)((i + pass) & 255U);
		status = st_console_write_cells(console, &full, screen, COLUMNS);
		if (status != 0)
			fail("cell update", status);
	}
	end = monotonic_ns();
	update_milliseconds = milliseconds(start, end) / raster_passes;
	status = st_console_end_update(console);
	if (status != 0)
		fail("st_console_end_update", status);
	printf("cell update: %.3f ms for %u 80x25 updates "
	    "(%.3f ms/frame)\n", update_milliseconds * raster_passes,
	    raster_passes, update_milliseconds);

	start = monotonic_ns();
	for (uint32_t pass = 0; pass < raster_passes; ++pass) {
		for (size_t i = 0; i < (size_t)COLUMNS * ROWS; ++i)
			screen[i].glyph = (uint32_t)((i + pass) & 255U);
		status = st_console_write_cells(console, &full, screen, COLUMNS);
		if (status != 0)
			fail("full raster", status);
	}
	end = monotonic_ns();
	printf("font: %.3f ms for %u 80x25 rasters (%.3f ms/frame)\n",
	    milliseconds(start, end), raster_passes,
	    milliseconds(start, end) / raster_passes);

	status = st_console_register_static_font(console, &wide_font,
	    &wide_font_identifier);
	if (status != 0)
		fail("wide font registration", status);
	for (size_t i = 0; i < (size_t)COLUMNS * ROWS; i += 2) {
		screen[i] = (struct st_console_cell){
			.glyph = 1,
			.foreground = 15,
			.background = 0,
			.font = wide_font_identifier,
			.glyph_font = wide_font_identifier,
		};
		screen[i + 1] = screen[i];
		screen[i + 1].flags = ST_CONSOLE_CELL_CONTINUATION;
	}
	start = monotonic_ns();
	for (uint32_t pass = 0; pass < raster_passes; ++pass) {
		uint32_t glyph = (pass & 1U) == 0 ? 1 : 2;

		for (size_t i = 0; i < (size_t)COLUMNS * ROWS; ++i)
			screen[i].glyph = glyph;
		status = st_console_write_cells(console, &full, screen, COLUMNS);
		if (status != 0)
			fail("wide font raster", status);
	}
	end = monotonic_ns();
	printf("wide font: %.3f ms for %u 80x25 rasters "
	    "(%.3f ms/frame)\n", milliseconds(start, end), raster_passes,
	    milliseconds(start, end) / raster_passes);

	for (size_t i = 0; i < (size_t)COLUMNS * ROWS; ++i) {
		screen[i] = (struct st_console_cell){
			.glyph = (uint32_t)(i & 255U),
			.foreground = 15,
			.background = 0,
			.font = font_identifier,
			.glyph_font = font_identifier,
		};
	}
	fill = (struct st_console_cell){
		.glyph = ' ',
		.foreground = 15,
		.background = 0,
		.font = font_identifier,
		.glyph_font = font_identifier,
	};

	for (uint32_t x = 0; x < COLUMNS; ++x)
		line[x] = screen[x];
	start = monotonic_ns();
	for (uint32_t pass = 0; pass < scroll_passes; ++pass) {
		for (uint32_t x = 0; x < COLUMNS; ++x)
			line[x].glyph = (pass + x) & 255U;
		status = st_console_begin_update(console);
		if (status == 0)
			status = st_console_scroll(console, 1, ' ');
		if (status == 0)
			status = st_console_write_cells(console, &last, line, COLUMNS);
		if (status == 0)
			status = st_console_end_update(console);
		if (status != 0)
			fail("scroll", status);
	}
	end = monotonic_ns();
	printf("scroll: %.3f ms for %u scroll-and-render operations "
	    "(%.3f us/line)\n", milliseconds(start, end), scroll_passes,
	    milliseconds(start, end) * 1000.0 / scroll_passes);

	start = monotonic_ns();
	for (uint32_t pass = 0; pass < scroll_passes; ++pass) {
		for (uint32_t x = 0; x < COLUMNS; ++x)
			line[x].glyph = (pass + x) & 255U;
		status = st_console_begin_update(console);
		if (status == 0)
			status = st_console_move_cells_clear(console, &terminal_scroll,
			    0, 0, &fill);
		if (status == 0)
			status = st_console_write_cells(console, &terminal_last, line,
			    COLUMNS);
		if (status == 0)
			status = st_console_end_update(console);
		if (status != 0)
			fail("rectangular scroll", status);
	}
	end = monotonic_ns();
	printf("rectangular scroll: %.3f ms for %u move-and-render operations "
	    "(%.3f us/line)\n", milliseconds(start, end), scroll_passes,
	    milliseconds(start, end) * 1000.0 / scroll_passes);
	printf("checksum: %" PRIu64 "\n", checksum(console));

	st_console_destroy(console);
	free(screen);
	return EXIT_SUCCESS;
}
