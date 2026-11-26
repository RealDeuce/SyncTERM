/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_ellipse.h"
#include "st_rip3_ports.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
expect(struct st_rip3_ports *ports, const uint8_t expected[64])
{
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			if (value != expected[y * 8 + x]) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n", x, y, value,
				    expected[y * 8 + x]);
				return 1;
			}
		}
	}
	return 0;
}

static int
shapes(void)
{
	struct st_rip3_ports     *ports = NULL;
	struct st_rip3_ellipse    ellipse;
	struct st_rip3_fill       fill;
	const struct st_rip3_rect bounds   = {0, 0, 7, 7};
	const uint8_t             circle[] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};
	const uint8_t rotated[] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};
	/* Integer conic oracle for this general basis, at half-cell X/Y:
	 * 5*(2dx)^2 - 12*(2dx)*(2dy) + 20*(2dy)^2 < 256. */
	const struct st_rip3_ellipse affine            = {4, 4, 2, -4, -1, -2};
	const uint8_t                affine_expected[] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 2, 2, 0) == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 2, ST_RIP3_COPY)
	    == 0);
	CHECK(expect(ports, circle) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 4, 4, 3, 1, 45) == 0);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_CHORD, &fill, 7, 2,
		  ST_RIP3_COPY)
	      == 0);
	CHECK(expect(ports, rotated) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_ellipse_interior(ports, &affine, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 2, ST_RIP3_COPY)
	      == 0);
	CHECK(expect(ports, affine_expected) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 4, 4, 3, 1, 90) == 0);
	CHECK(ellipse.xx == 0 && ellipse.xy == -1 && ellipse.yx == -3 && ellipse.yy == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 4, 4, 3, 1, 450) == 0);
	CHECK(ellipse.xx == 0 && ellipse.xy == -1 && ellipse.yx == -3 && ellipse.yy == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
sectors(void)
{
	struct st_rip3_ports     *ports = NULL;
	struct st_rip3_ellipse    ellipse;
	struct st_rip3_fill       fill;
	const struct st_rip3_rect bounds    = {0, 0, 7, 7};
	const uint8_t             quarter[] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};
	const uint8_t full[] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};
	const uint8_t wrap[] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    7,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 2, 2, 0) == 0);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 90, ST_RIP3_ELLIPSE_PIE, &fill, 7, 2, ST_RIP3_XOR)
	      == 0);
	CHECK(expect(ports, quarter) == 0);
	for (unsigned start = 90; start < 360; start += 90)
		CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, start, 90, ST_RIP3_ELLIPSE_PIE, &fill, 7, 2,
			  ST_RIP3_XOR)
		      == 0);
	CHECK(expect(ports, full) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 270, 180, ST_RIP3_ELLIPSE_PIE, &fill, 7, 2,
		  ST_RIP3_COPY)
	      == 0);
	CHECK(expect(ports, wrap) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 4, 4, 3, 3, 0) == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 90, ST_RIP3_ELLIPSE_CHORD, &fill, 7, 2, ST_RIP3_XOR)
	    == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == ((x == 5 && y == 1) || (x == 6 && y == 2) ? 7 : 0));
		}
	}
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 90, 270, ST_RIP3_ELLIPSE_CHORD, &fill, 7, 2,
		  ST_RIP3_XOR)
	      == 0);
	/* Complementary caps cover the radius-3 disk once, including chord ties. */
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			bool inside = (y == 1 || y == 6) ? x >= 2 && x <= 5 : y >= 2 && y <= 5 && x >= 1 && x <= 6;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (inside ? 7 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
patterns(void)
{
	struct st_rip3_ports      *ports = NULL;
	struct st_rip3_ellipse     ellipse;
	struct st_rip3_fill        fill;
	const struct st_rip3_rect  bounds    = {0, 0, 7, 7};
	const struct st_rip3_point origin    = {0, 0};
	const unsigned             checker[] = {170, 85, 170, 85, 170, 85, 170, 85};
	uint8_t                    value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_custom(&fill, checker, &origin) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 2, 2, 0) == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	    == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			bool inside =
			    (y == 1 || y == 4) ? x == 2 || x == 3 : (y == 2 || y == 3) && x >= 1 && x <= 4;
			unsigned expected = inside ? ((x + y) % 2 == 0 ? 7 : 4) : 0;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
		}
	}
	CHECK(st_rip3_ports_erase(ports, 15) == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_CHORD, &fill, 7, 4, ST_RIP3_NOT)
	    == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			bool inside =
			    (y == 1 || y == 4) ? x == 2 || x == 3 : (y == 2 || y == 3) && x >= 1 && x <= 4;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (inside ? 240 : 15));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports        *ports = NULL;
	struct st_rip3_ellipse       ellipse;
	struct st_rip3_ellipse       empty;
	struct st_rip3_ellipse       invalid;
	struct st_rip3_fill          fill;
	const struct st_rip3_rect    bounds   = {10, 20, 17, 27};
	const struct st_rip3_rect    view     = {1, 1, 6, 6};
	const struct st_rip3_ellipse singular = {3, 3, 2, 4, 1, 2};
	struct st_pixel_buffer      *capture  = NULL;
	struct st_rip3_palette       palette;
	uint8_t                      value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, INT32_MAX, INT32_MAX, 0) == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 3, 4, ST_RIP3_COPY)
	    == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 768; y++) {
		for (uint32_t x = 0; x < 1024; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 11 && x <= 16 && y >= 21 && y <= 26)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0, 170, 170);
			CHECK(capture->primary[(size_t)y * 1024 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 2, 2, 0) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 7, 8, -1, 3, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ellipse_axes(&ellipse, NAN, 3, 2, 2, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 2, 2, HUGE_VAL) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ellipse_axes(&ellipse, 1024.0 * INT32_MAX + 1, 3, 2, 2, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 1024.0 * INT32_MAX + 1, 2, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(ellipse.center_x == 3 && ellipse.center_y == 3 && ellipse.xx == 2 && ellipse.xy == 0
	      && ellipse.yx == 0 && ellipse.yy == -2);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, NAN, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4,
		  ST_RIP3_COPY)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 0, -1, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 361, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, (enum st_rip3_ellipse_region)2, &fill, 7, 4,
		  ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_PIE, NULL, 7, 4, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	invalid    = ellipse;
	invalid.xx = HUGE_VAL;
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &invalid, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ellipse_axes(&empty, 1024.0 * INT32_MAX, -1024.0 * INT32_MAX, 1024.0 * INT32_MAX,
		  1024.0 * INT32_MAX, 0)
	      == 0);
	CHECK(empty.center_x == 1024.0 * INT32_MAX && empty.center_y == -1024.0 * INT32_MAX
	      && empty.xx == 1024.0 * INT32_MAX && empty.yy == -1024.0 * INT32_MAX);
	CHECK(st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 0, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	      == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &singular, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	    == 0);
	CHECK(st_rip3_ellipse_axes(&empty, 3, 3, 0, 2, 90) == 0);
	CHECK(st_rip3_draw_ellipse_interior(ports, &empty, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	      == 0);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(
	    st_rip3_draw_ellipse_interior(ports, &ellipse, 0, 360, ST_RIP3_ELLIPSE_PIE, &fill, 7, 4, ST_RIP3_COPY)
	    == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 1 && x <= 6 && y >= 1 && y <= 6 ? 5 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(shapes() == 0);
	CHECK(sectors() == 0);
	CHECK(patterns() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
