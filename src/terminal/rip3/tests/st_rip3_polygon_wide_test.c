/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_polygon_internal.h"
#include "st_rip3_ports.h"

extern unsigned st_rip3_polygon_test_malloc_countdown;
extern bool     st_rip3_drawing_test_fail_calloc;
int32_t st_rip3_polygon_test_boundary(int64_t ax, int64_t ay, int64_t bx, int64_t by, int32_t y, int32_t limit);

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static const char *const border_map[] = {
    "........",
    ".FBFBFB.",
    ".B....F.",
    ".F....B.",
    ".B....F.",
    ".F....B.",
    ".BFBFBF.",
    "........",
};

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	const uint8_t             foreground[] = {5, 0x50, 0x55, 5, 0xaa}, background[] = {3, 0x56, 0x57, 1, 0xaa};
	struct st_rip3_ports     *ports = NULL;
	struct st_rip3_fill       fill;
	struct st_rip3_palette    palette;
	struct st_pixel_buffer   *capture = NULL;
	const struct st_rip3_rect bounds = {10, 20, 79, 89}, view = {2, 3, 53, 34};
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0 && st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	const unsigned             rows[] = {0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55};
	const struct st_rip3_point origin = {1, 2};
	CHECK(st_rip3_fill_custom(&fill, rows, &origin) == 0);
	for (unsigned row = 0; row < 3; row++)
		for (unsigned mode = 0; mode < 5; mode++) {
			const struct st_rip3_vector points[] = {
			    {mode * 10 + 1, row * 10 + 1},
                            {mode * 10 + 6, row * 10 + 1},
			    {mode * 10 + 6, row * 10 + 6},
                            {mode * 10 + 1, row * 10 + 6}
                        };
			const struct st_rip3_rect underlay = {(int32_t)mode * 10, (int32_t)row * 10,
			    (int32_t)mode * 10 + 7, (int32_t)row * 10 + 7};
			const struct st_rip3_pen  pen      = {.pattern = row == 1 ? 0xaaaa : 0xffff,
			    .thickness                            = row == 2 ? 3 : 1,
			    .draw_background                      = true};
			CHECK(st_rip3_ports_fill(ports, &underlay, 0x55, ST_RIP3_COPY) == 0);
			CHECK(st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, row == 0 ? NULL : &pen, 12,
				  (enum st_rip3_write_mode)mode)
			      == 0);
		}
	for (unsigned i = 0; i < 256; i++)
		palette.rgb[i] = i * UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			if (x >= 12 && x <= 59 && y >= 23 && y <= 50) {
				unsigned col = (x - 12) / 10, row = (y - 23) / 10, u = (x - 12) % 10,
					 v = (y - 23) % 10;
				if (col < 5 && row < 3 && u < 8 && v < 8) {
					expected = 0x55;
					if (u >= 1 && u < 6 && v >= 1 && v < 6)
						expected = (u + v) % 2 == 1 ? foreground[col] : background[col];
					if (row == 1 && border_map[v][u] != '.')
						expected = border_map[v][u] == 'F' ? 12 : 3;
					if (row == 2 && !(u >= 3 && u <= 4 && v >= 3 && v <= 4))
						expected = 12;
				}
			}
			CHECK(capture->primary[(size_t)y * width + x]
			      == (ST_PIXEL_COLOR_RGB_TAG | expected * UINT32_C(0x010101)));
		}
	CHECK(fill.origin.x == 1 && fill.origin.y == 2 && fill.rows[0] == 0xaa);
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
wide(void)
{
	/* Exact line x=y-1, with determinants requiring more than 64 bits.
	 * Splitting the edge at (0,1), reversing it, or doubling the entire loop
	 * must not create a one-pixel seam at sample centers on that line. */
	const double                m          = 1099511627903.0;
	const struct st_rip3_vector original[] = {
	    {-m, -m + 1},
            {m,  m + 1 },
            {m,  -m    }
        };
	const struct st_rip3_vector reversed[] = {
	    {m,  -m    },
            {m,  m + 1 },
            {-m, -m + 1}
        };
	const struct st_rip3_vector split[] = {
	    {-m, -m + 1},
            {0,  1     },
            {m,  m + 1 },
            {m,  -m    }
        };
	const struct st_rip3_vector twice[] = {
	    {-m, -m + 1},
            {m,  m + 1 },
            {m,  -m    },
            {-m, -m + 1},
            {m,  m + 1 },
            {m,  -m    }
        };
	const struct st_rip3_vector *const shapes[] = {original, reversed, split, twice};
	const size_t                       counts[] = {3, 3, 4, 6};
	struct st_rip3_ports              *ports    = NULL;
	struct st_rip3_fill                fill;
	const struct st_rip3_rect          view = {0, 0, 7, 7};
	uint8_t                            value;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0 && st_rip3_fill_solid(&fill, true) == 0);
	for (size_t shape = 0; shape < 4; shape++) {
		CHECK(st_rip3_ports_erase(ports, 0x55) == 0);
		CHECK(st_rip3_polygon_fill(ports, shapes[shape], counts[shape], &fill, 5, 3, NULL, 12, ST_RIP3_XOR)
		      == 0);
		for (int32_t y = 0; y < 8; y++)
			for (int32_t x = 0; x < 8; x++)
				CHECK(st_rip3_ports_read(ports, 0, x, y, &value) == 0
				      && value == (shape != 3 && x >= y - 1 ? 0x50 : 0x55));
	}
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
unchanged(struct st_rip3_ports *ports)
{
	uint8_t value;
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 0, x, y, &value) == 0 && value == 0x55);
	return 0;
}

