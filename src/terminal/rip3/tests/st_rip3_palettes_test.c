/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_palettes.h"
#include "io_error.h"
#include "st_pixels.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
slots(void)
{
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_palette_info info;
	struct st_rip3_palette palette, defaults;
	unsigned current = UINT_MAX;
	uint32_t color;

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 0);
	CHECK(st_rip3_palette_defaults(&defaults) == 0);
	for (unsigned slot = 0; slot < 36; slot++) {
		CHECK(st_rip3_palettes_info(palettes, slot, &info) == 0);
		CHECK(info.defined == (slot == 0) && !info.protected);
	}
	color = 0x123456;
	CHECK(st_rip3_palettes_rgb(palettes, 0, 8, &color, 1) == 0);
	for (unsigned slot = 1; slot < 36; slot++) {
		/* Undefined-slot protection is a no-op, and selection establishes
		 * defaults rather than copying the preceding slot's edited values. */
		CHECK(st_rip3_palettes_protect(palettes, slot, true) == 0);
		CHECK(st_rip3_palettes_info(palettes, slot, &info) == 0);
		CHECK(!info.defined && !info.protected);
		CHECK(st_rip3_palettes_switch(palettes, slot) == 0);
		CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == slot);
		CHECK(st_rip3_palettes_get(palettes, slot, &palette) == 0);
		CHECK(memcmp(&palette, &defaults, sizeof(palette)) == 0);
		color = 0x100000 + slot;
		CHECK(st_rip3_palettes_rgb(palettes, 0, 8, &color, 1) == 0);
	}
	for (unsigned slot = 0; slot < 36; slot++) {
		CHECK(st_rip3_palettes_get(palettes, slot, &palette) == 0);
		CHECK(palette.rgb[0] == (slot == 0 ? 0x123456 : 0x100000 + slot));
		CHECK(palette.rgb[1] == 0x0000aa && palette.rgb[255] == 0xffffff);
	}
	CHECK(st_rip3_palettes_switch(palettes, 0) == 0);
	CHECK(st_rip3_palettes_defaults(palettes) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0);
	CHECK(memcmp(&palette, &defaults, sizeof(palette)) == 0);
	st_rip3_palettes_destroy(palettes);
	st_rip3_palettes_destroy(NULL);
	return 0;
}

static int
protection_and_errors(void)
{
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_palettes *sentinel = NULL;
	struct st_rip3_palette_info info = {true,true};
	struct st_rip3_palette palette, previous;
	const uint32_t colors[] = {0x123456,0x654321};
	const uint32_t invalid[] = {0x000000,0x1000000};
	const unsigned masters[] = {4,63};
	const unsigned bad_masters[] = {64,1};
	unsigned current = 77;
	uint8_t index = 77;

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_clone(&sentinel, NULL) == IO_ERROR_INVALID_ARGUMENT && sentinel == NULL);
	CHECK(st_rip3_palettes_current(NULL, &current) == IO_ERROR_INVALID_ARGUMENT && current == 77);
	CHECK(st_rip3_palettes_switch(palettes, UINT_MAX) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 0);
	CHECK(st_rip3_palettes_info(palettes, 36, &info) == IO_ERROR_OUT_OF_RANGE);
	CHECK(info.defined && info.protected);
	CHECK(st_rip3_palettes_protect(palettes, 0, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_protect(palettes, 36, false) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0);
	previous = palette;
	CHECK(st_rip3_palettes_get(palettes, 1, &palette) == IO_ERROR_NOT_FOUND);
	CHECK(memcmp(&palette, &previous, sizeof(palette)) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 255, 8, colors, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_rgb(palettes, 0, 8, invalid, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_rgb(palettes, 0, 6, colors, 2) == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_rip3_palettes_desktop(palettes, 0, bad_masters, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0);
	CHECK(memcmp(&palette, &previous, sizeof(palette)) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 35) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 40, 8, colors, 2) == 0);
	CHECK(st_rip3_palettes_color(palettes, true, colors[0], &index) == 0 && index == 40);
	CHECK(st_rip3_palettes_color(palettes, false, 200, &index) == 0 && index == 200);
	CHECK(st_rip3_palettes_color(palettes, false, 256, &index) == IO_ERROR_OUT_OF_RANGE && index == 200);
	CHECK(st_rip3_palettes_get(palettes, 35, &previous) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 35, true) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 40, 8, colors + 1, 1) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_palettes_desktop(palettes, 0, masters, 2) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_palettes_defaults(palettes) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_palettes_get(palettes, 35, &palette) == 0);
	CHECK(memcmp(&palette, &previous, sizeof(palette)) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 35, false) == 0);
	CHECK(st_rip3_palettes_desktop(palettes, 0, masters, 2) == 0);
	CHECK(st_rip3_palettes_get(palettes, 35, &palette) == 0);
	CHECK(palette.rgb[0] == 0xa80000 && palette.rgb[1] == 0xffffff);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

