/* Independent literal image geometry, palette and coherent capture checks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_image.h"
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"
#include "st_rip3_bitmap.h"
#include "st_rip3_resources.h"
#include "st_pixel_image.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern unsigned st_rip3_image_test_fills;
extern struct st_rip3_rect st_rip3_image_test_fill_area;
extern uint8_t st_rip3_image_test_fill_color;
extern enum st_rip3_write_mode st_rip3_image_test_fill_mode;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
styles(void)
{
	struct st_rip3_ports *ports = NULL, *restored = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_port_info info;
	const struct st_rip3_rect port = {10,20,17,25};
	const struct st_rip3_rect view = {1,1,6,4};
	const struct st_rip3_rect area = {-5,-7,100,200};
	const struct st_rip3_rect zero = {0,0,0,0};
	const struct st_rip3_rect reversed = {1,0,0,1};
	unsigned current;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_info(ports, 0, &info) == 0);
	CHECK(info.image.automatic && info.image.enabled && info.image.flags == 0);
	CHECK(st_rip3_ports_define(ports, 1, &port, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_image_style(ports, 1, &area, 5) == 0);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0);
	CHECK(!info.image.automatic && info.image.enabled && info.image.flags == 5);
	CHECK(info.image.area.x0 == 0 && info.image.area.y0 == 0
	    && info.image.area.x1 == 5 && info.image.area.y1 == 3);
	CHECK(st_rip3_ports_image_style(ports, 0, &zero, 2) == 0);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(ports, 0, &info) == 0);
	CHECK(!info.image.automatic && !info.image.enabled && info.image.flags == 2);
	CHECK(st_rip3_ports_image_style(ports, 1, &reversed, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_image_style(ports, 1, NULL, 16) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_image_style(ports, 36, NULL, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_ports_image_style(ports, 2, NULL, 0) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_ports_image_style(NULL, 0, NULL, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_image_style(ports, 1, NULL, 0) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_ports_backup_create(&backup) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, ports) == 0);
	CHECK(st_rip3_ports_create(&restored, ST_RIP3_CANVAS_640_480) == 0);
	st_rip3_ports_destroy(ports);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, restored) == 0);
	CHECK(st_rip3_ports_info(restored, 0, &info) == 0 && !info.image.enabled);
	CHECK(st_rip3_ports_info(restored, 1, &info) == 0 && info.protected);
	CHECK(info.image.enabled && !info.image.automatic && info.image.flags == 5);
	CHECK(info.image.area.x1 == 5 && info.image.area.y1 == 3);
	CHECK(st_rip3_ports_protect(restored, 1, false) == 0);
	CHECK(st_rip3_ports_define(restored, 1, &port, false) == 0);
	CHECK(st_rip3_ports_info(restored, 1, &info) == 0);
	CHECK(info.image.enabled && info.image.automatic && info.image.flags == 0);
	CHECK(st_rip3_ports_image_style(restored, 0, NULL, 9) == 0);
	CHECK(st_rip3_ports_info(restored, 0, &info) == 0);
	CHECK(info.image.enabled && info.image.automatic && info.image.flags == 9);
	CHECK(st_rip3_ports_reset(restored, true) == 0);
	CHECK(st_rip3_ports_info(restored, 0, &info) == 0);
	CHECK(info.image.enabled && info.image.automatic && info.image.flags == 0);
	st_rip3_ports_destroy(restored);
	st_rip3_backup_destroy(backup);
	return 0;
}

static int
render(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	uint32_t rgb[] = {ST_PIXEL_COLOR_RGB_TAG | 0xff0000,
	    ST_PIXEL_COLOR_RGB_TAG | 0x00ff00};
	const struct st_pixel_buffer image = {rgb,NULL,2,1};
	const struct st_rip3_rect onscreen = {10,10,17,17};
	const struct st_rip3_rect view = {1,1,6,6};
	const struct st_rip3_rect square = {0,0,5,5};
	const struct st_rip3_rect tall = {0,0,0,9};
	const struct st_rip3_rect zero = {0,0,0,0};
	const struct st_rip3_rect offscreen = {0,0,5,5};
	uint8_t value;

	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &onscreen, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_erase(ports, 2) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_image_style(ports, 1, &square, 1) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_draw_image(ports, &image, NULL, 0, &palette) == 0);
	/* A second independent, taller-than-wide image lives offscreen. */
	CHECK(st_rip3_ports_define(ports, 2, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_ports_image_style(ports, 2, NULL, 1) == 0);
	uint32_t vertical_rgb[] = {ST_PIXEL_COLOR_RGB_TAG | 0x0000ff,
	    ST_PIXEL_COLOR_RGB_TAG | 0xffffff};
	const struct st_pixel_buffer vertical = {vertical_rgb,NULL,1,2};
	CHECK(st_rip3_draw_image(ports, &vertical, NULL, 0, &palette) == 0);
	for (int32_t y = 0; y < 6; y++) {
		for (int32_t x = 0; x < 6; x++) {
			CHECK(st_rip3_ports_read(ports, 2, x, y, &value) == 0);
			/* Default RGB cube blue (0,0,3) is entry 32+3. */
			CHECK(value == (x < 1 || x > 3 ? 9 : y < 3 ? 35 : 15));
		}
	}
	/* One coherent capture contains the entire related onscreen scene. */
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	CHECK(capture->width == width && capture->height == height);
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_TAG | 0x0000aa;
			if (x >= 10 && x <= 17 && y >= 10 && y <= 17)
				expected = ST_PIXEL_COLOR_RGB_TAG | 0x00aa00;
			if (x >= 11 && x <= 16 && y >= 11 && y <= 16) {
				if (y < 12 || y > 14)
					expected = ST_PIXEL_COLOR_RGB_TAG | 0x00aa00;
				else
					expected = ST_PIXEL_COLOR_RGB_TAG | (x < 14 ? 0xff0000 : 0x00ff00);
			}
			CHECK(capture->primary[(size_t)y * width + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	/* A very thin image fits to at least one pixel. The explicit internal
	 * inhibit state below is not wire 1i's all-zero rectangle producer. */
	CHECK(st_rip3_ports_image_style(ports, 2, &tall, 1) == 0);
	CHECK(st_rip3_draw_image(ports, &image, NULL, 0, &palette) == 0);
	for (int32_t y = 0; y < 6; y++) {
		CHECK(st_rip3_ports_read(ports, 2, 0, y, &value) == 0);
		/* A 1x1 fit samples the source center (green, RGB cube entry 60). */
		CHECK(value == (y == 2 ? 60 : 9));
	}
	CHECK(st_rip3_ports_image_style(ports, 2, &zero, 0) == 0);
	CHECK(st_rip3_draw_image(ports, NULL, NULL, 0, NULL) == 0);
	CHECK(st_rip3_ports_image_style(ports, 2, NULL, 0) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_draw_image(ports, NULL, NULL, 0, NULL) == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
all_pixels(struct st_rip3_ports *ports, uint8_t expected)
{
	for (int32_t y = 0; y < 4; y++) {
		for (int32_t x = 0; x < 8; x++) {
			uint8_t value;
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
		}
	}
	return 0;
}

static int
compositing(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette, before;
	const struct st_rip3_rect bounds = {0,0,7,3};
	const struct st_rip3_rect one_pixel = {0,0,0,0};
	const struct st_rip3_rect reduction = {1,1,2,1};
	const struct st_rip3_rect extreme = {INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX};
	uint32_t samples[] = {0,1,2};
	uint32_t colors[] = {0xff0000,0x00ff00,0};
	struct st_pixel_buffer image = {samples,NULL,3,1};
	const uint8_t stretched[] = {3,3,3,5,5,7,7,7};
	uint8_t value;

	for (size_t i = 0; i < 256; i++)
		palette.rgb[i] = 0x7f7f7f;
	palette.rgb[3] = 0xff0000;
	palette.rgb[5] = 0x00ff00;
	palette.rgb[7] = 0; /* Black is deliberately not index zero. */
	before = palette;
	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &bounds, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (unsigned flags = 0; flags <= 6; flags += 2) {
		CHECK(st_rip3_ports_erase(ports, 9) == 0);
		CHECK(st_rip3_ports_image_style(ports, 1, NULL, flags) == 0);
		st_rip3_image_test_fills = 0;
		CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == 0);
		CHECK(st_rip3_image_test_fills == ((flags&4) != 0 ? 0u : 1u));
		if ((flags&4) == 0) {
			CHECK(st_rip3_image_test_fill_color == 7 && st_rip3_image_test_fill_mode == ST_RIP3_COPY);
			CHECK(st_rip3_image_test_fill_area.x0 == 0 && st_rip3_image_test_fill_area.y0 == 0
			    && st_rip3_image_test_fill_area.x1 == 7 && st_rip3_image_test_fill_area.y1 == 3);
		}
		for (int32_t y = 0; y < 4; y++) {
			for (int32_t x = 0; x < 8; x++) {
				uint8_t source = stretched[x];
				uint8_t expected = source;
				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0 && value == expected);
			}
		}
	}
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_ports_image_style(ports, 1, NULL, 0) == 0);
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == 0);
	for (int32_t x = 0; x < 8; x++) {
		CHECK(st_rip3_ports_read(ports, 1, x, 0, &value) == 0);
		CHECK(value == stretched[x]);
	}
	for (unsigned flags = 1; flags <= 5; flags += 4) {
		CHECK(st_rip3_ports_erase(ports, 9) == 0);
		CHECK(st_rip3_ports_image_style(ports, 1, NULL, flags) == 0);
		st_rip3_image_test_fills = 0;
		CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == 0);
		CHECK(st_rip3_image_test_fills == (flags == 1 ? 1u : 0u));
		if (flags == 1) {
			CHECK(st_rip3_image_test_fill_color == 7 && st_rip3_image_test_fill_mode == ST_RIP3_COPY);
			CHECK(st_rip3_image_test_fill_area.x0 == 0 && st_rip3_image_test_fill_area.x1 == 7
			    && st_rip3_image_test_fill_area.y0 == 1 && st_rip3_image_test_fill_area.y1 == 2);
		}
		for (int32_t y = 0; y < 4; y++) {
			for (int32_t x = 0; x < 8; x++) {
				CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
				CHECK(value == (y == 0 || y == 3 ? 9 : stretched[x]));
			}
		}
	}
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_ports_image_style(ports, 1, &reduction, 0) == 0);
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == 0);
	for (int32_t y = 0; y < 4; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (y == 1 && x == 1 ? 3 : y == 1 && x == 2 ? 7 : 9));
		}
	}
	CHECK(st_rip3_ports_image_style(ports, 1, &extreme, 2) == 0);
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == 0);
	CHECK(memcmp(&palette, &before, sizeof(palette)) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	/* An invalid final sample cannot partially clear or paint the scene. */
	samples[2] = 3;
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_INVALID_FORMAT);
	CHECK(all_pixels(ports, 9) == 0);
	samples[2] = 0x81000000;
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_INVALID_FORMAT);
	samples[2] = 2;
	palette.rgb[255] = 0x1000000;
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_OUT_OF_RANGE);
	palette = before;
	colors[2] = 0x1000000;
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_OUT_OF_RANGE);
	colors[2] = 0;
	CHECK(st_rip3_draw_image(ports, &image, colors, 257, &palette) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_draw_image(ports, &image, NULL, 3, &palette) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_image(ports, NULL, colors, 3, &palette) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_draw_image(NULL, &image, colors, 3, &palette) == IO_ERROR_INVALID_ARGUMENT);
	image.width = UINT32_MAX;
	image.height = UINT32_MAX;
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_OUT_OF_RANGE);
	image.width = 0;
	image.height = 1;
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_INVALID_ARGUMENT);
	image.width = 3;
	CHECK(st_rip3_ports_image_style(ports, 1, NULL, 8) == 0);
	CHECK(st_rip3_draw_image(ports, &image, colors, 3, &palette) == IO_ERROR_NOT_SUPPORTED);
	CHECK(all_pixels(ports, 9) == 0);
	/* The JPEG/RGB path ignores palette-commit flag 8 and accepts no local palette. */
	for (size_t i = 0; i < 3; i++)
		samples[i] = ST_PIXEL_COLOR_RGB_TAG | colors[i];
	CHECK(st_rip3_draw_image(ports, &image, NULL, 0, &palette) == 0);
	CHECK(st_rip3_ports_read(ports, 1, 0, 0, &value) == 0 && value == 3);
	/* An existing explicit area is reclamped when the viewport shrinks;
	 * its origin is relative to the new viewport, never the underlying port. */
	const struct st_rip3_rect before_shrink = {1,1,6,3};
	const struct st_rip3_rect smaller_view = {2,1,5,2};
	CHECK(st_rip3_ports_image_style(ports, 1, &before_shrink, 0) == 0);
	CHECK(st_rip3_ports_erase(ports, 9) == 0);
	CHECK(st_rip3_ports_viewport(ports, &smaller_view) == 0);
	CHECK(st_rip3_draw_image(ports, &image, NULL, 0, &palette) == 0);
	for (int32_t y = 0; y < 4; y++) {
		for (int32_t x = 0; x < 8; x++) {
			CHECK(st_rip3_ports_read(ports, 1, x, y, &value) == 0);
			CHECK(value == (y == 2 && x == 3 ? 3 : y == 2 && x == 4 ? 5
			    : y == 2 && x == 5 ? 7 : 9));
		}
	}
	/* A one-pixel automatic area is enabled even though its bounds are zero. */
	CHECK(st_rip3_ports_define(ports, 2, &one_pixel, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_draw_image(ports, &image, NULL, 0, &palette) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &value) == 0 && value == 5);
	st_rip3_ports_destroy(ports);
	return 0;
}

