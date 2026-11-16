/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_rounded_rectangle.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern unsigned st_rip3_drawing_test_calloc_countdown;
extern unsigned st_rip3_ellipse_test_realloc_countdown;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* Independent radius-three circular corners and two-pixel straight spans. */
static const char *const outline[] = {
	"...........", "...#####...", "..#.....#..", ".#.......#.",
	".#.......#.", ".#.......#.", ".#.......#.", ".#.......#.",
	"..#.....#..", "...#####...", "...........",
};
static const char *const interior[] = {
	"...........", "..######...", ".########..", ".########..",
	".########..", ".########..", ".########..", ".########..",
	"..######...", "...........", "...........",
};

static void
palette(struct st_rip3_palette *result)
{
	for (unsigned i = 0; i < 256; i++) result->rgb[i] = i*UINT32_C(0x010101);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	static const uint8_t on[] = {7,14,15,1,246}, off[] = {2,11,11,0,246};
	const struct st_rip3_rect bounds = {10,20,89,89}, view = {5,7,64,42};
	const unsigned rows[] = {170,85,170,85,170,85,170,85};
	const struct st_rip3_point phase = {1,2};
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill fill;
	struct st_rip3_palette colors;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_port_info info;
	unsigned current;
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0 && st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_fill_custom(&fill, rows, &phase) == 0);
	for (unsigned row = 0; row < 3; row++) for (unsigned mode = 0; mode < 5; mode++) {
		struct st_rip3_rounded_rectangle rectangle;
		struct st_rip3_rect underlay = {(int32_t)mode*12,(int32_t)row*12,
		    (int32_t)mode*12+10,(int32_t)row*12+10};
		double x = mode*12, y = row*12;
		CHECK(st_rip3_ports_fill(ports, &underlay, 9, ST_RIP3_COPY) == 0);
		/* Opposing corners normalize without mutating the caller's geometry. */
		CHECK(st_rip3_rounded_rectangle_set(&rectangle, x+9,y+9,x+1,y+1,3) == 0);
		CHECK(rectangle.x0 == x+1 && rectangle.y0 == y+1 && rectangle.x1 == x+9 && rectangle.y1 == y+9);
		if (row == 0)
			CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, 1,7,(enum st_rip3_write_mode)mode) == 0);
		else CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,
		    row == 1 ? 1 : 0,5,(enum st_rip3_write_mode)mode) == 0);
	}
	palette(&colors);
	CHECK(st_rip3_ports_snapshot(ports, &colors, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (x >= 15 && x < 74 && y >= 27 && y < 62) {
			unsigned col = (x-15)/12, row = (y-27)/12, u = (x-15)%12, v = (y-27)%12;
			if (col < 5 && row < 3 && u < 11 && v < 11) {
				expected = 9;
				if (row == 0 && outline[v][u] == '#') expected = on[col];
				if (row != 0 && interior[v][u] == '#') expected = ((u+v)%2 == 1 ? on : off)[col];
				if (row == 1 && outline[v][u] == '#') expected = 5;
			}
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0 && info.protected && info.viewport_active
	    && info.viewport.x0 == 5 && info.viewport.y0 == 7 && info.viewport.x1 == 64 && info.viewport.y1 == 42);
	CHECK(fill.origin.x == 1 && fill.origin.y == 2 && fill.rows[0] == 170 && fill.rows[1] == 85);
	st_pixel_buffer_destroy(capture); st_rip3_ports_destroy(ports);
	return 0;
}

static int
special(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_rounded_rectangle rectangle;
	struct st_rip3_fill fill;
	struct st_rip3_palette colors;
	struct st_pixel_buffer *capture = NULL;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 1,1,7,5,0) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 3,2,0,5,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 10,1,16,5,2) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,0,5,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 20,1,20,5,0) == 0);
	CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, 3,9,ST_RIP3_XOR) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 25.5,1.5,28.5,4.5,0) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 11,2,0,5,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 33,3,33,3,0) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 3,2,0,5,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 3,2,1,5,ST_RIP3_NOT) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 40,1,46,7,3) == 0);
	CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, 1,7,ST_RIP3_XOR) == 0);
	palette(&colors);
	CHECK(st_rip3_ports_snapshot(ports, &colors, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) for (uint32_t x = 0; x < 640; x++) {
		unsigned expected = 0;
		if (x >= 1 && x <= 6 && y >= 1 && y <= 4) expected = 3;
		if (((y == 1 || y == 4) && x >= 11 && x <= 14)
		    || ((y == 2 || y == 3) && x >= 10 && x <= 15)) expected = 7;
		if (x >= 19 && x <= 21 && y <= 6) expected = 9;
		if (x >= 25 && x <= 27 && y >= 1 && y <= 3) expected = 11;
		if (x == 33 && y == 3) expected = 5;
		if (x >= 40 && x <= 46 && y >= 1 && y <= 7) {
			int dx = (int)x-43, dy = (int)y-4;
			if (dx*dx+dy*dy >= 7 && dx*dx+dy*dy <= 12) expected = 7;
		}
		CHECK(capture->primary[(size_t)y*640+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	st_pixel_buffer_destroy(capture); st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_rounded_rectangle rectangle, invalid;
	struct st_rip3_fill fill;
	const struct st_rip3_rect bounds = {0,0,10,10};
	uint8_t pixel;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0 && st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0 && st_rip3_fill_solid(&fill, true) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 1,1,9,9,3) == 0);
	CHECK(st_rip3_rounded_rectangle_set(NULL, 1,1,9,9,3) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 1,1,9,9,4.1) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 1,1,9,9,-1) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, NAN,1,9,9,3) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, 1,1,9,9,HUGE_VAL) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, -1024.0*INT32_MAX-1,1,9,9,3) == IO_ERROR_OUT_OF_RANGE);
	CHECK(rectangle.x0 == 1 && rectangle.y0 == 1 && rectangle.x1 == 9 && rectangle.y1 == 9 && rectangle.radius == 3);
	invalid = rectangle; invalid.radius = 5;
	CHECK(st_rip3_draw_rounded_rectangle(ports, &invalid, 1,7,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &invalid, &fill, 7,2,1,5,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, 0,7,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, UINT32_MAX,7,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_rounded_rectangle(NULL, &rectangle, 1,7,ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, 1,7,(enum st_rip3_write_mode)5) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,UINT32_MAX,5,ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, NULL, 7,2,1,5,ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	for (unsigned allocation = 1; allocation <= 8; allocation++) {
		st_rip3_ellipse_test_realloc_countdown = allocation;
		CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,1,5,ST_RIP3_XOR)
		    == IO_ERROR_OUT_OF_MEMORY && st_rip3_ellipse_test_realloc_countdown == 0);
		st_rip3_drawing_test_calloc_countdown = allocation;
		CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,1,5,ST_RIP3_NOT)
		    == IO_ERROR_OUT_OF_MEMORY && st_rip3_drawing_test_calloc_countdown == 0);
	}
	for (int32_t y = 0; y < 11; y++) for (int32_t x = 0; x < 11; x++)
		CHECK(st_rip3_ports_read(ports, 1, x,y,&pixel) == 0 && pixel == 9);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	st_rip3_ellipse_test_realloc_countdown = st_rip3_drawing_test_calloc_countdown = 1;
	CHECK(st_rip3_draw_rounded_rectangle(ports, &rectangle, 1,7,ST_RIP3_XOR) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,1,5,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ellipse_test_realloc_countdown == 1 && st_rip3_drawing_test_calloc_countdown == 1);
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,0,5,ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ellipse_test_realloc_countdown == 1 && st_rip3_drawing_test_calloc_countdown == 1);
	st_rip3_ellipse_test_realloc_countdown = st_rip3_drawing_test_calloc_countdown = 0;
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, -1e12,1,7,9,3) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 7,2,1,5,ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 11; y++) for (int32_t x = 0; x < 11; x++) {
		unsigned expected = 0;
		if ((y == 1 || y == 8) && x <= 5) expected = 7;
		if (y >= 2 && y <= 7 && x <= 6) expected = 7;
		if ((y == 1 || y == 9) && x <= 5) expected = 5;
		if ((y == 2 || y == 8) && x == 6) expected = 5;
		if (y >= 3 && y <= 7 && x == 7) expected = 5;
		CHECK(st_rip3_ports_read(ports, 1, x,y,&pixel) == 0 && pixel == expected);
	}
	CHECK(st_rip3_rounded_rectangle_set(&rectangle, -1e12,-1e12,1e12,1e12,0) == 0);
	CHECK(st_rip3_draw_filled_rounded_rectangle(ports, &rectangle, &fill, 3,2,1,5,ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 11; y++) for (int32_t x = 0; x < 11; x++)
		CHECK(st_rip3_ports_read(ports, 1, x,y,&pixel) == 0 && pixel == 3);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	CHECK(special() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