static int
backups(void)
{
	struct st_rip3_palettes *palettes = NULL, *saved = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_palette_info info;
	const uint32_t red = 0xff0000, green = 0x00ff00, blue = 0x0000ff;
	unsigned current;

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 1, 8, &red, 1) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 7) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 1, 8, &green, 1) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 7, true) == 0);
	CHECK(st_rip3_palettes_clone(&saved, palettes) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 7, false) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 1, 8, &blue, 1) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 7, true) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 2) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 1, 8, &blue, 1) == 0);
	CHECK(st_rip3_palettes_protect(palettes, 2, true) == 0);
	CHECK(st_rip3_palettes_get(saved, 7, &palette) == 0 && palette.rgb[1] == green);
	CHECK(st_rip3_palettes_restore(palettes, saved) == 0);
	CHECK(st_rip3_palettes_current(palettes, &current) == 0 && current == 7);
	CHECK(st_rip3_palettes_get(palettes, 7, &palette) == 0 && palette.rgb[1] == green);
	CHECK(st_rip3_palettes_info(palettes, 7, &info) == 0 && info.defined && info.protected);
	CHECK(st_rip3_palettes_info(palettes, 2, &info) == 0 && !info.defined && !info.protected);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0 && palette.rgb[1] == red);
	/* Snapshot storage has independent ownership and survives repeated restore. */
	CHECK(st_rip3_palettes_switch(palettes, 0) == 0);
	CHECK(st_rip3_palettes_defaults(palettes) == 0);
	CHECK(st_rip3_palettes_restore(palettes, saved) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0 && palette.rgb[1] == red);
	CHECK(st_rip3_palettes_restore(palettes, palettes) == 0);
	st_rip3_palettes_destroy(saved);
	CHECK(st_rip3_palettes_get(palettes, 7, &palette) == 0 && palette.rgb[1] == green);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

static int
cycling(void)
{
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_surface *surface = NULL;
	struct st_pixel_buffer *capture = NULL, *original = NULL;
	struct st_rip3_palette palette;
	const uint32_t colors[] = {0x120000,0x003400,0x000056};
	const uint32_t expected[] = {
		ST_PIXEL_COLOR_RGB_VALUE(0x12,0,0), ST_PIXEL_COLOR_RGB_VALUE(0,0x34,0),
		ST_PIXEL_COLOR_RGB_VALUE(0,0,0x56), ST_PIXEL_COLOR_RGB_VALUE(0x12,0,0),
	};

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_surface_create(&surface, 4, 1) == 0);
	CHECK(st_rip3_surface_plot(surface, NULL, 1, 0, 1, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_plot(surface, NULL, 2, 0, 2, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palettes_get(palettes, 0, &palette) == 0);
	CHECK(st_rip3_surface_snapshot(surface, &palette, &original) == 0);
	CHECK(st_rip3_palettes_switch(palettes, 1) == 0);
	CHECK(st_rip3_palettes_rgb(palettes, 0, 8, colors, 3) == 0);
	CHECK(st_rip3_palettes_get(palettes, 1, &palette) == 0);
	CHECK(st_rip3_surface_snapshot(surface, &palette, &capture) == 0);
	for (size_t x = 0; x < 4; x++) {
		uint8_t index;

		CHECK(capture->primary[x] == expected[x]);
		CHECK(st_rip3_surface_get(surface, (int32_t)x, 0, &index) == 0);
		CHECK(index == (x == 3 ? 0 : x));
	}
	CHECK(original->primary[0] == ST_PIXEL_COLOR_RGB_TAG);
	CHECK(original->primary[1] == ST_PIXEL_COLOR_RGB_VALUE(0,0,170));
	CHECK(original->primary[2] == ST_PIXEL_COLOR_RGB_VALUE(0,170,0));
	CHECK(original->primary[3] == ST_PIXEL_COLOR_RGB_TAG);
	st_pixel_buffer_destroy(original);
	st_pixel_buffer_destroy(capture);
	st_rip3_surface_destroy(surface);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

int
main(void)
{
	CHECK(slots() == 0);
	CHECK(protection_and_errors() == 0);
	CHECK(backups() == 0);
	CHECK(cycling() == 0);
	return 0;
}