static int
decoded_inputs(const char *jpeg_directory)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_bitmap *bitmap = NULL;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_resource_paths paths = {.shared = jpeg_directory};
	const struct st_rip3_rect areas[] = {{2,2,7,5},{10,2,12,10},{20,2,25,3}};
	const char *names[] = {"420.JPG", "GRAY.JPG"};
	bool jpeg_available = st_pixel_image_jpeg_supported();
	/* Literal 2x1 one-bit BMP: red, green; four-byte padded bottom-up row. */
	static const uint8_t bmp[66] = {
		[0]='B', [1]='M', [2]=66, [10]=62, [14]=40, [18]=2, [22]=1,
		[26]=1, [28]=1, [34]=4, [56]=255, [59]=255, [62]=0x40,
	};

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	for (size_t i = 0; i < 256; i++)
		palette.rgb[i] = 0x808080;
	palette.rgb[10] = 0xaa2d17; /* T.81 fixture's documented DC-only RGB. */
	palette.rgb[11] = 0x404040;
	palette.rgb[12] = 0xc0c0c0;
	palette.rgb[13] = 0xff0000;
	palette.rgb[14] = 0x00ff00;
	for (unsigned i = 0; i < 2; i++) {
		struct st_rip3_resource resource = {0};
		struct st_pixel_buffer *image = NULL;
		uint8_t bytes[1024];
		size_t size;

		CHECK(st_rip3_ports_define(ports, i + 1, &areas[i], false) == 0);
		CHECK(st_rip3_ports_switch(ports, i + 1) == 0);
		CHECK(st_rip3_resource_open(&resource, &paths, ST_RIP3_RESOURCE_MEDIA, names[i]) == 0);
		size = fread(bytes, 1, sizeof(bytes), resource.stream);
		CHECK(feof(resource.stream) && !ferror(resource.stream));
		st_rip3_resource_dispose(&resource);
		int status = st_pixel_image_decode_jpeg(&image, bytes, size);
		if (jpeg_available) {
			CHECK(status == 0 && image != NULL);
			CHECK(st_rip3_draw_image(ports, image, NULL, 0, &palette) == 0);
			st_pixel_buffer_destroy(image);
		}
		else
			CHECK(status == IO_ERROR_NOT_SUPPORTED && image == NULL);
	}
	CHECK(st_rip3_ports_define(ports, 3, &areas[2], false) == 0);
	CHECK(st_rip3_ports_switch(ports, 3) == 0);
	CHECK(st_rip3_bitmap_decode(&bitmap, bmp, sizeof(bmp)) == 0);
	CHECK(st_rip3_draw_image(ports, bitmap->pixels, bitmap->palette,
	    bitmap->palette_size, &palette) == 0);
	st_rip3_bitmap_destroy(bitmap);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			uint32_t expected = 0x808080;

			if (jpeg_available && x >= 2 && x <= 7 && y >= 2 && y <= 5)
				expected = 0xaa2d17;
			if (jpeg_available && x >= 10 && x <= 12 && y >= 2 && y <= 10)
				expected = y < 10 ? 0x404040 : 0xc0c0c0;
			if (x >= 20 && x <= 25 && y >= 2 && y <= 3)
				expected = x < 23 ? 0xff0000 : 0x00ff00;
			CHECK(capture->primary[(size_t)y * 640 + x] == (ST_PIXEL_COLOR_RGB_TAG | expected));
		}
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	printf("JPEG decode/render coverage: %s\n", jpeg_available ? "present" : "unavailable");
	return 0;
}

