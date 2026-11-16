/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern bool st_rip3_bitmap_stamp_test_fail_allocate;
uint32_t st_rip3_bitmap_stamp_test_sample(uint64_t position, uint64_t span, uint32_t source);

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

static void
gray(struct st_rip3_palette *palette)
{
	for (unsigned i = 0; i < 256; i++) palette->rgb[i] = i*UINT32_C(0x010101);
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	uint32_t codes[] = {3,12,48,192,0,255};
	struct st_pixel_buffer pixels = {.width=3,.height=2,.primary=codes};
	struct st_rip3_bitmap bitmap = {.pixels=&pixels,.palette_size=256,.bit_depth=8};
	const struct st_rip3_rect bounds = {10,20,45,35}, view = {2,1,33,12};
	const uint8_t expected_modes[5][6] = {
		{3,12,48,192,0,255},{86,89,101,149,85,170},{87,93,117,213,85,255},
		{1,4,16,64,0,85},{252,243,207,63,255,0},
	};
	const uint8_t transparent_rgb[] = {252,85,207,63,255,0};
	const uint8_t scaled[] = {3,3,12,12,48,48,3,3,12,12,48,48,
	    192,192,0,0,255,255,192,192,0,0,255,255};
	const uint8_t clipped[] = {3,3,12,3,3,12,192,192,0,192,192,0};
	const uint8_t negative[] = {12,12,48,48,0,0,255,255};
	gray(&palette);
	memcpy(bitmap.palette,palette.rgb,sizeof(bitmap.palette));
	CHECK(st_rip3_ports_create(&ports,canvas) == 0);
	CHECK(st_rip3_ports_define(ports,1,&bounds,false) == 0 && st_rip3_ports_switch(ports,1) == 0);
	CHECK(st_rip3_ports_viewport(ports,&view) == 0 && st_rip3_ports_erase(ports,85) == 0);
	/* Image-style disabling is unrelated to explicit bitmap stamping. */
	const struct st_rip3_rect disabled = {0,0,0,0};
	CHECK(st_rip3_ports_image_style(ports,1,&disabled,15) == 0 && st_rip3_ports_protect(ports,1,true) == 0);
	for (unsigned mode = 0; mode < 5; mode++) {
		struct st_rip3_bitmap_destination destination = {5*(int64_t)mode,0,5*(int64_t)mode+2,1};
		CHECK(st_rip3_draw_bitmap(ports,&bitmap,&destination,&palette,(enum st_rip3_transfer_mode)mode,NULL) == 0);
	}
	uint8_t transparent = 12;
	const struct st_rip3_bitmap_destination first = {0,3,2,4}, second = {5,3,7,4};
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&first,&palette,ST_RIP3_TRANSFER_COPY,&transparent) == 0);
	for (unsigned i = 0; i < 6; i++) codes[i] = ST_PIXEL_COLOR_RGB_TAG | palette.rgb[codes[i]];
	bitmap.bit_depth = 24; bitmap.palette_size = 0;
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&second,&palette,ST_RIP3_TRANSFER_INVERT,&transparent) == 0);
	for (unsigned i = 0; i < 6; i++) codes[i] &= 255;
	bitmap.bit_depth = 8; bitmap.palette_size = 256;
	const struct st_rip3_bitmap_destination enlarged = {0,6,5,9}, edge = {29,0,34,3}, left = {-2,10,3,11};
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&enlarged,&palette,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&edge,&palette,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&left,&palette,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	/* Equal source RGB values must not merge indexed transparency identities. */
	uint32_t twins[] = {1,2};
	struct st_pixel_buffer twin_pixels = {.width=2,.height=1,.primary=twins};
	struct st_rip3_bitmap twin_bitmap = {.pixels=&twin_pixels,.palette_size=3,.bit_depth=8,
	    .palette={0,0x141414,0x141414}};
	const struct st_rip3_bitmap_destination twin_area = {10,3,11,3};
	transparent = 1;
	CHECK(st_rip3_draw_bitmap(ports,&twin_bitmap,&twin_area,&palette,ST_RIP3_TRANSFER_COPY,&transparent) == 0);
	CHECK(st_rip3_ports_snapshot(ports,&palette,&capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		uint8_t expected = 0;
		if (x >= 12 && x <= 43 && y >= 21 && y <= 32) {
			uint32_t vx = x-12, vy = y-21;
			expected = 85;
			for (unsigned mode = 0; mode < 5; mode++)
				if (vx >= mode*5 && vx <= mode*5+2 && vy < 2)
					expected = expected_modes[mode][vy*3+vx-mode*5];
			if (vx < 3 && vy >= 3 && vy <= 4) {
				expected = expected_modes[0][(vy-3)*3+vx];
				if (vx == 1 && vy == 3) expected = 85;
			}
			if (vx >= 5 && vx <= 7 && vy >= 3 && vy <= 4) expected = transparent_rgb[(vy-3)*3+vx-5];
			if (vy == 3 && vx == 11) expected = 20;
			if (vx < 6 && vy >= 6 && vy <= 9) expected = scaled[(vy-6)*6+vx];
			if (vx >= 29 && vy < 4) expected = clipped[vy*3+vx-29];
			if (vx < 4 && vy >= 10) expected = negative[(vy-10)*4+vx];
		}
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | palette.rgb[expected]));
	}
	struct st_rip3_port_info info;
	unsigned current;
	CHECK(st_rip3_ports_current(ports,&current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(ports,1,&info) == 0 && info.protected && !info.image.enabled
	    && info.image.flags == 15 && info.viewport.x0 == 2 && info.viewport.y0 == 1);
	const uint32_t original[] = {3,12,48,192,0,255};
	CHECK(memcmp(codes,original,sizeof(original)) == 0);
	st_pixel_buffer_destroy(capture); st_rip3_ports_destroy(ports);
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	uint32_t codes[] = {3,12,48,192,0,255};
	struct st_pixel_buffer pixels = {.width=3,.height=2,.primary=codes};
	struct st_rip3_bitmap bitmap = {.pixels=&pixels,.palette_size=256,.bit_depth=8};
	const struct st_rip3_rect viewport = {0,0,3,1};
	const int64_t limit = INT64_C(1024)*INT32_MAX;
	struct st_rip3_bitmap_destination destination = {-limit,-limit,limit,limit};
	gray(&palette); memcpy(bitmap.palette,palette.rgb,sizeof(bitmap.palette));
	CHECK(st_rip3_ports_create(&ports,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_viewport(ports,&viewport) == 0 && st_rip3_ports_erase(ports,85) == 0);
	st_rip3_bitmap_stamp_test_fail_allocate = true;
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&destination,&palette,ST_RIP3_TRANSFER_COPY,NULL) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(!st_rip3_bitmap_stamp_test_fail_allocate);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 4; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == 85);
	}
	codes[5] = 256;
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&destination,&palette,ST_RIP3_TRANSFER_COPY,NULL) == IO_ERROR_INVALID_FORMAT);
	codes[5] = 255;
	destination.x1 = limit+1;
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&destination,&palette,ST_RIP3_TRANSFER_COPY,NULL) == IO_ERROR_OUT_OF_RANGE);
	destination.x1 = limit; destination.x0 = limit+1;
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&destination,&palette,ST_RIP3_TRANSFER_COPY,NULL) == IO_ERROR_INVALID_ARGUMENT);
	destination.x0 = -limit;
	CHECK(st_rip3_draw_bitmap(ports,&bitmap,&destination,&palette,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 4; x++) {
		uint8_t actual;
		CHECK(st_rip3_ports_read(ports,0,x,y,&actual) == 0 && actual == 0);
	}
	CHECK(st_rip3_ports_activate(ports,false) == 0);
	CHECK(st_rip3_draw_bitmap(ports,NULL,&destination,NULL,ST_RIP3_TRANSFER_COPY,NULL) == 0);
	st_rip3_ports_destroy(ports);
	/* Literal exact-integer rational expectations, including products wider
	 * than uint64_t; no floating-point or renderer generates the oracle. */
	const struct { uint64_t position; uint32_t source, expected; } samples[] = {
		{0,2147483647,0},{2199023254527,2147483647,1073741823},
		{2199023254528,2147483647,1073741823},{2199023254529,2147483647,1073741823},
		{4398046509056,2147483647,2147483646},{1234567890123,1073741825,301408176},
		{4398046509055,1073741825,1073741824},
	};
	for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++)
		CHECK(st_rip3_bitmap_stamp_test_sample(samples[i].position,UINT64_C(4398046509057),samples[i].source) == samples[i].expected);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480,640,480) == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600,800,600) == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768,1024,768) == 0);
	CHECK(boundaries() == 0);
	return 0;
}
