/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_fill.h"
#include "st_rip3_ports.h"
#include "io_error.h"
#include "st_pixels.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
expect(struct st_rip3_ports *ports, unsigned number, const uint8_t *pixels,
    int32_t width, int32_t height)
{
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, number, x, y, &value) == 0);
			if (value != pixels[y * width + x]) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n",
				    x, y, value, pixels[y * width + x]);
				return 1;
			}
		}
	}
	return 0;
}

static int
rectangles(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	struct st_rip3_pen border;
	struct st_rip3_rect bounds = {0, 0, 5, 5};
	struct st_rip3_rect area = {4, 4, 1, 1};
	const uint8_t bar[] = {
		2,2,2,2,2,2, 2,7,7,7,7,2, 2,7,7,7,7,2,
		2,7,7,7,7,2, 2,7,7,7,7,2, 2,2,2,2,2,2,
	};
	const uint8_t half_open[] = {
		2,2,2,2,2,2, 2,7,7,7,2,2, 2,7,7,7,2,2,
		2,7,7,7,2,2, 2,2,2,2,2,2, 2,2,2,2,2,2,
	};
	const uint8_t bordered[] = {
		2,2,2,2,2,2, 2,9,9,9,9,2, 2,9,7,7,9,2,
		2,9,7,7,9,2, 2,9,9,9,9,2, 2,2,2,2,2,2,
	};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_pen_set(&border, true, 4, 0xaaaa, 1) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_draw_bar(ports, &area, &fill, 5, 4, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, 1, bar, 6, 6) == 0);
	CHECK(area.x0 == 4 && area.y0 == 4 && area.x1 == 1 && area.y1 == 1);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_draw_filled_rectangle(ports, &area, &fill, 5, 4, NULL, 9,
	    ST_RIP3_XOR) == 0);
	CHECK(expect(ports, 1, half_open, 6, 6) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_draw_filled_rectangle(ports, &area, &fill, 5, 4, &border, 9,
	    ST_RIP3_XOR) == 0);
	CHECK(expect(ports, 1, bordered, 6, 6) == 0);
	/* Fill, then COPY border: even NOT mode cannot invert the border. */
	CHECK(st_rip3_ports_erase(ports, 248) == 0);
	CHECK(st_rip3_draw_filled_rectangle(ports, &area, &fill, 5, 4, &border, 9,
	    ST_RIP3_NOT) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_read(ports, 1, 1, 1, &value) == 0 && value == 9);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	area = (struct st_rip3_rect){2, 4, 2, 1};
	CHECK(st_rip3_draw_filled_rectangle(ports, &area, &fill, 5, 4, NULL, 9,
	    ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &value) == 0 && value == 2);
	CHECK(st_rip3_draw_filled_rectangle(ports, &area, &fill, 5, 4, &border, 9,
	    ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x == 2 && y >= 1 && y <= 4 ? 9 : 2));
		}
	}
	/* BAR still draws a degenerate inclusive column. */
	CHECK(st_rip3_draw_bar(ports, &area, &fill, 5, 4, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &value) == 0 && value == 5);
	CHECK(st_rip3_fill_solid(&fill, false) == 0);
	CHECK(st_rip3_draw_bar(ports, &bounds, &fill, 15, 6, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 6);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
patterns(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	struct st_rip3_point origin = {1, 2};
	unsigned rows[] = {128,64,32,16,8,4,2,1};
	struct st_rip3_rect bounds = {0, 0, 15, 7};
	struct st_rip3_rect left = {-10, -10, 7, 20};
	struct st_rip3_rect right = {8, -10, 30, 20};
	const uint16_t expected[] = {0x0101,0x8080,0x4040,0x2020,
	    0x1010,0x0808,0x0404,0x0202};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_custom(&fill, rows, &origin) == 0);
	CHECK(st_rip3_draw_bar(ports, &left, &fill, 7, 2, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_bar(ports, &right, &fill, 7, 2, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 16; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (expected[y] & (0x8000 >> x) ? 7 : 2));
		}
	}
	/* The two fills share one explicit phase across the common edge. */
	CHECK(st_rip3_draw_bar(ports, &bounds, &fill, 7, 2, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 16; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 0);
	}
	rows[0] = 1;
	rows[7] = 256;
	CHECK(st_rip3_fill_custom(&fill, rows, &origin) == IO_ERROR_OUT_OF_RANGE);
	CHECK(fill.origin.x == 1 && fill.origin.y == 2);
	CHECK(fill.rows[0] == 128 && fill.rows[7] == 1);
	rows[0] = 128;
	rows[7] = 1;
	origin = (struct st_rip3_point){INT32_MIN, INT32_MAX};
	CHECK(st_rip3_fill_custom(&fill, rows, &origin) == 0);
	CHECK(st_rip3_draw_bar(ports, &bounds, &fill, 7, 2, ST_RIP3_COPY) == 0);
	for (int32_t x = 0; x < 16; x++) {
		CHECK(st_rip3_ports_read(ports, 1, x, 0, &value) == 0);
		CHECK(value == (x == 1 || x == 9 ? 7 : 2));
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	struct st_rip3_pen invalid_border;
	struct st_rip3_rect bounds = {100, 200, 107, 207};
	struct st_rip3_rect view = {1, 1, 6, 6};
	struct st_rip3_rect extremes = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_draw_filled_rectangle(ports, &extremes, &fill, 3, 4,
	    NULL, 9, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 768; y++) {
		for (uint32_t x = 0; x < 1024; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 101 && x <= 106 && y >= 201 && y <= 206)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0, 170, 170);
			CHECK(capture->primary[(size_t)y * 1024 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	CHECK(st_rip3_pen_init(&invalid_border) == 0);
	invalid_border.thickness = 0;
	CHECK(st_rip3_draw_filled_rectangle(ports, &extremes, &fill, 7, 4,
	    &invalid_border, 9, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_bar(ports, NULL, &fill, 7, 4, ST_RIP3_COPY)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_bar(ports, &extremes, &fill, 7, 4,
	    (enum st_rip3_write_mode)-1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_draw_bar(ports, &extremes, &fill, 7, 4, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 1 && x <= 6 && y >= 1 && y <= 6 ? 3 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(rectangles() == 0);
	CHECK(patterns() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
