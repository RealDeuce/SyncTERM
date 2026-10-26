/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixel_image.h"

#include "io_error.h"
#include "st_pixels.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
test_ppm(void)
{
	static const char p3[] = "P3\n# colours\n3 1\n255\n0 0 0 128 128 128 255 255 255\n";
	static const uint8_t p6[] = {
		'P', '6', '\n', '2', ' ', '1', '\n', '6', '5', '5', '3', '5', '\n',
		0x00, 0x00, 0x80, 0x00, 0xff, 0xff,
		0xff, 0xff, 0x00, 0x00, 0x40, 0x00,
	};
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_buffer *sentinel = (struct st_pixel_buffer *)(uintptr_t)1;

	CHECK(st_pixel_image_decode_ppm(&pixels, p3, sizeof(p3) - 1) == 0);
	CHECK(pixels->width == 3 && pixels->height == 1);
	CHECK(pixels->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(0, 0, 0));
	CHECK(pixels->primary[1] == ST_PIXEL_COLOR_RGB_VALUE(140, 140, 140));
	CHECK(pixels->primary[2] == ST_PIXEL_COLOR_RGB_VALUE(255, 255, 255));
	st_pixel_buffer_destroy(pixels);
	pixels = NULL;
	CHECK(st_pixel_image_decode_ppm(&pixels, p6, sizeof(p6)) == 0);
	CHECK(pixels->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(0, 139, 255));
	CHECK(pixels->primary[1] == ST_PIXEL_COLOR_RGB_VALUE(255, 0, 79));
	st_pixel_buffer_destroy(pixels);
	CHECK(st_pixel_image_decode_ppm(&sentinel, "P6\n1 1\n255\n\0",
	    12) == IO_ERROR_INVALID_FORMAT);
	CHECK(sentinel == (struct st_pixel_buffer *)(uintptr_t)1);
	CHECK(st_pixel_image_decode_ppm(&sentinel, "P3 1 1 0 0 0 0", 16)
	    == IO_ERROR_INVALID_FORMAT);
	return 0;
}

static int
test_pbm(void)
{
	static const char p1[] = "P1\n3 2\n0 1 0\n1 0 1\n";
	static const uint8_t p4[] = {
		'P', '4', '\n', '9', ' ', '2', '\n', 0x80, 0x80, 0x40, 0x00,
	};
	struct st_pixel_mask *mask = NULL;
	struct st_pixel_mask *sentinel = (struct st_pixel_mask *)(uintptr_t)1;
	bool value;

	CHECK(st_pixel_image_decode_pbm(&mask, p1, sizeof(p1) - 1) == 0);
	CHECK(st_pixel_mask_get(mask, 1, 0, &value) == 0 && value);
	CHECK(st_pixel_mask_get(mask, 2, 1, &value) == 0 && value);
	st_pixel_mask_destroy(mask);
	mask = NULL;
	CHECK(st_pixel_image_decode_pbm(&mask, p4, sizeof(p4)) == 0);
	CHECK(st_pixel_mask_get(mask, 0, 0, &value) == 0 && value);
	CHECK(st_pixel_mask_get(mask, 8, 0, &value) == 0 && value);
	CHECK(st_pixel_mask_get(mask, 1, 1, &value) == 0 && value);
	CHECK(st_pixel_mask_get(mask, 0, 1, &value) == 0 && !value);
	st_pixel_mask_destroy(mask);
	CHECK(st_pixel_image_decode_pbm(&sentinel, "P4\n9 1\n\200", 8)
	    == IO_ERROR_INVALID_FORMAT);
	CHECK(sentinel == (struct st_pixel_mask *)(uintptr_t)1);
	return 0;
}

