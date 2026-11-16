/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_draw.h"
#include "st_rip3_ports.h"
#include "io_error.h"

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
expect(struct st_rip3_ports *ports, const uint8_t *expected,
    int32_t width, int32_t height)
{
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			uint8_t value;

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			if (value != expected[y * width + x]) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n",
				    x, y, value, expected[y * width + x]);
				return 1;
			}
		}
	}
	return 0;
}

static int
paths(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_pen pen;
	const struct st_rip3_rect bounds = {0, 0, 5, 5};
	const struct st_rip3_point points[] = {{1,1}, {4,1}, {4,4}};
	const struct st_rip3_point crossing[] = {{1,1}, {5,5}, {1,5}, {5,1}};
	const struct st_rip3_point phased[] = {{0,0}, {2,0}, {2,3}};
	const uint8_t open[] = {
		0,0,0,0,0,0, 0,7,7,7,7,0, 0,0,0,0,7,0,
		0,0,0,0,7,0, 0,0,0,0,7,0, 0,0,0,0,0,0,
	};
	const uint8_t closed[] = {
		0,0,0,0,0,0, 0,7,7,7,7,0, 0,0,7,0,7,0,
		0,0,0,7,7,0, 0,0,0,0,7,0, 0,0,0,0,0,0,
	};
	const uint8_t cross_expected[] = {
		0,0,0,0,0,0, 0,7,0,0,0,7, 0,0,7,0,7,0,
		0,0,0,7,0,0, 0,0,7,0,7,0, 0,7,7,7,7,7,
	};
	const uint8_t phase_expected[] = {
		7,7,7,0,0,0, 0,0,7,0,0,0, 0,0,2,0,0,0,
		0,0,2,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0,
	};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(st_rip3_draw_path(ports, points, 3, false, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, open, 6, 6) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_path(ports, points, 3, true, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, closed, 6, 6) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_path(ports, crossing, 4, false, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(expect(ports, cross_expected, 6, 6) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_pen_set(&pen, true, 4, 0xf000, 1) == 0);
	CHECK(st_rip3_draw_path(ports, phased, 3, false, &pen, 7, 2, ST_RIP3_COPY) == 0);
	CHECK(expect(ports, phase_expected, 6, 6) == 0);
	/* At a crossing, an on sample wins over another segment's off sample. */
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_pen_set(&pen, true, 4, 0xf800, 1) == 0);
	CHECK(st_rip3_draw_path(ports, crossing, 4, false, &pen, 7, 2, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 3, 3, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_pen_set(&pen, false, 0, 0, 3) == 0);
	CHECK(st_rip3_draw_path(ports, points, 3, false, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 4, 1, &value) == 0 && value == 7);
	CHECK(st_rip3_draw_path(ports, points, 3, false, &pen, 7, 0, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 0);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
rectangles(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_pen pen;
	struct st_rip3_rect bounds = {0, 0, 7, 7};
	struct st_rip3_rect rect = {5, 5, 1, 1};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(st_rip3_draw_rectangle(ports, &rect, &pen, 7, 0, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			bool edge = ((x == 1 || x == 5) && y >= 1 && y <= 5)
			    || ((y == 1 || y == 5) && x >= 1 && x <= 5);

			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (edge ? 7 : 0));
		}
	}
	CHECK(rect.x0 == 5 && rect.x1 == 1 && rect.y0 == 5 && rect.y1 == 1);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	rect = (struct st_rip3_rect){2, 4, 2, 1};
	CHECK(st_rip3_draw_rectangle(ports, &rect, &pen, 7, 0, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x == 2 && y >= 1 && y <= 4 ? 7 : 0));
		}
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	rect = (struct st_rip3_rect){3, 3, 3, 3};
	CHECK(st_rip3_draw_rectangle(ports, &rect, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 3, 3, &value) == 0 && value == 7);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_pen pen;
	const struct st_rip3_rect bounds = {0, 0, 7, 7};
	struct st_rip3_point points[4096];
	struct st_rip3_point extremes[] = {{INT32_MIN,2}, {INT32_MAX,2}};
	uint8_t value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	for (size_t i = 0; i < 4096; i++)
		points[i] = (struct st_rip3_point){2, 2};
	/* All 4096 coincident vertices still describe one covered point. */
	CHECK(st_rip3_draw_path(ports, points, 4096, false, &pen, 7, 0, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x == 2 && y == 2 ? 7 : 0));
		}
	}
	CHECK(st_rip3_draw_path(ports, points, 4096, true, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &value) == 0 && value == 0);
	CHECK(st_rip3_draw_path(ports, points, 1, false, &pen, 7, 0, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_draw_path(ports, extremes, 2, false, &pen, 7, 0, ST_RIP3_COPY) == 0);
	for (int32_t x = 0; x < 8; x++)
		CHECK(st_rip3_ports_read(ports, 1, x, 2, &value) == 0 && value == 7);
	CHECK(st_rip3_ports_erase(ports, 4) == 0);
	CHECK(st_rip3_draw_path(ports, points, 0, false, &pen, 7, 0, ST_RIP3_COPY)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_path(ports, points, 4097, false, &pen, 7, 0, ST_RIP3_COPY)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_path(ports, points, SIZE_MAX, false, &pen, 7, 0, ST_RIP3_COPY)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_path(ports, NULL, 1, false, &pen, 7, 0, ST_RIP3_COPY)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_path(ports, points, 2, false, &pen, 7, 0,
	    (enum st_rip3_write_mode)5) == IO_ERROR_INVALID_ARGUMENT);
	pen.pattern = 0;
	CHECK(st_rip3_draw_path(ports, points, 2, false, &pen, 7, 0, ST_RIP3_COPY)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_pen_init(&pen) == 0);
	points[0] = (struct st_rip3_point){INT32_MAX, INT32_MAX};
	CHECK(st_rip3_draw_path(ports, points, 1, false, &pen, 7, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_draw_path(ports, extremes, 2, false, &pen, 7, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 4);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(paths() == 0);
	CHECK(rectangles() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
