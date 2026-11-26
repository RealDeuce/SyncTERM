/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_pixel_image.h"
#include "st_pixels.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static struct st_pixel_buffer *const sentinel = (struct st_pixel_buffer *)(uintptr_t)1;

static int
fixture(const char *directory, const char *name)
{
	unsigned char           bytes[1024];
	char                    path[4096];
	FILE                   *file;
	size_t                  length;
	struct st_pixel_buffer *pixels    = sentinel;
	bool                    grayscale = strncmp(name, "gray", 4) == 0;

	CHECK(snprintf(path, sizeof(path), "%s/%s.jpg", directory, name) > 0);
	file = fopen(path, "rb");
	CHECK(file != NULL);
	length = fread(bytes, 1, sizeof(bytes), file);
	CHECK(!ferror(file) && feof(file));
	CHECK(fclose(file) == 0);
	CHECK(length > 100);
	CHECK(st_pixel_image_jpeg_compiled() == (ST_TEST_JPEG != 0));
	if (!ST_TEST_JPEG) {
		CHECK(!st_pixel_image_jpeg_supported());
		CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length) == IO_ERROR_NOT_SUPPORTED);
		CHECK(pixels == sentinel);
		return 0;
	}
	/* An installed runtime decoder is required for this native test to
	 * provide positive coverage. The disabled target checks absence. */
	CHECK(st_pixel_image_jpeg_supported());
	if (strcmp(name, "cmyk") == 0) {
		CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length) == IO_ERROR_NOT_SUPPORTED);
		CHECK(pixels == sentinel);
		return 0;
	}
	CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length) == 0);
	CHECK(pixels->width == 17 && pixels->height == 9);
	CHECK(pixels->alternate == NULL);
	for (size_t y = 0; y < 9; y++) {
		for (size_t x = 0; x < 17; x++) {
			uint32_t pixel = pixels->primary[y * 17 + x];

			if (grayscale) {
				unsigned value = y < 8 ? 64 : 192;

				CHECK(pixel == ST_PIXEL_COLOR_RGB_VALUE(value, value, value));
			}
			else {
				CHECK(pixel == ST_PIXEL_COLOR_RGB_VALUE(170, 45, 23));
			}
		}
	}
	st_pixel_buffer_destroy(pixels);
	pixels = sentinel;
	/* Every truncated prefix must fail, including missing final EOI. */
	for (size_t length_cut = 0; length_cut < length; length_cut++) {
		CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length_cut) != 0);
		CHECK(pixels == sentinel);
	}
	/* Corrupt all SOF dimensions/precision and unsupported coding modes.
	 * These mutations have unambiguous header outcomes before entropy data. */
	for (size_t i = 0; i + 10 < length; i++) {
		if (bytes[i] == 255 && (bytes[i + 1] == 0xc0 || bytes[i + 1] == 0xc2)) {
			unsigned char frame = bytes[i + 1];
			unsigned char saved = bytes[i + 4];

			bytes[i + 4] = 12;
			CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length) != 0);
			CHECK(pixels == sentinel);
			bytes[i + 4] = saved;
			if (frame == 0xc0) {
				/* A sequential scan is invalid under a progressive frame header. */
				bytes[i + 1] = 0xc2;
				CHECK(
				    st_pixel_image_decode_jpeg(&pixels, bytes, length) == IO_ERROR_INVALID_FORMAT);
				CHECK(pixels == sentinel);
			}
			bytes[i + 1] = frame == 0xc0 ? 0xc9 : 0xca;
			CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length) == IO_ERROR_NOT_SUPPORTED);
			CHECK(pixels == sentinel);
			bytes[i + 1] = frame;
			if (!grayscale) {
				unsigned char sampling = bytes[i + 11];

				bytes[i + 11] = 0x31;
				CHECK(
				    st_pixel_image_decode_jpeg(&pixels, bytes, length) == IO_ERROR_NOT_SUPPORTED);
				CHECK(pixels == sentinel);
				bytes[i + 11] = sampling;
			}
			bytes[i + 5] = bytes[i + 6] = 0;
			CHECK(st_pixel_image_decode_jpeg(&pixels, bytes, length) != 0);
			CHECK(pixels == sentinel);
			break;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct st_pixel_buffer *pixels = sentinel;
	const char *names[] = {"gray", "444", "422", "420", "cmyk", "gray-progressive", "420-progressive"};

	CHECK(argc == 2);
	CHECK(st_pixel_image_decode_jpeg(NULL, "", 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_pixel_image_decode_jpeg(&pixels, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_pixel_image_decode_jpeg(&pixels, "", 0) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_pixel_image_decode_jpeg(&pixels, "garbage", 7) != 0);
	CHECK(pixels == sentinel);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		CHECK(fixture(argv[1], names[i]) == 0);
	return 0;
}
