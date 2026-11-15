/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bezier.h"
#include "st_rip3_ports.h"
#include "io_error.h"
#include "st_pixels.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
samples(void)
{
	const struct st_rip3_point start = {0,0};
	const struct st_rip3_bezier_segment curve = {
		ST_RIP3_BEZIER_JOINTED, {{0,8},{8,8},{8,0}},
	};
	/* Exact Bernstein values at quarters: (0,0), (5/4,9/2), (4,6),
	 * (27/4,9/2), (8,0), rounded using the explicit kernel policy. */
	const struct st_rip3_point expected[] = {{0,0},{1,5},{4,6},{7,5},{8,0}};
	const struct st_rip3_point negative_expected[] = {{-8,-8},{-7,-3},{-4,-2},{-1,-3},{0,-8}};
	const struct st_rip3_point negative = {-8,-8};
	const struct st_rip3_bezier_segment negative_curve = {
		ST_RIP3_BEZIER_SMOOTH, {{-8,0},{0,0},{0,-8}},
	};
	const struct st_rip3_point extreme = {INT32_MIN,INT32_MAX};
	const struct st_rip3_bezier_segment extreme_curve = {
		ST_RIP3_BEZIER_JOINTED,
		{{INT32_MIN,INT32_MAX},{INT32_MAX,INT32_MIN},{INT32_MAX,INT32_MIN}},
	};
	struct st_rip3_bezier_path path = {0};

	CHECK(st_rip3_bezier_build(&path, &start, &curve, 1, 4) == 0);
	CHECK(path.count == 5);
	for (size_t i = 0; i < path.count; i++)
		CHECK(path.points[i].x == expected[i].x && path.points[i].y == expected[i].y);
	st_rip3_bezier_free(&path);
	CHECK(path.points == NULL && path.count == 0);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_build(&path, &negative, &negative_curve, 1, 4) == 0);
	for (size_t i = 0; i < path.count; i++)
		CHECK(path.points[i].x == negative_expected[i].x && path.points[i].y == negative_expected[i].y);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_build(&path, &extreme, &extreme_curve, 1, 2) == 0);
	CHECK(path.count == 3 && path.points[1].x == 0 && path.points[1].y == 0);
	CHECK(path.points[0].x == INT32_MIN && path.points[2].x == INT32_MAX);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_build(&path, &start, &curve, 1, 1) == 0);
	CHECK(path.count == 2 && path.points[0].x == 0 && path.points[1].x == 8);
	st_rip3_bezier_free(&path);
	CHECK(st_rip3_bezier_build(&path, &start, &curve, 1, 4095) == 0);
	CHECK(path.count == 4096 && path.points[4095].x == 8 && path.points[4095].y == 0);
	for (size_t i = 0; i < path.count; i++)
		CHECK(path.points[i].x >= 0 && path.points[i].x <= 8
		    && path.points[i].y >= 0 && path.points[i].y <= 6);
	st_rip3_bezier_free(&path);
	return 0;
}

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
	struct st_rip3_pen pen;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_point start = {1,1};
	const struct st_rip3_bezier_segment curve = {
		ST_RIP3_BEZIER_JOINTED, {{1,5},{5,5},{5,1}},
	};
	const char *const open[] = {
		"........", ".#...#..", "..#.#...", "..#.#...",
		"...#....", "........", "........", "........",
	};
	const char *const closed[] = {
		"........", ".#####..", "..#.#...", "..#.#...",
		"...#....", "........", "........", "........",
	};
	const char *const finer[] = {
		"........", ".#...#..", ".##.##..", "..#.#...",
		"...#....", "........", "........", "........",
	};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 2, false,
	    &pen, 7, 2, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, open, 7, 0) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 2, false,
	    &pen, 7, 2, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, open, 0, 0) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 2, true,
	    &pen, 7, 2, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, closed, 7, 0) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 4, false,
	    &pen, 7, 2, ST_RIP3_COPY) == 0);
	CHECK(expect(ports, finer, 7, 0) == 0);
	{
		struct st_rip3_fill fill;

		CHECK(st_rip3_fill_solid(&fill, true) == 0);
		for (unsigned border = 0; border <= 1; border++) {
			CHECK(st_rip3_ports_erase(ports, 2) == 0);
			CHECK(st_rip3_draw_filled_bezier_path(ports, &start, &curve, 1, 2,
			    &fill, 3, 0, border ? &pen : NULL, 9, ST_RIP3_XOR) == 0);
			/* Subdivision two forms the triangle (1,1),(3,4),(5,1).
			 * Its half-cell interior is four pixels in row 1, two in row 2. */
			for (int32_t y = 0; y < 8; y++) {
				for (int32_t x = 0; x < 8; x++) {
					uint8_t value;
					uint8_t expected = (y == 1 && x >= 1 && x <= 4)
					    || (y == 2 && x >= 2 && x <= 3) ? 1 : 2;

					if (border && closed[y][x] == '#')
						expected = 9;
					CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
				}
			}
		}
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
chains(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_pen pen;
	struct st_rip3_fill fill;
	const struct st_rip3_rect bounds = {0,0,7,7};
	const struct st_rip3_point start = {1,1};
	const struct st_rip3_bezier_segment square[] = {
		{ST_RIP3_BEZIER_SMOOTH, {{2,1},{4,1},{5,1}}},
		{ST_RIP3_BEZIER_LINE, {{5,5},{0,0},{0,0}}},
		{ST_RIP3_BEZIER_JOINTED, {{4,5},{2,5},{1,5}}},
		{ST_RIP3_BEZIER_LINE, {{1,1},{0,0},{0,0}}},
	};
	const struct st_rip3_point line_start = {0,3};
	const struct st_rip3_bezier_segment line[] = {
		{ST_RIP3_BEZIER_SMOOTH, {{2,3},{4,3},{6,3}}},
		{ST_RIP3_BEZIER_LINE, {{7,3},{0,0},{0,0}}},
	};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	for (unsigned border = 0; border <= 1; border++) {
		CHECK(st_rip3_ports_erase(ports, 2) == 0);
		CHECK(st_rip3_draw_filled_bezier_path(ports, &start, square, 4, 4,
		    &fill, 7, 3, border ? &pen : NULL, 9, ST_RIP3_XOR) == 0);
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 8; x++) {
				uint8_t value;
				uint8_t expected = x >= 1 && x < 5 && y >= 1 && y < 5 ? 5 : 2;

				if (border && (((x == 1 || x == 5) && y >= 1 && y <= 5)
				    || ((y == 1 || y == 5) && x >= 1 && x <= 5)))
					expected = 9;
				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
			}
		}
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_pen_set(&pen, true, 4, 0xaaaa, 1) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &line_start, line, 2, 3, false,
	    &pen, 7, 2, ST_RIP3_XOR) == 0);
	for (int32_t x = 0; x < 8; x++) {
		uint8_t value;

		CHECK(st_rip3_ports_read(ports, 1, x, 3, &value) == 0);
		CHECK(value == (x % 2 == 0 ? 7 : 2));
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
errors_and_limits(void)
{
	const struct st_rip3_point start = {0,0};
	struct st_rip3_bezier_segment *segments = calloc(4095, sizeof(*segments));
	struct st_rip3_point sentinel = {7,8};
	struct st_rip3_bezier_path path = {&sentinel,99};
	struct st_rip3_bezier_path expanded = {0};

	CHECK(segments != NULL);
	CHECK(st_rip3_bezier_build(&path, NULL, segments, 1, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bezier_build(&path, &start, NULL, 1, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bezier_build(&path, &start, segments, 0, 1) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_bezier_build(&path, &start, segments, SIZE_MAX, 1) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_bezier_build(&path, &start, segments, 1, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_bezier_build(&path, &start, segments, 1, SIZE_MAX) == IO_ERROR_OUT_OF_RANGE);
	segments[0].type = (enum st_rip3_bezier_type)3;
	CHECK(st_rip3_bezier_build(&path, &start, segments, 1, 1) == IO_ERROR_INVALID_ARGUMENT);
	segments[0].type = ST_RIP3_BEZIER_LINE;
	CHECK(st_rip3_bezier_build(&expanded, &start, segments, 4095, 4095) == 0);
	CHECK(expanded.count == 4096);
	st_rip3_bezier_free(&expanded);
	segments[4094].type = ST_RIP3_BEZIER_JOINTED;
	CHECK(st_rip3_bezier_build(&path, &start, segments, 4095, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(path.points == &sentinel && path.count == 99);
	CHECK(sentinel.x == 7 && sentinel.y == 8);
	free(segments);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_pen pen;
	struct st_rip3_fill fill;
	const struct st_rip3_point start = {0,1};
	const struct st_rip3_bezier_segment curve = {
		ST_RIP3_BEZIER_JOINTED, {{0,5},{5,5},{5,1}},
	};
	const struct st_rip3_rect bounds = {10,20,17,27};
	const struct st_rip3_rect view = {1,1,6,6};
	const struct st_rip3_rect zero = {0,0,0,0};
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 1, false,
	    &pen, 2, 3, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 0, false,
	    &pen, 7, 3, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	pen.pattern = 0;
	CHECK(st_rip3_draw_filled_bezier_path(ports, &start, &curve, 1, 4,
	    &fill, 7, 3, &pen, 9, ST_RIP3_XOR) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 768; y++) {
		for (uint32_t x = 0; x < 1024; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG;

			if (x >= 11 && x <= 16 && y == 22)
				expected = ST_PIXEL_COLOR_RGB_VALUE(0,170,0);
			CHECK(capture->primary[(size_t)y * 1024 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_viewport(ports, &zero) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(st_rip3_draw_bezier_path(ports, &start, &curve, 1, 4, true,
	    &pen, 7, 3, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_filled_bezier_path(ports, &start, &curve, 1, 4,
	    &fill, 7, 3, &pen, 9, ST_RIP3_COPY) == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(samples() == 0);
	CHECK(outlines() == 0);
	CHECK(chains() == 0);
	CHECK(errors_and_limits() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
