/* Authored seed/phase rasters; the original caller wallpapers the raw record. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_image.h"
#include "st_rip3_ports.h"

extern bool     st_rip3_image_test_fail_allocate, st_rip3_wallpaper_test_fail_allocate;
extern unsigned st_rip3_image_test_fills;

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

struct fixture {
	struct st_rip3_ports  *ports;
	struct st_rip3_palette palette;
	struct st_pixel_buffer image;
	uint32_t               source[2];
	bool                   offscreen;
	uint32_t               width, height;
};

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	const struct st_rip3_rect screen = {10, 20, 29, 43}, view = {3, 5, 14, 18}, bitmap = {0, 0, 11, 13};
	f->width     = width;
	f->height    = height;
	f->offscreen = offscreen;
	f->source[0] = 0x80404040;
	f->source[1] = 0x80c0c0c0;
	f->image     = (struct st_pixel_buffer) {f->source, NULL, 2, 1};
	for (unsigned i = 0; i < 256; i++)
		f->palette.rgb[i] = i * UINT32_C(0x010101);
	CHECK(st_rip3_ports_create(&f->ports, canvas) == 0);
	CHECK(st_rip3_ports_define(f->ports, 1, offscreen ? &bitmap : &screen, offscreen) == 0
	      && st_rip3_ports_switch(f->ports, 1) == 0);
	if (!offscreen)
		CHECK(st_rip3_ports_viewport(f->ports, &view) == 0);
	CHECK(st_rip3_ports_protect(f->ports, 1, true) == 0);
	return 0;
}

/* One complete capture contains the source viewport, untouched margins and
 * its palette. Record/selection checks happen after synchronous drawing ends. */
static int
capture(struct fixture *f, unsigned flags, bool wallpaper, bool drawn)
{
	struct st_rip3_bitmap *image = NULL;
	CHECK(st_rip3_bitmap_capture(&image, f->ports, f->offscreen ? 1 : 0, NULL, &f->palette) == 0);
	uint32_t width = f->offscreen ? 12 : f->width, height = f->offscreen ? 14 : f->height;
	CHECK(image->pixels->width == width && image->pixels->height == height);
	/* Stored (2,3,7,8) is a 5x5 tile. Initial JPEG rendering consumes two
	 * far units: a 4x4 target, or 4x2 aspect fit. Its extra last column/row
	 * and aspect margins remain background and must be repeated too. */
	const uint8_t plain[12]         = {192, 9, 64, 64, 192, 192, 9, 64, 64, 192, 192, 9};
	const uint8_t shifted[12]       = {64, 64, 192, 192, 9, 64, 64, 192, 192, 9, 64, 64};
	const bool    ordinary_rows[14] = {1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1};
	const bool    aspect_rows[14]   = {1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0};
	const bool    odd_rows[14]      = {1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0};
	for (uint32_t y = 0; y < height; y++)
		for (uint32_t x = 0; x < width; x++) {
			unsigned expected = 0;
			bool     inside   = f->offscreen || (x >= 13 && x <= 24 && y >= 25 && y <= 38);
			if (inside) {
				uint32_t vx = f->offscreen ? x : x - 13, vy = f->offscreen ? y : y - 25;
				expected = 9;
				if (drawn && wallpaper) {
					bool row = (flags & 1) != 0 ? aspect_rows[vy] : ordinary_rows[vy];
					if (row)
						expected =
						    (flags & 32) != 0 && odd_rows[vy] ? shifted[vx] : plain[vx];
				}
				else if (drawn && vx >= 2 && vx <= 5 && vy >= ((flags & 1) != 0 ? 4u : 3u)
				         && vy <= ((flags & 1) != 0 ? 5u : 6u))
					expected = vx < 4 ? 64 : 192;
			}
			CHECK(image->pixels->primary[(size_t)y * width + x] == expected);
		}
	for (unsigned i = 0; i < 256; i++)
		CHECK(image->palette[i] == i * UINT32_C(0x010101));
	CHECK(f->source[0] == 0x80404040 && f->source[1] == 0x80c0c0c0);
	unsigned                 current;
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_current(f->ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(f->ports, 1, &info) == 0 && info.protected && info.viewport_active
	      && info.image.flags == flags && info.image.area.x0 == 2 && info.image.area.y0 == 3
	      && info.image.area.x1 == 7 && info.image.area.y1 == 8);
	st_rip3_bitmap_destroy(image);
	return 0;
}

static int
scenes(enum st_rip3_canvas canvas, uint32_t width, uint32_t height, bool offscreen)
{
	struct fixture            f      = {0};
	const struct st_rip3_rect record = {2, 3, 7, 8};
	CHECK(create(&f, canvas, width, height, offscreen) == 0);
	for (unsigned flags = 16; flags < 64; flags++) {
		if ((flags & 16) == 0)
			continue;
		CHECK(st_rip3_ports_erase(f.ports, 9) == 0
		      && st_rip3_ports_image_style(f.ports, 1, &record, flags) == 0);
		st_rip3_image_test_fills = 0;
		CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == 0);
		CHECK(st_rip3_image_test_fills == ((flags & 4) == 0 ? 1u : 0u));
		CHECK(capture(&f, flags, true, true) == 0);
	}
	for (unsigned flags = 0; flags <= 32; flags += 32) {
		CHECK(st_rip3_ports_erase(f.ports, 9) == 0
		      && st_rip3_ports_image_style(f.ports, 1, &record, flags) == 0);
		st_rip3_wallpaper_test_fail_allocate = true;
		CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == 0
		      && st_rip3_wallpaper_test_fail_allocate);
		st_rip3_wallpaper_test_fail_allocate = false;
		CHECK(capture(&f, flags, false, true) == 0);
	}
	st_rip3_ports_destroy(f.ports);
	return 0;
}

