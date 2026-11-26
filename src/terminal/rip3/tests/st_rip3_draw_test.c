/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdio.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_draw.h"
#include "st_rip3_ports.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
styles(void)
{
	struct st_rip3_pen pen;
	const uint16_t     expected[] = {0xffff, 0x3333, 0x1e3f, 0x1f1f};

	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(pen.pattern == 0xffff && pen.thickness == 1 && !pen.draw_background);
	for (unsigned i = 0; i < 4; i++) {
		CHECK(st_rip3_pen_set(&pen, true, i, 0, 3) == 0);
		CHECK(pen.pattern == expected[i] && pen.thickness == 3 && pen.draw_background);
	}
	CHECK(st_rip3_pen_set(&pen, false, 4, 0xaaaa, 2) == 0);
	CHECK(pen.pattern == 0xaaaa && pen.thickness == 2 && !pen.draw_background);
	CHECK(st_rip3_pen_set(&pen, true, 4, 0, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_pen_set(&pen, true, 5, 1, 1) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_pen_set(&pen, true, 0, 0, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_pen_set(&pen, true, 0, 0, UINT32_MAX) == IO_ERROR_OUT_OF_RANGE);
	CHECK(pen.pattern == 0xaaaa && pen.thickness == 2 && !pen.draw_background);
	return 0;
}

static int
patterns(void)
{
	struct st_rip3_ports   *ports = NULL;
	struct st_rip3_pen      pen;
	struct st_rip3_point    first      = {0, 0};
	struct st_rip3_point    last       = {19, 0};
	struct st_rip3_rect     bounds     = {10, 20, 29, 27};
	const uint8_t           expected[] = {7, 7, 7, 7, 2, 2, 2, 2, 7, 7, 7, 7, 2, 2, 2, 2, 7, 7, 7, 7};
	struct st_pixel_buffer *capture    = NULL;
	struct st_rip3_palette  palette;
	uint8_t                 value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_pen_set(&pen, false, 4, 0xf0f0, 1) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 4, ST_RIP3_COPY) == 0);
	for (int32_t x = 0; x < 20; x++) {
		CHECK(st_rip3_ports_read(ports, 1, x, 0, &value) == 0);
		CHECK(value == expected[x]);
	}
	/* Gaps write background only when enabled; XOR applies to both colors. */
	first.y = last.y = 1;
	CHECK(st_rip3_pen_set(&pen, true, 4, 0xf0f0, 1) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 4, ST_RIP3_XOR) == 0);
	for (int32_t x = 0; x < 20; x++) {
		CHECK(st_rip3_ports_read(ports, 1, x, 1, &value) == 0);
		CHECK(value == (expected[x] == 7 ? 5 : 6));
	}
	/* MSB starts at x=-3. Clipping at the viewport retains phase 3, so
	 * mask 0x1000 paints at visible x=0 and x=16, not x=3 and x=19. */
	first = (struct st_rip3_point) {-3, 2};
	last  = (struct st_rip3_point) {20, 2};
	CHECK(st_rip3_pen_set(&pen, false, 4, 0x1000, 1) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 4, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	/* One capture covers foreground, transparent/background gaps and margins. */
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			uint32_t color = 0;

			if (x >= 10 && x < 30 && y >= 20 && y < 28) {
				color = 2;
				if (y == 20)
					color = expected[x - 10];
				if (y == 21)
					color = expected[x - 10] == 7 ? 5 : 6;
				if (y == 22 && (x == 10 || x == 26))
					color = 7;
			}
			CHECK(capture->primary[(size_t)y * 640 + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[color]));
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
coverage(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_pen    pen;
	struct st_rip3_rect   bounds = {0, 0, 7, 7};
	struct st_rip3_point  first  = {2, 3};
	struct st_rip3_point  last   = {5, 3};
	uint8_t               value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_800_600) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_pen_set(&pen, false, 0, 0, 3) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_XOR) == 0);
	/* A 3-pixel horizontal stroke, round caps included, covers these
	 * integer pixel centers. Every covered pixel must XOR exactly once. */
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 1 && x <= 6 && y >= 2 && y <= 4 ? 7 : 0));
		}
	}
	CHECK(st_rip3_draw_line(ports, &last, &first, &pen, 7, 0, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 0);
		}
	}
	CHECK(st_rip3_pen_set(&pen, false, 0, 0, 2) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 2 && x <= 5 && y >= 3 && y <= 4 ? 7 : 0));
		}
	}
	CHECK(st_rip3_draw_line(ports, &last, &first, &pen, 7, 0, ST_RIP3_XOR) == 0);
	first = (struct st_rip3_point) {3, 2};
	last  = (struct st_rip3_point) {3, 5};
	CHECK(st_rip3_draw_line(ports, &last, &first, &pen, 7, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x >= 3 && x <= 4 && y >= 2 && y <= 5 ? 7 : 0));
		}
	}
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	CHECK(st_rip3_pen_init(&pen) == 0);
	first = (struct st_rip3_point) {1, 1};
	last  = (struct st_rip3_point) {6, 6};
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 3, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (x == y && x >= 1 && x <= 6 ? 3 : 0));
		}
	}
	/* Point-degenerate line and each independent write operation. */
	first = last = (struct st_rip3_point) {0, 0};
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 5, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 10, 0, ST_RIP3_OR) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 15);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 3, 0, ST_RIP3_AND) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 3);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 0, 0, ST_RIP3_NOT) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 252);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports  = NULL;
	struct st_rip3_rect   bounds = {0, 0, 7, 7};
	struct st_rip3_point  first  = {INT32_MIN, 2};
	struct st_rip3_point  last   = {INT32_MAX, 2};
	struct st_rip3_pen    pen;
	uint8_t               value;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_pen_set(&pen, false, 4, 0x8000, 1) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_COPY) == 0);
	for (int32_t x = 0; x < 8; x++) {
		CHECK(st_rip3_ports_read(ports, 1, x, 2, &value) == 0);
		CHECK(value == (x == 0 ? 7 : 0));
	}
	CHECK(st_rip3_pen_set(&pen, false, 0, 0, INT32_MAX) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 3, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 3);
	}
	CHECK(st_rip3_pen_init(&pen) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, (enum st_rip3_write_mode)5)
	      == IO_ERROR_INVALID_ARGUMENT);
	pen.pattern = 0;
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	pen.pattern   = 0xffff;
	pen.thickness = UINT32_MAX;
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_pen_init(&pen) == 0);
	first.x = last.x = INT32_MAX;
	first.y = last.y = INT32_MIN;
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_draw_line(ports, NULL, &last, &pen, 7, 0, ST_RIP3_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_protect(ports, 1, false) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	first = (struct st_rip3_point) {0, 0};
	last  = (struct st_rip3_point) {7, 7};
	CHECK(st_rip3_draw_line(ports, &first, &last, &pen, 7, 0, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == 3);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(styles() == 0);
	CHECK(patterns() == 0);
	CHECK(coverage() == 0);
	CHECK(boundaries() == 0);
	return 0;
}
