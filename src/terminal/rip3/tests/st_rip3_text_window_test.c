/* Authored coordinate/cell expectations, independently of the layout service. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_maf.h"
#include "st_rip3_surface.h"
#include "st_rip3_text_window.h"

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)

static int
extended(void)
{
	const struct st_rip3_text_cell cells[5] = {
	    {12, 17},
            {11, 17},
            {12, 30},
            {11, 30},
            {20, 24}
        };

	const struct {
		uint32_t x0, y0, x1, y1, columns, rows;
		unsigned font, flags, selected, c, r, x, y, bw, bh;
	} cases[] = {
	    {10, 20, 1010, 620, 40, 25, 63, 8, 4, 40, 25, 110, 20, 1000, 600},
	    {0,  0,  800,  500, 40, 25, 0,  8, 0, 40, 25, 160, 37, 800,  500},
	    {20, 30, 21,   31,  1,  1,  4,  8, 0, 1,  1,  20,  30, 12,   17 },
	    {0,  0,  800,  500, 40, 25, 2,  0, 0, 40, 25, 160, 37, 800,  500},
	    {0,  0,  100,  100, 9,  5,  0,  0, 1, 9,  5,  0,   7,  100,  100},
	    {0,  0,  100,  100, 10, 6,  0,  0, 0, 8,  5,  2,   7,  100,  100},
	    {0,  0,  115,  80,  10, 4,  2,  0, 1, 10, 4,  2,   6,  115,  80 },
	    {0,  0,  150,  130, 10, 4,  4,  0, 2, 10, 4,  15,  5,  150,  130},
	    {0,  0,  115,  130, 10, 4,  2,  0, 3, 10, 4,  2,   5,  115,  130},
	    {0,  0,  115,  90,  10, 4,  3,  0, 1, 10, 4,  2,   11, 115,  90 },
	    {0,  0,  100,  90,  10, 4,  3,  0, 3, 9,  3,  0,   0,  100,  90 },
            {0,  0,  5,    5,   1,  1,  1,  0, 1, 1,  1,  0,   0,  11,   17 }
        };
	struct st_rip3_frame frame;
	CHECK(st_rip3_frame_init_device(&frame, 1024, 768) == 0 && st_rip3_frame_world(&frame, 1024, 768) == 0);
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		struct st_rip3_text_window_layout layout;
		CHECK(st_rip3_text_window_extended(&layout, &frame, cells, cases[i].x0, cases[i].y0, cases[i].x1,
			  cases[i].y1, cases[i].columns, cases[i].rows, cases[i].font, cases[i].flags)
		      == 0);
		CHECK(layout.extended && layout.font == cases[i].selected && layout.columns == cases[i].c
		      && layout.rows == cases[i].r && layout.x == cases[i].x && layout.y == cases[i].y
		      && layout.bound_width == cases[i].bw && layout.bound_height == cases[i].bh
		      && layout.cell_width == cells[layout.font].width
		      && layout.cell_height == cells[layout.font].height);
		CHECK(!st_rip3_text_window_same(&layout, &layout));
	}
	struct st_rip3_text_window_layout layout;
	CHECK(st_rip3_frame_world(&frame, 1, 1) == 0);
	CHECK(st_rip3_text_window_extended(&layout, &frame, cells, 16777216, 0, 16777217, 1, 1, 1, 4, 0) == 0
	      && layout.bound_x == UINT64_C(17179869184) && layout.x == UINT64_C(17179869686) && layout.y == 372);
	struct st_rip3_rect bounds;
	CHECK(st_rip3_text_window_bounds(&layout, 1024, 768, &bounds) == 0 && bounds.x0 == 0 && bounds.x1 == -1);
	CHECK(st_rip3_frame_init_device(&frame, 640, 480) == 0 && st_rip3_frame_world(&frame, 7, 7) == 0);
	const struct st_rip3_text_cell small[5] = {
	    {8,  11},
            {7,  11},
            {8,  19},
            {7,  19},
            {16, 19}
        };
	CHECK(st_rip3_text_window_extended(&layout, &frame, small, 6, 6, 1, 1, 10, 10, 0, 8) == 0
	      && layout.bound_x == 91 && layout.bound_y == 68 && layout.bound_width == 457
	      && layout.bound_height == 343 && layout.x == 239 && layout.y == 144 && layout.font == 4);
	CHECK(st_rip3_text_window_bounds(&layout, 640, 480, &bounds) == 0 && bounds.x0 == 91 && bounds.y0 == 68
	      && bounds.x1 == 547 && bounds.y1 == 410);
	struct st_rip3_text_window_layout saved = layout;
	CHECK(st_rip3_text_window_extended(&layout, &frame, small, 1, 1, 6, 6, 0, 10, 0, 0)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_text_window_extended(&layout, &frame, small, 1, 1, 6, 6, 10, 10, 5, 0)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_text_window_extended(&layout, &frame, small, 1, 1, 6, 6, 10, 10, 0, 64)
	      == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);
	struct st_rip3_text_cell missing[5];
	memcpy(missing, small, sizeof(missing));
	missing[0].width = 0;
	CHECK(st_rip3_text_window_extended(&layout, &frame, missing, 1, 1, 6, 6, 10, 10, 0, 0)
	      == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_rip3_text_window_extended(&layout, &frame, missing, 1, 1, 6, 6, 1, 1, 4, 0) == 0
	      && layout.font == 4);
	CHECK(st_rip3_frame_world(&frame, 65535, 65535) == 0);
	saved = layout;
	CHECK(
	    st_rip3_text_window_extended(&layout, &frame, small, 1, 1, 2, 2, 1, 1, 0, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);
	frame.world_width = 0;
	CHECK(st_rip3_text_window_extended(&layout, &frame, small, 1, 1, 6, 6, 1, 1, 0, 0) != 0);
	CHECK(st_rip3_text_window_normal(&layout, &frame, 10, 5, 12, 7, 1, 0) == 0 && layout.x == 80
	      && layout.y == 54 && layout.columns == 3 && layout.rows == 3);
	return 0;
}

int
main(void)
{
	CHECK(extended() == 0);
	for (unsigned font = 0; font < 5; font++) {
		struct st_rip3_maf_font loaded;
		const unsigned          width[] = {12, 11, 12, 11, 20}, height[] = {17, 17, 30, 30, 24};
		CHECK(st_rip3_maf_builtin_font(&loaded, 1024, 768, (enum st_rip3_maf_mode)font) == 0
		      && loaded.width == width[font] && loaded.height == height[font]);
		CHECK(st_rip3_maf_builtin_font(&loaded, 720, 400, (enum st_rip3_maf_mode)font) == 0);
		const unsigned fallback_width[] = {8, 7, 8, 7, 16}, fallback_height[] = {11, 11, 19, 19, 19};
		CHECK(loaded.width == fallback_width[font] && loaded.height == fallback_height[font]);
	}

	/* Inclusive two-axis ranges in the original font initializer. */
	static const struct {
		unsigned w, h, cw, ch;
	} devices[] = {
	    {560,  430, 7,  10},
            {639,  479, 7,  10},
            {620,  460, 7,  10},
            {640,  516, 9,  12},
            {799,  599, 9,  12},
	    {792,  592, 9,  12},
            {880,  688, 12, 16},
            {1023, 767, 12, 16},
            {559,  450, 0,  0 },
            {620,  480, 0,  0 },
	    {640,  479, 0,  0 },
            {799,  515, 0,  0 },
            {800,  599, 0,  0 },
            {879,  700, 0,  0 },
            {1023, 768, 0,  0 }
        };

	for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
		struct st_rip3_maf_font strike;
		int status = st_rip3_maf_device_font(&strike, devices[i].w, devices[i].h, ST_RIP3_MAF_80X43);
		if (devices[i].cw == 0)
			CHECK(status == IO_ERROR_NOT_FOUND);
		else
			CHECK(status == 0 && strike.width == devices[i].cw && strike.height == devices[i].ch);
	}

	static const struct {
		unsigned width, height, font, x, y, cw, ch;
	} cases[] = {
	    {640,  480, 0, 80,  54,  8,  11},
            {800,  600, 0, 100, 68,  10, 13},
            {1024, 768, 0, 128, 87,  12, 17},
	    {640,  480, 2, 80,  96,  8,  19},
            {800,  600, 2, 100, 120, 10, 24},
            {1024, 768, 2, 128, 153, 12, 30},
	    {640,  350, 1, 70,  40,  7,  8 },
            {720,  400, 1, 78,  45,  7,  9 },
            {1280, 800, 4, 320, 160, 32, 32},
	    {640,  480, 3, 70,  96,  7,  19},
            {800,  600, 3, 87,  120, 8,  24},
            {1024, 768, 3, 112, 153, 11, 30}
        };

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct st_rip3_frame frame;
		CHECK(st_rip3_frame_init_device(&frame, cases[i].width, cases[i].height) == 0);
		/* WORLD changes do not move an ordinary text window. */
		CHECK(st_rip3_frame_world(&frame, 17, 29) == 0);
		struct st_rip3_text_window_layout layout;
		CHECK(st_rip3_text_window_normal(&layout, &frame, 10, 5, 20, 10, 1, cases[i].font) == 0);
		CHECK(layout.x == cases[i].x && layout.y == cases[i].y && layout.columns == 11 && layout.rows == 6
		      && layout.cell_width == cases[i].cw && layout.cell_height == cases[i].ch && layout.wrap);
		struct st_rip3_text_window_layout saved = layout, other = layout;
		other.wrap = false;
		CHECK(st_rip3_text_window_same(&layout, &other));
		other.x1++;
		CHECK(!st_rip3_text_window_same(&layout, &other));
		CHECK(st_rip3_text_window_normal(&layout, &frame, 20, 10, 10, 5, 1, cases[i].font) == 0);
		CHECK(
		    st_rip3_text_window_same(&layout, &saved) && layout.x == cases[i].x && layout.y == cases[i].y);
		saved = layout;
		CHECK(st_rip3_text_window_normal(&layout, &frame, 10, 5, 20, 10, 2, cases[i].font)
		      == IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);
	}
	struct st_rip3_frame frame;
	CHECK(st_rip3_frame_init_device(&frame, 640, 480) == 0);
	static const unsigned columns[] = {80, 91, 80, 91, 40}, rows[] = {43, 43, 25, 25, 25};
	for (unsigned font = 0; font < 5; font++) {
		struct st_rip3_text_window_layout layout;
		CHECK(st_rip3_text_window_normal(&layout, &frame, columns[font] - 1, rows[font] - 1,
			  columns[font] - 1, rows[font] - 1, 1, font)
		          == 0
		      && layout.columns == 1 && layout.rows == 1);
		CHECK(st_rip3_text_window_normal(&layout, &frame, columns[font], rows[font], 0, 0, 1, font) == 0
		      && layout.x0 == 0 && layout.y0 == 0 && layout.x1 == columns[font] - 1
		      && layout.y1 == rows[font] - 1 && layout.columns == columns[font]
		      && layout.rows == rows[font]);
		CHECK(st_rip3_text_window_normal(&layout, &frame, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
			  0, font)
		          == 0
		      && layout.x0 == columns[font] - 1 && layout.y0 == rows[font] - 1 && layout.columns == 1
		      && layout.rows == 1);
		struct st_rip3_text_window_layout saved = layout;
		CHECK(st_rip3_text_window_normal(&layout, &frame, 0, 0, 20, 10, 1, 5) == IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);
	}
	CHECK(st_rip3_frame_init_device(&frame, 640, 350) == 0);
	struct st_rip3_text_window_layout example;
	CHECK(st_rip3_text_window_normal(&example, &frame, 10, 15, 20, 25, 1, 2) == 0 && example.x0 == 10
	      && example.y0 == 15 && example.x1 == 20 && example.y1 == 24 && example.x == 80 && example.y == 210
	      && example.columns == 11 && example.rows == 10 && example.cell_width == 8
	      && example.cell_height == 14);
	CHECK(!st_rip3_text_window_same(NULL, NULL));
	return 0;
}