static int
hex_digit(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

static int
test_cterm_pnm(void)
{
	static const char ppm[] = "P6\n1 1\n255\r\n\xff\0\0";
	static const char mask_data[] = "P4\n8 1\r\n\xa0";
	static const char sixteen_bit[] = "P3\n1 1\n65535\n0 0 0\n";
	static const char extended_space[] = "P3\f1 1 255 0 0 0";
	struct st_pixel_buffer *image = NULL;
	struct st_pixel_mask *mask = NULL;

	/* APC consumes exactly one separator byte, even when it is CR.
	 * The following LF is an image sample, not another separator. */
	CHECK(st_pixel_image_decode_cterm_ppm(&image, ppm, sizeof(ppm) - 1) == 0);
	CHECK(image->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(23, 255, 0));
	st_pixel_buffer_destroy(image); image = NULL;
	CHECK(st_pixel_image_decode_ppm(&image, ppm, sizeof(ppm) - 1) == 0);
	CHECK(image->primary[0] == ST_PIXEL_COLOR_RGB_VALUE(255, 0, 0));
	st_pixel_buffer_destroy(image); image = NULL;
	CHECK(st_pixel_image_decode_cterm_pbm(&mask, mask_data, sizeof(mask_data) - 1) == 0);
	CHECK(mask->bits[0] == 0x0a);
	st_pixel_mask_destroy(mask); mask = NULL;
	CHECK(st_pixel_image_decode_pbm(&mask, mask_data, sizeof(mask_data) - 1) == 0);
	CHECK(mask->bits[0] == 0xa0);
	st_pixel_mask_destroy(mask);
	CHECK(st_pixel_image_decode_cterm_ppm(&image, sixteen_bit, sizeof(sixteen_bit) - 1) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_pixel_image_decode_cterm_ppm(&image, extended_space, sizeof(extended_space) - 1) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_pixel_image_decode_ppm(&image, sixteen_bit, sizeof(sixteen_bit) - 1) == 0);
	st_pixel_buffer_destroy(image);
	return 0;
}

static int
test_jxl(void)
{
	static const char encoded[] =
	    "ff0a001010505c080802010098024b189b9c71840338800338204ac03905"
	    "01002044800810012240e4ff917bfa1e5a67575555552549921050777777"
	    "7777ffffffbf556f66666606fedfbfe7bf87c69c73aeb5cfbd4992240454"
	    "5555555555ffffffcfbdafbbbbbb1bfedfbfe7bf87c69c73aeb5cfbd4992"
	    "2404545555555555ffffffcfbdafbbbbbb1bfedfbfe7bf87c69c73aeb5cf"
	    "bd49922404545555555555ffffffcfbdafbbbbbbfb022100b81237c28d30";
	uint8_t bytes[(sizeof(encoded) - 1) / 2];
	struct st_pixel_buffer *pixels = NULL;
	struct st_pixel_buffer *sentinel = (struct st_pixel_buffer *)(uintptr_t)1;
	bool supported = st_pixel_image_jxl_supported();
	int invalid_status;

	for (size_t i = 0; i < sizeof(bytes); ++i) {
		int high = hex_digit(encoded[i * 2]);
		int low = hex_digit(encoded[i * 2 + 1]);

		CHECK(high >= 0 && low >= 0);
		bytes[i] = (uint8_t)((high << 4) | low);
	}
	invalid_status = st_pixel_image_decode_jxl(&sentinel, "bad", 3);
	CHECK(invalid_status == (supported ? IO_ERROR_INVALID_FORMAT
	    : IO_ERROR_NOT_SUPPORTED));
	CHECK(sentinel == (struct st_pixel_buffer *)(uintptr_t)1);
	if (!supported)
		return st_pixel_image_decode_jxl(&pixels, bytes, sizeof(bytes))
		    == IO_ERROR_NOT_SUPPORTED ? 0 : 1;
	CHECK(st_pixel_image_decode_jxl(&pixels, bytes, sizeof(bytes)) == 0);
	CHECK(pixels != NULL && pixels->width == 1 && pixels->height == 1);
	CHECK((pixels->primary[0] & UINT32_C(0x00ffffff))
	    == UINT32_C(0x123456));
	st_pixel_buffer_destroy(pixels);
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(st_pixel_image_jxl_compiled() == (ST_PIXEL_IMAGE_TEST_HAVE_JXL != 0));
	CHECK(!st_pixel_image_jxl_supported() || st_pixel_image_jxl_compiled());
	if (argc == 2 && strcmp(argv[1], "--require-jxl") == 0)
		CHECK(st_pixel_image_jxl_supported());
	if (test_ppm() != 0 || test_pbm() != 0 || test_jxl() != 0
	    || test_cterm_pnm() != 0)
		return 1;
	return 0;
}