static int
failures(void)
{
	struct fixture            f      = {0};
	const struct st_rip3_rect record = {2, 3, 7, 8}, zero = {0, 0, 0, 0};
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, true) == 0);
	CHECK(st_rip3_ports_erase(f.ports, 9) == 0 && st_rip3_ports_image_style(f.ports, 1, &record, 21) == 0);
	st_rip3_image_test_fail_allocate = st_rip3_wallpaper_test_fail_allocate = true;
	st_rip3_image_test_fills                                                = 0;
	CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == IO_ERROR_OUT_OF_MEMORY
	      && !st_rip3_image_test_fail_allocate && st_rip3_wallpaper_test_fail_allocate);
	CHECK(st_rip3_image_test_fills == 0 && capture(&f, 21, false, false) == 0);
	f.source[1] = 0x81000000;
	CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == IO_ERROR_INVALID_FORMAT
	      && st_rip3_wallpaper_test_fail_allocate);
	f.source[1] = 0x80c0c0c0;
	CHECK(capture(&f, 21, false, false) == 0);
	/* Wallpaper allocation fails after the valid initial fit has painted;
	 * that draw is retained and the next invocation can complete normally. */
	CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == IO_ERROR_OUT_OF_MEMORY
	      && !st_rip3_wallpaper_test_fail_allocate);
	CHECK(capture(&f, 21, false, true) == 0);
	CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == 0 && capture(&f, 21, true, true) == 0);
	CHECK(st_rip3_ports_image_style(f.ports, 1, &zero, 16) == 0);
	st_rip3_wallpaper_test_fail_allocate = true;
	st_rip3_image_test_fills             = 0;
	CHECK(st_rip3_draw_image(f.ports, NULL, NULL, 0, NULL) == 0 && st_rip3_wallpaper_test_fail_allocate
	      && st_rip3_image_test_fills == 0);
	CHECK(st_rip3_ports_protect(f.ports, 1, false) == 0 && st_rip3_ports_activate(f.ports, false) == 0
	      && st_rip3_ports_image_style(f.ports, 1, &record, 16) == 0);
	CHECK(st_rip3_draw_image(f.ports, NULL, NULL, 0, NULL) == 0 && st_rip3_wallpaper_test_fail_allocate);
	st_rip3_wallpaper_test_fail_allocate = false;
	st_rip3_ports_destroy(f.ports);
	return 0;
}

int
main(void)
{
	const enum st_rip3_canvas canvases[] = {ST_RIP3_CANVAS_640_480, ST_RIP3_CANVAS_800_600,
	    ST_RIP3_CANVAS_1024_768};

	const uint32_t widths[] = {640, 800, 1024}, heights[] = {480, 600, 768};
	for (size_t i = 0; i < 3; i++)
		for (unsigned offscreen = 0; offscreen < 2; offscreen++)
			CHECK(scenes(canvases[i], widths[i], heights[i], offscreen != 0) == 0);
	CHECK(failures() == 0);
	struct fixture f = {0};
	CHECK(create(&f, ST_RIP3_CANVAS_640_480, 640, 480, true) == 0);
	const struct st_rip3_rect single = {0, 0, 1, 1}, full = {0, 0, 12, 14};
	for (unsigned flags = 16; flags <= 48; flags += 32) {
		CHECK(st_rip3_ports_image_style(f.ports, 1, &single, flags) == 0);
		CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == 0);
		struct st_rip3_bitmap *image = NULL;
		CHECK(st_rip3_bitmap_capture(&image, f.ports, 1, NULL, &f.palette) == 0);
		for (size_t i = 0; i < 12 * 14; i++)
			CHECK(image->pixels->primary[i] == 64);
		st_rip3_bitmap_destroy(image);
		/* A seed covering the viewport needs no wallpaper allocation. Its
		 * retained last row/column still belong to the tile's raw boundary. */
		CHECK(st_rip3_ports_erase(f.ports, 9) == 0
		      && st_rip3_ports_image_style(f.ports, 1, &full, flags) == 0);
		st_rip3_wallpaper_test_fail_allocate = true;
		CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == 0
		      && st_rip3_wallpaper_test_fail_allocate);
		CHECK(st_rip3_bitmap_capture(&image, f.ports, 1, NULL, &f.palette) == 0);
		for (size_t y = 0; y < 14; y++)
			for (size_t x = 0; x < 12; x++)
				CHECK(image->pixels->primary[y * 12 + x]
				      == (y == 13 || x == 11 ? 9u
				          : x < 5            ? 64u
				                             : 192u));
		st_rip3_bitmap_destroy(image);
		st_rip3_wallpaper_test_fail_allocate = false;
	}
	/* The raw default marker is not replaced with a convenient fitted seed.
	 * Native rectangle reconciliation rejects it after the initial paint. */
	CHECK(st_rip3_ports_image_style(f.ports, 1, NULL, 16) == 0);
	st_rip3_wallpaper_test_fail_allocate = true;
	CHECK(st_rip3_draw_image(f.ports, &f.image, NULL, 0, &f.palette) == IO_ERROR_INVALID_ARGUMENT
	      && st_rip3_wallpaper_test_fail_allocate);
	st_rip3_wallpaper_test_fail_allocate = false;
	struct st_rip3_bitmap *image         = NULL;
	CHECK(st_rip3_bitmap_capture(&image, f.ports, 1, NULL, &f.palette) == 0);
	for (size_t y = 0; y < 14; y++)
		for (size_t x = 0; x < 12; x++)
			CHECK(image->pixels->primary[y * 12 + x] == (x < 6 ? 64u : 192u));
	st_rip3_bitmap_destroy(image);
	st_rip3_ports_destroy(f.ports);
	return 0;
}