static int
failures(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_fill   fill;
	struct st_rip3_vector points[] = {
	    {1, 1},
            {6, 1},
            {6, 6},
            {1, 6}
        };
	const struct st_rip3_pen  pen  = {.pattern = 0xaaaa, .thickness = 1, .draw_background = true};
	const struct st_rip3_rect view = {0, 0, 7, 7};
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0 && st_rip3_ports_erase(ports, 0x55) == 0);
	CHECK(st_rip3_fill_solid(&fill, true) == 0);
	for (unsigned stage = 1; stage <= 2; stage++) {
		st_rip3_polygon_test_malloc_countdown = stage;
		CHECK(st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, &pen, 12, ST_RIP3_XOR)
		      == IO_ERROR_OUT_OF_MEMORY);
		CHECK(st_rip3_polygon_test_malloc_countdown == 0 && unchanged(ports) == 0);
	}
	st_rip3_drawing_test_fail_calloc = true;
	CHECK(
	    st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, &pen, 12, ST_RIP3_XOR) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(!st_rip3_drawing_test_fail_calloc && unchanged(ports) == 0);
	CHECK(
	    st_rip3_polygon_fill(ports, points, 0, &fill, 5, 3, NULL, 12, ST_RIP3_COPY) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_polygon_fill(ports, points, 4097, &fill, 5, 3, NULL, 12, ST_RIP3_COPY)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_polygon_fill(ports, NULL, 4, &fill, 5, 3, NULL, 12, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_polygon_fill(ports, points, 4, NULL, 5, 3, NULL, 12, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	const double invalid[] = {NAN, INFINITY, 0.5, 1024.0 * INT32_MAX + 1, -1024.0 * INT32_MAX - 1};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		points[3].x = invalid[i];
		CHECK(st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, &pen, 12, ST_RIP3_COPY)
		      == IO_ERROR_OUT_OF_RANGE);
	}
	points[3].x                    = 1;
	struct st_rip3_pen invalid_pen = pen;
	invalid_pen.pattern            = 0;
	CHECK(st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, &invalid_pen, 12, ST_RIP3_COPY)
	      == IO_ERROR_INVALID_ARGUMENT);
	CHECK(unchanged(ports) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	st_rip3_polygon_test_malloc_countdown = 1;
	st_rip3_drawing_test_fail_calloc      = true;
	CHECK(st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, &pen, 12, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_polygon_test_malloc_countdown == 1 && st_rip3_drawing_test_fail_calloc);
	st_rip3_polygon_test_malloc_countdown = 0;
	st_rip3_drawing_test_fail_calloc      = false;
	CHECK(st_rip3_ports_activate(ports, true) == 0);
	CHECK(st_rip3_polygon_fill(ports, points, 4, &fill, 5, 3, &pen, 12, ST_RIP3_COPY) == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

/* Device-service degeneracies and limits are independent of wire-count policy. */
static int
kernel_counts(void)
{
	struct st_rip3_vector    *points = malloc(4096 * sizeof(*points));
	struct st_rip3_ports     *ports  = NULL;
	struct st_rip3_fill       fill;
	const struct st_rip3_pen  pen      = {.pattern = 0xffff, .thickness = 1};
	const struct st_rip3_rect view     = {0, 0, 7, 7};
	const size_t              counts[] = {1, 2, 4096};
	uint8_t                   value;
	CHECK(points != NULL && st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0 && st_rip3_fill_solid(&fill, true) == 0);
	for (size_t i = 0; i < 4096; i++)
		points[i] = (struct st_rip3_vector) {3, 3};
	st_rip3_polygon_test_malloc_countdown = 1;
	for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
		CHECK(st_rip3_ports_erase(ports, 0x55) == 0);
		CHECK(st_rip3_polygon_fill(ports, points, counts[i], &fill, 5, 3, &pen, 12, ST_RIP3_XOR) == 0);
		CHECK(st_rip3_polygon_test_malloc_countdown == 1);
		for (int32_t y = 0; y < 8; y++)
			for (int32_t x = 0; x < 8; x++)
				CHECK(st_rip3_ports_read(ports, 0, x, y, &value) == 0
				      && value == (x == 3 && y == 3 ? 12 : 0x55));
	}
	points[0] = (struct st_rip3_vector) {1, 1};
	points[1] = (struct st_rip3_vector) {6, 1};
	CHECK(st_rip3_ports_erase(ports, 0x55) == 0);
	CHECK(st_rip3_polygon_fill(ports, points, 2, &fill, 5, 3, &pen, 12, ST_RIP3_XOR) == 0);
	for (int32_t y = 0; y < 8; y++)
		for (int32_t x = 0; x < 8; x++)
			CHECK(st_rip3_ports_read(ports, 0, x, y, &value) == 0
			      && value == (y == 1 && x >= 1 && x <= 6 ? 12 : 0x55));
	CHECK(st_rip3_polygon_test_malloc_countdown == 1);
	st_rip3_polygon_test_malloc_countdown = 0;
	free(points);
	st_rip3_ports_destroy(ports);
	return 0;
}

/* Literal expectations derived independently with Python Fraction interpolation
 * at y+1/2, followed by ceil(x-1/2) and clipping. No emulator output is used. */
static int
crossings(void)
{
	static const struct {
		int64_t ax, ay, bx, by;
		int32_t y, limit, expected;
	} cases[] = {
	    {-1099511627905, -1099511627903, 1099511627901, 1099511627903, 0,          8,          0         },
	    {-1099511627905, -1099511627903, 1099511627901, 1099511627903, 4,          8,          2         },
	    {-1099511627905, -1099511627903, 1099511627901, 1099511627903, 2147483645, 2147483647, 2147483643},
	    {-1099511627904, -1099511627903, 1099511627902, 1099511627903, 0,          8,          0         },
	    {-1099511627904, -1099511627903, 1099511627902, 1099511627903, 4,          8,          3         },
	    {-1099511627904, -1099511627903, 1099511627902, 1099511627903, 2147483645, 2147483647, 2147483644},
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627903, 0,          8,          0         },
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627903, 4,          8,          4         },
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627903, 2147483645, 2147483647, 2147483645},
	    {-1099511627902, -1099511627903, 1099511627904, 1099511627903, 0,          8,          1         },
	    {-1099511627902, -1099511627903, 1099511627904, 1099511627903, 4,          8,          5         },
	    {-1099511627902, -1099511627903, 1099511627904, 1099511627903, 2147483645, 2147483647, 2147483646},
	    {-1099511627901, -1099511627903, 1099511627905, 1099511627903, 0,          8,          2         },
	    {-1099511627901, -1099511627903, 1099511627905, 1099511627903, 4,          8,          6         },
	    {-1099511627901, -1099511627903, 1099511627905, 1099511627903, 2147483645, 2147483647, 2147483647},
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627900, 4,          8,          6         },
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627902, 4,          8,          5         },
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627904, 4,          8,          4         },
	    {-1099511627903, -1099511627903, 1099511627903, 1099511627906, 4,          8,          3         },
	    {-4294967295,    -4294967294,    4294967295,    4294967296,    2147483645, 2147483647, 2147483644},
	    {-4294967297,    -4294967296,    4294967297,    4294967298,    2147483645, 2147483647, 2147483644},
	    {-2199023254528, -2199023254527, 2199023254528, 2199023254529, 2147483645, 2147483647, 2147483644},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CHECK(st_rip3_polygon_test_boundary(cases[i].ax, cases[i].ay, cases[i].bx, cases[i].by, cases[i].y,
			  cases[i].limit)
		      == cases[i].expected);
		CHECK(st_rip3_polygon_test_boundary(cases[i].bx, cases[i].by, cases[i].ax, cases[i].ay, cases[i].y,
			  cases[i].limit)
		      == cases[i].expected);
	}
	return 0;
}

int
main(void)
{
	CHECK(crossings() == 0);
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(wide() == 0);
	CHECK(failures() == 0);
	CHECK(kernel_counts() == 0);
	return 0;
}