static int
measured_regions(enum st_rip3_canvas canvas)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	const struct st_rip3_rect bounds = {0,0,139,159};
	/* The reference's GDI rectangles have exclusive right/bottom edges:
	 * (10,20,110,120) fits to (10,45,110,95) or (35,20,85,120).
	 * This kernel's rectangles are inclusive, so subtract one at that
	 * representation boundary. Expected endpoints below are literal. */
	const struct st_rip3_rect area = {10,20,109,119};
	const struct st_rip3_rect expected[] = {{10,45,109,94},{35,20,84,119}};
	uint32_t pixels[50];
	for (unsigned i = 0; i < 50; i++) pixels[i] = ST_PIXEL_COLOR_RGB_VALUE(64,64,64);
	for (unsigned i = 0; i < 256; i++) palette.rgb[i] = 0xffffff;
	palette.rgb[5] = 0x404040; palette.rgb[7] = 0;
	CHECK(st_rip3_ports_create(&ports,canvas) == 0
	    && st_rip3_ports_define(ports,1,&bounds,true) == 0 && st_rip3_ports_switch(ports,1) == 0);
	for (unsigned aspect = 0; aspect < 2; aspect++) for (unsigned flags = 1; flags <= 5; flags += 4) {
		struct st_pixel_buffer image = {pixels,NULL,aspect == 0 ? 10u : 5u,aspect == 0 ? 5u : 10u};
		struct st_rip3_bitmap *capture = NULL;
		CHECK(st_rip3_ports_erase(ports,9) == 0 && st_rip3_ports_image_style(ports,1,&area,flags) == 0);
		st_rip3_image_test_fills = 0;
		CHECK(st_rip3_draw_image(ports,&image,NULL,0,&palette) == 0);
		CHECK(st_rip3_image_test_fills == (flags == 1 ? 1u : 0u));
		if (flags == 1) {
			CHECK(st_rip3_image_test_fill_color == 7 && st_rip3_image_test_fill_mode == ST_RIP3_COPY);
			CHECK(st_rip3_image_test_fill_area.x0 == expected[aspect].x0 && st_rip3_image_test_fill_area.y0 == expected[aspect].y0
			    && st_rip3_image_test_fill_area.x1 == expected[aspect].x1 && st_rip3_image_test_fill_area.y1 == expected[aspect].y1);
		}
		CHECK(st_rip3_bitmap_capture(&capture,ports,1,NULL,&palette) == 0);
		CHECK(capture->pixels->width == 140 && capture->pixels->height == 160);
		for (int32_t y = 0; y < 160; y++) for (int32_t x = 0; x < 140; x++) {
			unsigned value = x >= expected[aspect].x0 && x <= expected[aspect].x1
			    && y >= expected[aspect].y0 && y <= expected[aspect].y1 ? 5 : 9;
			CHECK(capture->pixels->primary[(size_t)y*140+(unsigned)x] == value);
		}
		for (unsigned i = 0; i < 256; i++) CHECK(capture->palette[i] == palette.rgb[i]);
		st_rip3_bitmap_destroy(capture);
	}
	st_rip3_ports_destroy(ports); return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 2);
	CHECK(styles() == 0);
	CHECK(compositing() == 0);
	CHECK(decoded_inputs(argv[1]) == 0);
	CHECK(render(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(render(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(render(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(measured_regions(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(measured_regions(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(measured_regions(ST_RIP3_CANVAS_1024_768) == 0);
	return 0;
}
