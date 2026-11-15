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
expect(struct st_rip3_ports *ports, const uint8_t expected[64])
{
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			if (value != expected[y * 8 + x]) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n",
				    x, y, value, expected[y * 8 + x]);
				return 1;
			}
		}
	}
	return 0;
}

static int
holes(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_rect under_hole = {2,2,4,4};
	const struct st_rip3_point outer[] = {{1,1},{6,1},{6,6},{1,6}};
	const struct st_rip3_point hole[] = {{2,2},{5,2},{5,5},{2,5}};
	const struct st_rip3_point island[] = {{3,3},{4,3},{4,4},{3,4}};
	const struct st_rip3_contour contours[] = {{outer,4},{hole,4},{island,4}};
	struct st_rip3_point reverse[3][4];
	struct st_rip3_contour reversed[3];
	const uint8_t expected[] = {
		2,2,2,2,2,2,2,2, 2,7,7,7,7,7,2,2,
		2,7,4,4,4,7,2,2, 2,7,4,7,4,7,2,2,
		2,7,4,4,4,7,2,2, 2,7,7,7,7,7,2,2,
		2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,
	};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_ports_fill(ports, &under_hole, 4, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, contours, 3, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	CHECK(expect(ports, expected) == 0);
	for (size_t c = 0; c < 3; c++) {
		for (size_t i = 0; i < 4; i++)
			reverse[c][i] = contours[c].points[3 - i];
		reversed[c] = (struct st_rip3_contour){reverse[c],4};
	}
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_ports_fill(ports, &under_hole, 4, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, reversed, 3, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	CHECK(expect(ports, expected) == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
parity(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_point a[] = {{1,1},{5,1},{1,5}};
	const struct st_rip3_point b[] = {{5,1},{5,5},{1,5}};
	const struct st_rip3_point twice[] = {
		{1,1},{5,1},{5,5},{1,5},{1,1},{5,1},{5,5},{1,5},
	};
	/* The subdivided/reversed triangle is geometrically identical to a,
	 * with one collinear extra point on each side. */
	const struct st_rip3_point split_a[] = {{1,1},{1,3},{1,5},{3,3},{5,1},{3,1}};
	const struct st_rip3_contour pair[] = {{a,3},{b,3}};
	const struct st_rip3_contour duplicates[] = {{a,3},{split_a,6}};
	const struct st_rip3_contour double_loop = {twice,8};
	const struct st_rip3_point third[] = {{1,1},{3,1},{1,7}};
	const struct st_rip3_point split_third[] = {{1,1},{1,4},{1,7},{2,4},{3,1},{2,1}};
	const struct st_rip3_contour thirds[] = {{third,3},{split_third,6}};
	const uint8_t third_expected[] = {
		0,0,0,0,0,0,0,0, 0,7,7,0,0,0,0,0,
		0,7,0,0,0,0,0,0, 0,7,0,0,0,0,0,0,
		0,7,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	};
	const uint8_t square[] = {
		0,0,0,0,0,0,0,0, 0,7,7,7,7,0,0,0,
		0,7,7,7,7,0,0,0, 0,7,7,7,7,0,0,0,
		0,7,7,7,7,0,0,0, 0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	/* Adjacent triangles cover each center once, including their shared edge. */
	CHECK(st_rip3_draw_filled_polygons(ports, &pair[0], 1, &fill, 7, 9,
	    NULL, 0, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, &pair[1], 1, &fill, 7, 9,
	    NULL, 0, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, square) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, pair, 2, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	CHECK(expect(ports, square) == 0);
	CHECK(st_rip3_ports_erase(ports, 4) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, duplicates, 2, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 4);
	}
	CHECK(st_rip3_draw_filled_polygons(ports, &double_loop, 1, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 4);
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, thirds, 1, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	CHECK(expect(ports, third_expected) == 0);
	CHECK(st_rip3_ports_erase(ports, 4) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, thirds, 2, &fill, 7, 9,
	    NULL, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 4);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
patterns_and_borders(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	struct st_rip3_pen border;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_point points[] = {{1,1},{5,1},{5,5},{1,5}};
	const struct st_rip3_contour contour = {points,4};
	const unsigned checker[] = {170,85,170,85,170,85,170,85};
	const struct st_rip3_point origin = {0,0};
	const uint8_t bordered[] = {
		2,2,2,2,2,2,2,2, 2,9,7,9,7,9,2,2,
		2,7,7,7,7,2,2,2, 2,9,7,7,7,9,2,2,
		2,7,7,7,7,2,2,2, 2,9,2,9,2,9,2,2,
		2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,
	};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_pen_set(&border, false, 4, 0xaaaa, 1) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 5, 4,
	    &border, 9, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, bordered) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	border.draw_background = true;
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 5, 4,
	    &border, 9, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 1, &value) == 0 && value == 4);
	CHECK(st_rip3_ports_read(ports, 1, 1, 1, &value) == 0 && value == 9);
	CHECK(st_rip3_ports_read(ports, 1, 3, 3, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_fill_custom(&fill, checker, &origin) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			unsigned expected = 2;

			if (x >= 1 && x < 5 && y >= 1 && y < 5)
				expected = (x + y) % 2 == 0 ? 7 : 4;
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	struct st_rip3_pen border;
	const struct st_rip3_rect bounds = {10,20,17,27};
	const struct st_rip3_rect view = {1,1,6,6};
	const struct st_rip3_rect zero = {0,0,0,0};
	const struct st_rip3_rect offscreen = {0,0,7,7};
	const struct st_rip3_point enclosing[] = {
		{INT32_MIN,INT32_MIN}, {INT32_MAX,INT32_MIN},
		{INT32_MAX,INT32_MAX}, {INT32_MIN,INT32_MAX},
	};
	const struct st_rip3_contour contour = {enclosing,4};
	struct st_rip3_contour invalid = {enclosing,4097};
	struct st_rip3_point many[4096];
	struct st_rip3_contour many_contours[4096];
	struct st_rip3_contour many_vertices = {many,4096};
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_pen_init(&border) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 3, 4,
	    &border, 9, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 768; y++) {
		for (uint32_t x = 0; x < 1024; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 11 && x <= 16 && y >= 21 && y <= 26)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0,170,170);
			CHECK(capture->primary[(size_t)y * 1024 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	CHECK(st_rip3_draw_filled_polygons(ports, &invalid, 1, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	invalid.count = 0;
	CHECK(st_rip3_draw_filled_polygons(ports, &invalid, 1, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	invalid = (struct st_rip3_contour){NULL,4};
	CHECK(st_rip3_draw_filled_polygons(ports, &invalid, 1, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 4097, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 0, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	border.pattern = 0;
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 7, 4,
	    &border, 9, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_pen_init(&border) == 0);
	border.thickness = 0;
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 7, 4,
	    &border, 9, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_viewport(ports, &zero) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, &contour, 1, &fill, 7, 4,
	    NULL, 9, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 1 && x <= 6 && y >= 1 && y <= 6 ? 3 : 0));
		}
	}
	CHECK(st_rip3_ports_define(ports, 2, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_pen_init(&border) == 0);
	for (size_t i = 0; i < 4096; i++) {
		many[i] = (struct st_rip3_point){2,2};
		many_contours[i] = (struct st_rip3_contour){many,1};
	}
	CHECK(st_rip3_draw_filled_polygons(ports, &many_vertices, 1, &fill, 7, 4,
	    &border, 9, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 2, 2, &value) == 0 && value == 9);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_filled_polygons(ports, many_contours, 4096, &fill, 7, 4,
	    &border, 9, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 2, x, y, &value) == 0
			    && value == (x == 2 && y == 2 ? 9 : 0));
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(holes() == 0);
	CHECK(parity() == 0);
	CHECK(patterns_and_borders() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
