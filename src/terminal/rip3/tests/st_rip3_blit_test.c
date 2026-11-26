/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_ports.h"
#include "st_rip3_surface.h"

extern bool st_rip3_copy_test_fail_malloc;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
expect(const struct st_rip3_surface *surface, const uint8_t *pixels, int32_t width, int32_t height)
{
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			uint8_t value = 0;
			CHECK(st_rip3_surface_get(surface, x, y, &value) == 0);
			if (value != pixels[y * width + x]) {
				fprintf(stderr, "at %d,%d: got %u, expected %u\n", x, y, value,
				    pixels[y * width + x]);
				return 1;
			}
		}
	}
	return 0;
}

static int
seed(struct st_rip3_surface *surface, int32_t width, int32_t height)
{
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++)
			CHECK(st_rip3_surface_plot(surface, NULL, x, y, (uint8_t)(y * width + x + 1), ST_RIP3_COPY)
			      == 0);
	}
	return 0;
}

static int
clipping(void)
{
	struct st_rip3_surface   *source = NULL, *destination = NULL;
	const struct st_rip3_rect whole            = {0, 0, 5, 3};
	const struct st_rip3_rect from             = {-1, -1, 4, 2};
	const struct st_rip3_rect source_clip      = {1, 0, 3, 2};
	const struct st_rip3_rect destination_clip = {1, 2, 4, 3};
	const struct st_rip3_rect extreme          = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
	const uint8_t             clipped[]        = {
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    8,
	    9,
	    10,
	    90,
	    90,
	    90,
	    14,
	    15,
	    16,
	    90,
	};
	const uint8_t negative[] = {
	    9,
	    10,
	    11,
	    12,
	    90,
	    90,
	    15,
	    16,
	    17,
	    18,
	    90,
	    90,
	    21,
	    22,
	    23,
	    24,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	};
	const uint8_t positive[] = {
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    90,
	    1,
	    2,
	    90,
	    90,
	    90,
	    90,
	    7,
	    8,
	};
	uint8_t original[24];

	for (size_t i = 0; i < sizeof(original); i++)
		original[i] = (uint8_t)(i + 1);
	CHECK(st_rip3_surface_create(&source, 6, 4) == 0);
	CHECK(st_rip3_surface_create(&destination, 6, 4) == 0);
	CHECK(seed(source, 6, 4) == 0);
	CHECK(st_rip3_surface_fill(destination, NULL, &whole, 90, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_blit(destination, &destination_clip, 0, 0, source, &source_clip, &from,
		  ST_RIP3_TRANSFER_COPY)
	      == 0);
	CHECK(expect(destination, clipped, 6, 4) == 0);
	CHECK(st_rip3_surface_fill(destination, NULL, &whole, 90, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_blit(destination, NULL, -2, -1, source, NULL, &whole, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(destination, negative, 6, 4) == 0);
	CHECK(st_rip3_surface_fill(destination, NULL, &whole, 90, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_blit(destination, NULL, 4, 2, source, NULL, &whole, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(expect(destination, positive, 6, 4) == 0);
	/* The enormous requested region maps source (0,0) to destination (0,0).
	 * Work and allocation must depend on visible pixels, not requested extent. */
	CHECK(st_rip3_surface_blit(destination, &extreme, INT32_MIN, INT32_MIN, source, &extreme, &extreme,
		  ST_RIP3_TRANSFER_COPY)
	      == 0);
	CHECK(expect(destination, original, 6, 4) == 0);
	CHECK(expect(source, original, 6, 4) == 0);
	st_rip3_surface_destroy(source);
	st_rip3_surface_destroy(destination);
	return 0;
}

static int
unchanged(void)
{
	struct st_rip3_surface   *surface    = NULL;
	const struct st_rip3_rect area       = {0, 0, 3, 3};
	const struct st_rip3_rect invalid    = {1, 1, 0, 0};
	const struct st_rip3_rect external   = {5, 5, INT32_MAX, INT32_MAX};
	const uint8_t             original[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

	CHECK(st_rip3_surface_create(&surface, 4, 4) == 0);
	CHECK(seed(surface, 4, 4) == 0);
	CHECK(st_rip3_surface_blit(NULL, NULL, 0, 0, surface, NULL, &area, ST_RIP3_TRANSFER_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, NULL, NULL, &area, ST_RIP3_TRANSFER_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, NULL, NULL, ST_RIP3_TRANSFER_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, NULL, &invalid, ST_RIP3_TRANSFER_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, &invalid, 0, 0, surface, NULL, &area, ST_RIP3_TRANSFER_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, &invalid, &area, ST_RIP3_TRANSFER_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, NULL, &area, (enum st_rip3_transfer_mode) - 1)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, NULL, &area, (enum st_rip3_transfer_mode)5)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(expect(surface, original, 4, 4) == 0);
	/* Empty intersections require no snapshot; leave the failure armed. */
	st_rip3_copy_test_fail_malloc = true;
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, NULL, &external, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_surface_blit(surface, &external, 0, 0, surface, NULL, &area, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_surface_blit(surface, NULL, 0, 0, surface, &external, &area, ST_RIP3_TRANSFER_COPY) == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		CHECK(st_rip3_surface_blit(surface, NULL, INT32_MIN, INT32_MAX, surface, NULL, &area,
			  (enum st_rip3_transfer_mode)mode)
		      == 0);
		CHECK(st_rip3_surface_blit(surface, NULL, INT32_MAX, INT32_MIN, surface, NULL, &area,
			  (enum st_rip3_transfer_mode)mode)
		      == 0);
		CHECK(st_rip3_copy_test_fail_malloc);
		CHECK(st_rip3_surface_blit(surface, NULL, 1, 1, surface, NULL, &area,
			  (enum st_rip3_transfer_mode)mode)
		      == IO_ERROR_OUT_OF_MEMORY);
		CHECK(!st_rip3_copy_test_fail_malloc);
		CHECK(expect(surface, original, 4, 4) == 0);
		st_rip3_copy_test_fail_malloc = true;
	}
	st_rip3_copy_test_fail_malloc = false;
	st_rip3_surface_destroy(surface);
	return 0;
}

/* Independent per-pixel logical operations; geometry expectations below are
 * literal source-position maps, never produced by the transfer implementation. */
static uint8_t
transfer(unsigned mode, uint8_t source, uint8_t destination)
{
	switch (mode) {
		case 0:
			return source;
		case 1:
			return source ^ destination;
		case 2:
			return source | destination;
		case 3:
			return source & destination;
		default:
			return (uint8_t)~source;
	}
}

static int
overlap(void)
{
	struct st_rip3_surface   *surface   = NULL;
	const struct st_rip3_rect regions[] = {
	    {0, 0, 2, 2},
            {1, 1, 3, 3},
            {0, 0, 3, 3}
        };
	const int32_t targets[][2] = {
	    {1, 1},
            {0, 0},
            {0, 0}
        };
	/* Zero means untouched; other values name the original source pixel. */
	const uint8_t source_map[][16] = {
	    {0, 0, 0, 0, 0,  1,  2,  3, 0,  5,  6,  7,  0,  9,  10, 11},
	    {6, 7, 8, 0, 10, 11, 12, 0, 14, 15, 16, 0,  0,  0,  0,  0 },
	    {1, 2, 3, 4, 5,  6,  7,  8, 9,  10, 11, 12, 13, 14, 15, 16},
	};
	CHECK(st_rip3_surface_create(&surface, 4, 4) == 0);
	for (size_t direction = 0; direction < 3; direction++) {
		for (unsigned mode = 0; mode < 5; mode++) {
			uint8_t expected[16];
			CHECK(seed(surface, 4, 4) == 0);
			for (size_t i = 0; i < sizeof(expected); i++)
				expected[i] = source_map[direction][i] == 0
				                  ? (uint8_t)(i + 1)
				                  : transfer(mode, source_map[direction][i], (uint8_t)(i + 1));
			CHECK(st_rip3_surface_blit(surface, NULL, targets[direction][0], targets[direction][1],
				  surface, NULL, &regions[direction], (enum st_rip3_transfer_mode)mode)
			      == 0);
			CHECK(expect(surface, expected, 4, 4) == 0);
		}
	}
	st_rip3_surface_destroy(surface);
	return 0;
}

static int
capture(struct st_rip3_ports *ports, const uint8_t *tile, uint32_t x0, uint32_t y0, uint32_t width,
    uint32_t height)
{
	struct st_rip3_palette  palette;
	struct st_pixel_buffer *pixels = NULL;
	for (unsigned i = 0; i < 256; i++)
		palette.rgb[i] = i * UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &pixels) == 0);
	for (uint32_t y = 0; y < pixels->height; y++) {
		for (uint32_t x = 0; x < pixels->width; x++) {
			uint8_t value = 0;
			if (x >= x0 && y >= y0 && x - x0 < width && y - y0 < height)
				value = tile[(y - y0) * width + x - x0];
			CHECK(pixels->primary[(size_t)y * pixels->width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[value]));
		}
	}
	st_pixel_buffer_destroy(pixels);
	return 0;
}

static bool
same_rect(const struct st_rip3_rect *a, const struct st_rip3_rect *b)
{
	return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1;
}

static bool
same_info(const struct st_rip3_port_info *a, const struct st_rip3_port_info *b)
{
	return a->defined == b->defined && a->offscreen == b->offscreen && a->protected == b->protected
	       && a->viewport_active == b->viewport_active && same_rect(&a->bounds, &b->bounds)
	       && same_rect(&a->viewport, &b->viewport) && a->image.automatic == b->image.automatic
	       && a->image.enabled == b->image.enabled && same_rect(&a->image.area, &b->image.area)
	       && a->image.flags == b->image.flags;
}

static int
ports_test(enum st_rip3_canvas canvas)
{
	struct st_rip3_ports     *ports   = NULL;
	const struct st_rip3_rect bounds1 = {10, 20, 15, 25}, bounds2 = {11, 21, 16, 26};
	const struct st_rip3_rect view = {1, 1, 4, 4}, offscreen = {0, 0, 3, 3};
	const struct st_rip3_rect offview = {1, 1, 3, 3}, from = {-1, -1, 2, 2};
	const uint8_t             original[36] = {
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    1,
	    2,
	    3,
	    4,
	    0,
	    0,
	    5,
	    6,
	    7,
	    8,
	    0,
	    0,
	    9,
	    10,
	    11,
	    12,
	    0,
	    0,
	    13,
	    14,
	    15,
	    16,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	    0,
	};
	const uint8_t source_map[36] = {
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
	    1,
	    2,
	    3,
	    4,
	    0,
	    0,
	    5,
	    6,
	    7,
	    8,
	    0,
	    0,
	    9,
	    10,
	    11,
	    12,
	    0,
	    0,
	    13,
	    14,
	    15,
	    16,
	};
	const uint8_t            offexpected[16] = {0, 0, 0, 0, 0, 21, 22, 23, 0, 24, 25, 26, 0, 27, 28, 29};
	struct st_rip3_port_info before[3], after;
	unsigned                 current;

	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds1, false) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &bounds2, false) == 0);
	for (unsigned i = 1; i <= 2; i++) {
		CHECK(st_rip3_ports_switch(ports, i) == 0);
		CHECK(st_rip3_ports_viewport(ports, &view) == 0);
		CHECK(st_rip3_ports_protect(ports, i, true) == 0);
		CHECK(st_rip3_ports_info(ports, i, &before[i - 1]) == 0);
	}
	CHECK(st_rip3_ports_switch(ports, 0) == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		uint8_t expected[36];
		CHECK(st_rip3_ports_erase(ports, 0) == 0);
		for (int32_t y = 0; y < 4; y++) {
			for (int32_t x = 0; x < 4; x++)
				CHECK(st_rip3_ports_plot(ports, 11 + x, 21 + y, (uint8_t)(1 + y * 4 + x),
					  ST_RIP3_COPY)
				      == 0);
		}
		for (size_t i = 0; i < sizeof(expected); i++)
			expected[i] =
			    source_map[i] == 0 ? original[i] : transfer(mode, source_map[i], original[i]);
		st_rip3_copy_test_fail_malloc = true;
		CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 0, 0, (enum st_rip3_transfer_mode)mode)
		      == IO_ERROR_OUT_OF_MEMORY);
		CHECK(!st_rip3_copy_test_fail_malloc);
		CHECK(capture(ports, original, 10, 20, 6, 6) == 0);
		CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 0, 0, (enum st_rip3_transfer_mode)mode) == 0);
		CHECK(capture(ports, expected, 10, 20, 6, 6) == 0);
	}
	CHECK(st_rip3_ports_define(ports, 3, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 3) == 0);
	CHECK(st_rip3_ports_viewport(ports, &offview) == 0);
	for (int32_t y = 0; y < 3; y++) {
		for (int32_t x = 0; x < 3; x++)
			CHECK(st_rip3_ports_plot(ports, x, y, (uint8_t)(21 + y * 3 + x), ST_RIP3_COPY) == 0);
	}
	CHECK(st_rip3_ports_protect(ports, 3, true) == 0);
	CHECK(st_rip3_ports_info(ports, 3, &before[2]) == 0);
	CHECK(st_rip3_ports_switch(ports, 0) == 0);
	CHECK(st_rip3_ports_erase(ports, 0) == 0);
	/* Independent offscreen and onscreen buffers need no overlap allocation. */
	st_rip3_copy_test_fail_malloc = true;
	CHECK(st_rip3_ports_blit(ports, 3, &from, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_copy_test_fail_malloc);
	st_rip3_copy_test_fail_malloc = false;
	CHECK(capture(ports, offexpected, 12, 22, 4, 4) == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		/* Destination clipping leaves only the lower-right source pixel. */
		const uint8_t corner[] = {29, 23, 31, 8, 226};
		uint8_t       expected[16];
		memset(expected, 10, sizeof(expected));
		expected[0] = corner[mode];
		CHECK(st_rip3_ports_switch(ports, 2) == 0);
		CHECK(st_rip3_ports_erase(ports, 10) == 0);
		CHECK(st_rip3_ports_blit(ports, 3, NULL, 2, -2, -2, (enum st_rip3_transfer_mode)mode) == 0);
		CHECK(capture(ports, expected, 12, 22, 4, 4) == 0);
	}
	/* No port definition, protection or selection changes during content transfer. */
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 2);
	for (unsigned i = 1; i <= 3; i++) {
		CHECK(st_rip3_ports_info(ports, i, &after) == 0);
		CHECK(same_info(&after, &before[i - 1]));
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
port_boundaries(enum st_rip3_canvas canvas)
{
	struct st_rip3_ports     *ports = NULL;
	struct st_rip3_port_info  screen;
	const struct st_rip3_rect bitmap = {0, 0, 3, 3}, bounds = {10, 20, 14, 24};
	const struct st_rip3_rect view = {1, 1, 3, 3}, point = {0, 0, 0, 0};
	const struct st_rip3_rect extreme = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
	const struct st_rip3_rect invalid = {1, 1, 0, 0}, external = {4, 4, 5, 5};
	const uint8_t             original[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	uint8_t                   value;
	unsigned                  current;

	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_ports_info(ports, 0, &screen) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bitmap, true) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &bounds, false) == 0);
	for (unsigned i = 1; i <= 2; i++) {
		CHECK(st_rip3_ports_switch(ports, i) == 0);
		CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	}
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (int32_t y = 0; y < 3; y++) {
		for (int32_t x = 0; x < 3; x++)
			CHECK(st_rip3_ports_plot(ports, x, y, (uint8_t)(1 + y * 3 + x), ST_RIP3_COPY) == 0);
	}
	/* Extreme pre-clipping origins cancel without narrowing or overflow. */
	CHECK(st_rip3_ports_blit(ports, 1, &extreme, 2, INT32_MIN, INT32_MIN, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(capture(ports, original, 11, 21, 3, 3) == 0);
	st_rip3_copy_test_fail_malloc = true;
	CHECK(st_rip3_ports_blit(ports, 2, &extreme, 2, INT32_MAX, INT32_MAX, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 2, &external, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 2, NULL, 2, INT32_MIN, INT32_MIN, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 1, &external, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 35, NULL, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 2, NULL, 35, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_blit(ports, 2, NULL, 1, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_copy_test_fail_malloc);
	st_rip3_copy_test_fail_malloc = false;
	CHECK(st_rip3_ports_blit(NULL, 0, NULL, 0, 0, 0, ST_RIP3_TRANSFER_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_blit(ports, 1, &invalid, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_blit(ports, 36, NULL, 2, 0, 0, ST_RIP3_TRANSFER_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_blit(ports, 1, NULL, 36, 0, 0, ST_RIP3_TRANSFER_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 0, 0, (enum st_rip3_transfer_mode) - 1)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 0, 0, (enum st_rip3_transfer_mode)5)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(capture(ports, original, 11, 21, 3, 3) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 2);
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	/* The active one-pixel device viewport at (0,0) is a real source. */
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &point) == 0);
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 200, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 1, NULL, 2, 2, 2, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 3, 3, &value) == 0 && value == 200);
	/* Screen-to-offscreen transfer writes through protected content too. */
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 17, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_blit(ports, 2, NULL, 1, -2, -2, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 200);
	CHECK(
	    st_rip3_ports_blit(ports, 1, NULL, 0, screen.bounds.x1, screen.bounds.y1, ST_RIP3_TRANSFER_COPY) == 0);
	CHECK(st_rip3_ports_read(ports, 0, screen.bounds.x1, screen.bounds.y1, &value) == 0 && value == 200);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(clipping() == 0);
	CHECK(unchanged() == 0);
	CHECK(overlap() == 0);
	for (enum st_rip3_canvas canvas = ST_RIP3_CANVAS_640_480; canvas <= ST_RIP3_CANVAS_1024_768; canvas++) {
		CHECK(ports_test(canvas) == 0);
		CHECK(port_boundaries(canvas) == 0);
	}
	return 0;
}
