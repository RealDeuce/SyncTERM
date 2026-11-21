/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_surface.h"
#include "io_error.h"
#include "st_pixels.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "palette_expected.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
palette_test(void)
{
	struct st_rip3_palette palette;
	struct st_rip3_palette saved;
	unsigned master[] = {0, 32, 4, 36, 16, 2, 18, 8, 1, 9, 63};
	const uint32_t legacy[] = {0, 0x550000, 0xaa0000, 0xff0000,
	    0x005500, 0x00aa00, 0x00ff00, 0x000055, 0x0000aa, 0x0000ff,
	    0xffffff};
	const uint32_t colors[] = {0x04140d, 0x4c5660, 0xffffff};
	uint32_t invalid[] = {0x010203, 0x1000000};
	uint8_t index = 77;

	CHECK(st_rip3_palette_defaults(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	for (size_t i = 0; i < 256; i++)
		CHECK(palette.rgb[i] == default_palette_expected[i]);
	CHECK(st_rip3_palette_color(&palette, true, 0xffffff, &index) == 0);
	CHECK(index == 15);
	CHECK(st_rip3_palette_rgb(&palette, 0, 8, default_palette_expected, 256) == 0);
	CHECK(st_rip3_palette_rgb(&palette, 0, 8, default_palette_expected, 257)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palette_desktop(&palette, 0, master,
	    sizeof(master) / sizeof(master[0])) == 0);
	for (size_t i = 0; i < sizeof(legacy) / sizeof(legacy[0]); i++)
		CHECK(palette.rgb[i] == legacy[i]);
	CHECK(palette.rgb[16] == 0 && palette.rgb[255] == 0xffffff);
	CHECK(st_rip3_palette_rgb(&palette, 253, 8, colors, 3) == 0);
	CHECK(palette.rgb[253] == 0x04140d && palette.rgb[254] == 0x4c5660
	    && palette.rgb[255] == 0xffffff);
	CHECK(st_rip3_palette_color(&palette, false, 255, &index) == 0);
	CHECK(index == 255);
	CHECK(st_rip3_palette_color(&palette, true, 0x04140d, &index) == 0);
	CHECK(index == 253);
	saved = palette;
	CHECK(st_rip3_palette_rgb(&palette, SIZE_MAX, 8, colors, 1)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palette_rgb(&palette, 255, 8, colors, 2)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palette_rgb(&palette, 0, 8, invalid, 2)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palette_rgb(&palette, 0, 6, colors, 1)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(st_rip3_palette_rgb(&palette, 0, 8, NULL, 1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_palette_desktop(&palette, 15, master, 2)
	    == IO_ERROR_OUT_OF_RANGE);
	master[2] = 64;
	CHECK(st_rip3_palette_desktop(&palette, 0, master, 3)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palette_desktop(&palette, SIZE_MAX, master, 1)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(&palette, &saved, sizeof(palette)) == 0);
	index = 77;
	CHECK(st_rip3_palette_color(&palette, false, 256, &index)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palette_color(&palette, true, 0x1000000, &index)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(index == 77);
	CHECK(st_rip3_palette_rgb(&palette, 256, 8, NULL, 0) == 0);
	CHECK(st_rip3_palette_desktop(&palette, 16, NULL, 0) == 0);
	CHECK(memcmp(&palette, &saved, sizeof(palette)) == 0);
	/* A palette can be changed from its own overlapping span. */
	CHECK(st_rip3_palette_rgb(&palette, 254, 8, palette.rgb + 253, 2) == 0);
	CHECK(palette.rgb[254] == 0x04140d && palette.rgb[255] == 0x4c5660);
	/* Isolate nearest-match/tie behavior from the default cube algorithm. */
	for (size_t i = 0; i < 256; i++)
		palette.rgb[i] = 0xffffff;
	palette.rgb[20] = 0;
	palette.rgb[21] = 0x020202;
	CHECK(st_rip3_palette_color(&palette, true, 0x010101, &index) == 0);
	CHECK(index == 20);
	CHECK(st_rip3_palette_color(&palette, true, 0x030203, &index) == 0);
	CHECK(index == 21);
	palette.rgb[255] = 0x1000000;
	index = 77;
	CHECK(st_rip3_palette_color(&palette, true, 0, &index)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(index == 77);
	return 0;
}

static int
surface_test(void)
{
	struct st_rip3_surface *surface = NULL;
	struct st_rip3_surface *unchanged = (struct st_rip3_surface *)(uintptr_t)1;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *snapshot = NULL;
	struct st_pixel_buffer *older = NULL;
	struct st_rip3_rect area = {-10, -10, 10, 10};
	struct st_rip3_rect clip = {1, 1, 3, 2};
	const uint8_t expected[] = {
		0, 0, 0, 0, 0,
		0, 7, 7, 7, 0,
		0, 7, 7, 7, 0,
		0, 0, 0, 0, 0,
	};
	uint8_t value;
	uint32_t width;
	uint32_t height;
	uint32_t custom[] = {0x123456};

	CHECK(st_rip3_surface_create(&surface, 5, 4) == 0);
	CHECK(st_rip3_surface_size(surface, &width, &height) == 0);
	CHECK(width == 5 && height == 4);
	CHECK(st_rip3_surface_fill(surface, &clip, &area, 7, ST_RIP3_COPY) == 0);
	for (int32_t y = 0; y < 4; y++) {
		for (int32_t x = 0; x < 5; x++) {
			CHECK(st_rip3_surface_get(surface, x, y, &value) == 0);
			CHECK(value == expected[y * 5 + x]);
		}
	}
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_surface_snapshot(surface, &palette, &older) == 0);
	CHECK(older->width == 5 && older->height == 4 && older->alternate == NULL);
	CHECK(older->primary[6] == ST_PIXEL_COLOR_RGB_VALUE(170, 170, 170));
	CHECK(st_rip3_palette_rgb(&palette, 7, 8, custom, 1) == 0);
	CHECK(st_rip3_palette_rgb(&palette, 0, 8, custom, 1) == 0);
	CHECK(st_rip3_surface_snapshot(surface, &palette, &snapshot) == 0);
	for (size_t i = 0; i < 20; i++)
		CHECK(snapshot->primary[i] == ST_PIXEL_COLOR_RGB_VALUE(18, 52, 86));
	/* Existing snapshot remains coherent after palette changes and draws. */
	CHECK(older->primary[0] == ST_PIXEL_COLOR_RGB_TAG);
	CHECK(older->primary[6] == ST_PIXEL_COLOR_RGB_VALUE(170, 170, 170));
	st_pixel_buffer_destroy(snapshot);
	snapshot = NULL;
	CHECK(st_rip3_surface_plot(surface, NULL, 4, 3, 7, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_plot(surface, NULL, 4, 3, 5, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_surface_get(surface, 4, 3, &value) == 0 && value == 2);
	CHECK(st_rip3_surface_plot(surface, NULL, 4, 3, 8, ST_RIP3_OR) == 0);
	CHECK(st_rip3_surface_get(surface, 4, 3, &value) == 0 && value == 10);
	CHECK(st_rip3_surface_plot(surface, NULL, 4, 3, 6, ST_RIP3_AND) == 0);
	CHECK(st_rip3_surface_get(surface, 4, 3, &value) == 0 && value == 2);
	CHECK(st_rip3_surface_plot(surface, NULL, 4, 3, 0, ST_RIP3_NOT) == 0);
	CHECK(st_rip3_surface_get(surface, 4, 3, &value) == 0 && value == 253);
	CHECK(st_rip3_surface_plot(surface, NULL, 4, 3, 99, ST_RIP3_NOT) == 0);
	CHECK(st_rip3_surface_get(surface, 4, 3, &value) == 0 && value == 2);
	/* Literal monochrome truth tables, indexed by destination*2 + source. */
	{
		const uint8_t truth[][4] = {
			{0, 1, 0, 1}, {0, 1, 1, 0}, {0, 1, 1, 1}, {0, 0, 0, 1},
		};

		for (unsigned mode = 0; mode < 4; mode++) {
			for (unsigned pair = 0; pair < 4; pair++) {
				CHECK(st_rip3_surface_plot(surface, NULL, 0, 3,
				    (uint8_t)(pair / 2), ST_RIP3_COPY) == 0);
				CHECK(st_rip3_surface_plot(surface, NULL, 0, 3,
				    (uint8_t)(pair % 2), (enum st_rip3_write_mode)mode) == 0);
				CHECK(st_rip3_surface_get(surface, 0, 3, &value) == 0);
				CHECK(value == truth[mode][pair]);
			}
		}
	}
	/* Index operations are independent of palette RGB: 7 NOT becomes 248. */
	CHECK(st_rip3_surface_plot(surface, NULL, 1, 1, 0, ST_RIP3_NOT) == 0);
	CHECK(st_rip3_surface_get(surface, 1, 1, &value) == 0 && value == 248);
	CHECK(st_rip3_surface_plot(surface, NULL, 1, 1, 99, ST_RIP3_NOT) == 0);
	CHECK(st_rip3_surface_get(surface, 1, 1, &value) == 0 && value == 7);
	CHECK(st_rip3_surface_plot(surface, &clip, 0, 0, 99, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_plot(surface, NULL, -1, 0, 99, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_surface_plot(surface, NULL, 0, INT32_MAX, 99, ST_RIP3_COPY) == 0);
	area = (struct st_rip3_rect){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
	CHECK(st_rip3_surface_fill(surface, &clip, &area, 3, ST_RIP3_XOR) == 0);
	CHECK(st_rip3_surface_get(surface, 1, 1, &value) == 0 && value == 4);
	area = (struct st_rip3_rect){1, 1, 0, 0};
	CHECK(st_rip3_surface_fill(surface, NULL, &area, 77, ST_RIP3_COPY)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_plot(surface, &area, 1, 1, 77, ST_RIP3_COPY)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_plot(surface, NULL, 1, 1, 77,
	    (enum st_rip3_write_mode)-1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_plot(surface, NULL, 1, 1, 77,
	    (enum st_rip3_write_mode)5) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_get(surface, 1, 1, &value) == 0 && value == 4);
	value = 66;
	CHECK(st_rip3_surface_get(surface, -1, 0, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_surface_get(surface, 5, 0, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(value == 66);
	CHECK(st_rip3_surface_create(&unchanged, 0, 4) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_create(&unchanged, UINT32_MAX, 4) == IO_ERROR_OUT_OF_RANGE);
	CHECK(unchanged == (struct st_rip3_surface *)(uintptr_t)1);
	palette.rgb[255] = 0x1000000;
	CHECK(st_rip3_surface_snapshot(surface, &palette, &snapshot)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(snapshot == NULL);
	st_pixel_buffer_destroy(older);
	st_rip3_surface_destroy(surface);
	st_rip3_surface_destroy(NULL);
	return 0;
}

static int
tiers_test(void)
{
	const uint32_t sizes[][2] = {{640, 480}, {800, 600}, {1024, 768}};
	struct st_rip3_palette palette;

	CHECK(st_rip3_palette_defaults(&palette) == 0);
	for (size_t i = 0; i < 3; i++) {
		struct st_rip3_surface *surface = NULL;
		struct st_pixel_buffer *pixels = NULL;
		int32_t x = (int32_t)sizes[i][0] - 1;
		int32_t y = (int32_t)sizes[i][1] - 1;

		CHECK(st_rip3_surface_create(&surface, sizes[i][0], sizes[i][1]) == 0);
		CHECK(st_rip3_surface_plot(surface, NULL, x, y, 15, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_surface_snapshot(surface, &palette, &pixels) == 0);
		CHECK(pixels->width == sizes[i][0] && pixels->height == sizes[i][1]);
		CHECK(pixels->primary[0] == ST_PIXEL_COLOR_RGB_TAG);
		CHECK(pixels->primary[(size_t)y * sizes[i][0] + (uint32_t)x]
		    == ST_PIXEL_COLOR_RGB_VALUE(255, 255, 255));
		st_pixel_buffer_destroy(pixels);
		st_rip3_surface_destroy(surface);
	}
	return 0;
}

static int
damage_test(void)
{
	struct st_rip3_surface *surface=NULL,*copy=NULL;
	uint8_t damage[12]={0};
	CHECK(st_rip3_surface_create(&surface,4,3)==0);
	CHECK(st_rip3_surface_damage(surface,damage,11)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_damage(surface,damage,sizeof(damage))==0);
	/* Black-on-black COPY is still damage: ANSI may have painted over it. */
	CHECK(st_rip3_surface_plot(surface,NULL,1,1,0,ST_RIP3_COPY)==0);
	for (size_t i=0;i<sizeof(damage);i++) CHECK(damage[i]==(i==5));
	CHECK(st_rip3_surface_plot(surface,NULL,-1,0,0,ST_RIP3_COPY)==0 && damage[0]==0);
	CHECK(st_rip3_surface_clone(&copy,surface)==0);
	memset(damage,0,sizeof(damage));
	CHECK(st_rip3_surface_plot(copy,NULL,0,0,0,ST_RIP3_COPY)==0 && damage[0]==0);
	const struct st_rip3_rect source={0,0,0,0},destination={2,2,3,2};
	CHECK(st_rip3_surface_copy(surface,NULL,&destination,copy,NULL,&source,ST_RIP3_TRANSFER_COPY)==0);
	for (size_t i=0;i<sizeof(damage);i++) CHECK(damage[i]==(i>=10));
	memset(damage,0,sizeof(damage));
	CHECK(st_rip3_surface_copy_projected(surface,&destination,copy,&source,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(damage[10]==1 && damage[11]==1);
	CHECK(st_rip3_surface_damage(surface,NULL,0)==0);
	memset(damage,0,sizeof(damage));
	CHECK(st_rip3_surface_plot(surface,NULL,0,0,15,ST_RIP3_COPY)==0 && damage[0]==0);
	st_rip3_surface_destroy(copy);st_rip3_surface_destroy(surface);return 0;
}

static int
black_clear(void)
{
	struct st_rip3_surface *surface=NULL,*copy=NULL,*saved=NULL;
	struct st_rip3_palette palette;struct st_pixel_buffer *pixels=NULL;
	CHECK(st_rip3_palette_defaults(&palette)==0);
	palette.rgb[0]=0xff0000;
	CHECK(st_rip3_surface_create(&surface,4,2)==0 && st_rip3_surface_create(&copy,4,2)==0);
	uint8_t damage[8]={0};const struct st_rip3_rect all={0,0,3,1};
	CHECK(st_rip3_surface_damage(surface,damage,sizeof(damage))==0);
	CHECK(st_rip3_surface_clear_black(NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_surface_clear_black(surface)==0 && st_rip3_surface_clone(&saved,surface)==0);
	for (size_t i=0;i<8;i++) CHECK(damage[i]==1);
	CHECK(st_rip3_surface_plot(surface,NULL,0,0,0,ST_RIP3_COPY)==0);
	CHECK(st_rip3_surface_copy(copy,NULL,&all,surface,NULL,&all,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(st_rip3_surface_snapshot(copy,&palette,&pixels)==0);
	for (size_t i=0;i<8;i++) CHECK(pixels->primary[i]==(ST_PIXEL_COLOR_RGB_TAG|(i==0 ? 0xff0000 : 0)));
	st_pixel_buffer_destroy(pixels);pixels=NULL;
	palette.rgb[0]=0x00ff00;
	CHECK(st_rip3_surface_snapshot(saved,&palette,&pixels)==0);
	for (size_t i=0;i<8;i++) CHECK(pixels->primary[i]==ST_PIXEL_COLOR_RGB_TAG);
	st_pixel_buffer_destroy(pixels);pixels=NULL;
	/* Overlap and projected copies preserve literal pixels independently of
	 * subsequent palette uploads. Ordinary index-zero COPY still uses PAL. */
	const struct st_rip3_rect from={0,0,2,0},to={1,0,3,0};
	CHECK(st_rip3_surface_copy_projected(copy,&to,copy,&from,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(st_rip3_surface_snapshot(copy,&palette,&pixels)==0);
	CHECK(pixels->primary[0]==(ST_PIXEL_COLOR_RGB_TAG|0x00ff00)
	    && pixels->primary[1]==(ST_PIXEL_COLOR_RGB_TAG|0x00ff00)
	    && pixels->primary[2]==ST_PIXEL_COLOR_RGB_TAG && pixels->primary[3]==ST_PIXEL_COLOR_RGB_TAG);
	st_pixel_buffer_destroy(pixels);st_rip3_surface_destroy(saved);
	st_rip3_surface_destroy(copy);st_rip3_surface_destroy(surface);return 0;
}

int
main(void)
{
	CHECK(palette_test() == 0);
	CHECK(surface_test() == 0);
	CHECK(tiers_test() == 0);
	CHECK(damage_test() == 0);
	CHECK(black_clear() == 0);
	return 0;
}
