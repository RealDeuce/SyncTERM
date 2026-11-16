/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ellipse.h"
#include "st_rip3_ports.h"
#include "io_error.h"
#include "st_pixels.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* Radius-three circle sampled at integer pixel addresses: its unit-width
 * annulus has radial distance in [2.5,3.5]. These rows are independent of the
 * renderer's parametric flattening. Quarter arcs retain their round end caps. */
static const char *const circle[] = {
	"..###...", ".#...#..", "#.....#.", "#.....#.",
	"#.....#.", ".#...#..", "..###...", "........",
};
static const char *const arc[] = {
	"...##...", ".....#..", "......#.", "......#.",
	"........", "........", "........", "........",
};
static const char *const pie[] = {
	"...##...", "...#.#..", "...#..#.", "...####.",
	"........", "........", "........", "........",
};
static const char *const chord[] = {
	"...##...", "....##..", ".....##.", "......#.",
	"........", "........", "........", "........",
};

static int
expect(struct st_rip3_ports *ports, const char *const rows[8], uint8_t on, uint8_t off)
{
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;
			uint8_t expected = rows[y][x] == '#' ? on : off;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			if (value != expected) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n", x, y, value, expected);
				return 1;
			}
		}
	}
	return 0;
}

static int
outlines(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_ellipse ellipse;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const char *const *rows[] = {pie, chord, arc};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 3, 3, 0) == 0);
	for (unsigned region = 0; region < 3; region++) {
		CHECK(st_rip3_ports_erase(ports, 0) == 0);
		CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 90,
		    (enum st_rip3_ellipse_region)region, 1, 7, ST_RIP3_XOR) == 0);
		CHECK(expect(ports, rows[region], 7, 0) == 0);
		CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 90,
		    (enum st_rip3_ellipse_region)region, 1, 7, ST_RIP3_XOR) == 0);
		CHECK(expect(ports, rows[region], 0, 0) == 0);
		/* A full turn has no internal chord or radii, for any region type. */
		CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
		    (enum st_rip3_ellipse_region)region, 1, 7, ST_RIP3_XOR) == 0);
		CHECK(expect(ports, circle, 7, 0) == 0);
	}
	/* A wrap at zero is one continuous right semicircle. */
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 270, 180,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 3 && circle[y][x] == '#' ? 7 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
filled(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_ellipse ellipse;
	struct st_rip3_fill fill;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_point origin = {0,0};
	const unsigned checker[] = {170,85,170,85,170,85,170,85};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 3, 3, 0) == 0);
	CHECK(st_rip3_fill_custom(&fill, checker, &origin) == 0);
	for (unsigned border = 0; border <= 1; border++) {
		for (unsigned mode = ST_RIP3_COPY; mode <= ST_RIP3_NOT; mode++) {
			CHECK(st_rip3_ports_erase(ports, 9) == 0);
			CHECK(st_rip3_draw_filled_ellipse(ports, &ellipse, 0, 360,
			    ST_RIP3_ELLIPSE_PIE, &fill, 7, 2, border, 5,
			    (enum st_rip3_write_mode)mode) == 0);
			for (int32_t y = 0; y < 8; y++) {
				for (int32_t x = 0; x < 8; x++) {
					int dx = 2 * x + 1 - 6, dy = 2 * y + 1 - 6;
					uint8_t source = (x + y) % 2 == 0 ? 7 : 2;
					uint8_t expected = 9, value;

					if (dx * dx + dy * dy < 36) {
						const uint8_t operations[] = {source, 9 ^ source,
						    9 | source, 9 & source, 246};

						expected = operations[mode];
					}
					if (border && circle[y][x] == '#')
						expected = 5;
					CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
					CHECK(value == expected);
				}
			}
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
degenerate(void)
{
	struct st_rip3_ports *ports = NULL;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_ellipse diagonal = {3,3,2,2,-2,-2};
	struct st_rip3_ellipse ellipse;
	const char *const line[] = {
		"......#.", ".....#..", "....#...", "...#....",
		"..#.....", ".#......", "#.......", "........",
	};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &diagonal, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, line, 7, 0) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 0, 0, 0) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_CHORD, 3, 7, ST_RIP3_XOR) == 0);
	/* A zero-radius curve is a round pen dot, applied once. */
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 2 && x <= 4 && y >= 2 && y <= 4 ? 7 : 0));
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
transforms_and_width(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_ellipse ellipse;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_ellipse affine = {3,3,2,1,-1,-2};
	const struct st_rip3_point endpoints[] = {{5,2},{4,1},{1,4},{2,5}};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 3, 1, 90) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 3, 0, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_read(ports, 1, 4, 3, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_read(ports, 1, 3, 3, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &affine, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_XOR) == 0);
	for (size_t i = 0; i < sizeof(endpoints) / sizeof(*endpoints); i++)
		CHECK(st_rip3_ports_read(ports, 1, endpoints[i].x, endpoints[i].y,
		    &value) == 0 && value == 7);
	CHECK(st_rip3_ports_read(ports, 1, 3, 3, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_read(ports, 1, 7, 7, &value) == 0 && value == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 3, 3, 0) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, 3, 7, ST_RIP3_XOR) == 0);
	/* Cross-section of the width-three annulus, away from flatness ties. */
	for (int32_t x = 0; x < 8; x++) {
		CHECK(st_rip3_ports_read(ports, 1, x, 3, &value) == 0);
		CHECK(value == (x <= 1 || x >= 5 ? 7 : 0));
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 90, 1e-20,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 3, 0, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_read(ports, 1, 3, 3, &value) == 0 && value == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_ellipse ellipse, invalid;
	struct st_rip3_fill fill;
	const struct st_rip3_rect bounds = {10,20,17,27};
	const struct st_rip3_rect view = {1,1,6,6};
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	/* Only a tiny portion of this two-billion-pixel circle is near the
	 * viewport. It must be clipped before generating a polygonal path. */
	CHECK(st_rip3_ellipse_axes(&ellipse, 3 - INT32_MAX, 3,
	    INT32_MAX, INT32_MAX, 0) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, -180, 360,
	    ST_RIP3_ELLIPSE_ARC, 1, 3, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 768; y++) {
		for (uint32_t x = 0; x < 1024; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x == 14 && y >= 21 && y <= 26)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0,170,170);
			CHECK(capture->primary[(size_t)y * 1024 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	CHECK(st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_ellipse_axes(&ellipse, 3, 3, 2, 2, 0) == 0);
	invalid = ellipse;
	invalid.xy = NAN;
	CHECK(st_rip3_draw_ellipse_outline(ports, &invalid, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, 0, 7, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, UINT32_MAX, 7, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, HUGE_VAL, 360,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, -1,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 361,
	    ST_RIP3_ELLIPSE_ARC, 1, 7, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    (enum st_rip3_ellipse_region)3, 1, 7, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_filled_ellipse(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_PIE, &fill, 7, 2, UINT32_MAX, 3,
	    ST_RIP3_XOR) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_filled_ellipse(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_ARC, &fill, 7, 2, 1, 3,
	    ST_RIP3_XOR) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_filled_ellipse(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_PIE, NULL, 7, 2, 1, 3,
	    ST_RIP3_XOR) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 0,
	    ST_RIP3_ELLIPSE_PIE, 1, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_draw_ellipse_outline(ports, &ellipse, 0, 360,
	    ST_RIP3_ELLIPSE_PIE, 1, 7, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;

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
	CHECK(outlines() == 0);
	CHECK(filled() == 0);
	CHECK(degenerate() == 0);
	CHECK(transforms_and_width() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
