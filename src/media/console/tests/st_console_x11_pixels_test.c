/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "console_x11_pixels.h"
#include "st_time.h"

#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); \
	exit(EXIT_FAILURE); \
} } while (false)

static unsigned long
component(unsigned long value, unsigned long mask)
{
	unsigned shift = 0;
	if (mask == 0)
		return 0;
	while ((mask & 1) == 0) {
		++shift;
		mask >>= 1;
	}
	return ((value * mask + 127) / 255) << shift;
}

static void
check_format(int bits, int order, unsigned long red, unsigned long green,
	unsigned long blue, int xoffset)
{
	unsigned char actual[2048], expected[2048];
	uint32_t source[17 * 11];
	struct st_rgb_buffer pixels = {.pixels = source, .width = 17, .height = 11};
	struct st_console_rect rectangle = {2, 1, 13, 8};
	XImage image = {
		.width = 17, .height = 11, .format = ZPixmap, .xoffset = xoffset,
		.byte_order = order, .bitmap_unit = 32, .bitmap_bit_order = order,
		.bitmap_pad = 32, .depth = bits == 32 ? 24 : bits,
		.bits_per_pixel = bits, .bytes_per_line = 80,
		.red_mask = red, .green_mask = green, .blue_mask = blue,
		.data = (char *)actual,
	};
	struct st_x11_pixel_writer writer;

	CHECK(XInitImage(&image));
	XImage reference = image;
	reference.data = (char *)expected;
	memset(actual, 0xa5, sizeof(actual));
	memset(expected, 0xa5, sizeof(expected));
	for (size_t i = 0; i < sizeof(source) / sizeof(source[0]); ++i)
		source[i] = (uint32_t)(i * 129871) & UINT32_C(0xffffff);
	source[25] = 0;
	source[26] = UINT32_C(0xffffff);
	st_x11_pixel_writer_init(&writer, &image);
	st_x11_pixel_writer_upload(&writer, &image, &pixels, &rectangle);
	for (uint32_t y = rectangle.y; y < rectangle.y + rectangle.height; ++y)
		for (uint32_t x = rectangle.x; x < rectangle.x + rectangle.width; ++x) {
			uint32_t rgb = source[y * pixels.width + x];
			unsigned long value = component((rgb >> 16) & 255, red)
			    | component((rgb >> 8) & 255, green) | component(rgb & 255, blue);
			XPutPixel(&reference, (int)x, (int)y, value);
		}
	CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
}

/* Exact pre-optimization upload loop, retained only as a benchmark oracle. */
static void
old_upload(const struct st_x11_pixel_writer *writer, XImage *image,
	const struct st_rgb_buffer *pixels, const struct st_console_rect *rectangle)
{
	for (uint32_t y = 0; y < rectangle->height; ++y) {
		const uint32_t *source = pixels->pixels
		    + (size_t)(rectangle->y + y) * pixels->width + rectangle->x;
		for (uint32_t x = 0; x < rectangle->width; ++x) {
			unsigned long value = writer->red[(source[x] >> 16) & 255]
			    | writer->green[(source[x] >> 8) & 255] | writer->blue[source[x] & 255];
			unsigned bytes = (unsigned)image->bits_per_pixel / 8;
			unsigned char *destination = (unsigned char *)image->data
			    + (size_t)(rectangle->y + y) * (size_t)image->bytes_per_line
			    + (size_t)(rectangle->x + x) * bytes;
			for (unsigned i = 0; i < bytes; ++i)
				destination[i] = (unsigned char)(value >>
				    ((image->byte_order == LSBFirst ? i : bytes - i - 1) * 8));
		}
	}
}

static void
benchmark(void)
{
	struct st_rgb_buffer *pixels = NULL;
	struct st_x11_pixel_writer writer;
	const uint32_t marker = 1;
	unsigned char first;
	uint64_t start, end;
	const struct st_console_rect full = {0, 0, 640, 400};
	CHECK(st_rgb_buffer_create(&pixels, 640, 400) == 0);
	memcpy(&first, &marker, 1);
	XImage image = {.width = 640, .height = 400, .format = ZPixmap,
		.bits_per_pixel = 32, .bytes_per_line = 640 * 4,
		.byte_order = first == 1 ? LSBFirst : MSBFirst,
		.red_mask = 0xff0000, .green_mask = 0xff00, .blue_mask = 0xff};
	image.data = malloc(640 * 400 * 4);
	CHECK(image.data != NULL);
	for (size_t i = 0; i < (size_t)640 * 400; ++i)
		pixels->pixels[i] = (uint32_t)(i * 3121) & UINT32_C(0xffffff);
	st_x11_pixel_writer_init(&writer, &image);
	for (unsigned optimized = 0; optimized < 2; ++optimized) {
		CHECK(st_time_process_cpu_ns(&start) == 0);
		for (unsigned pass = 0; pass < 1000; ++pass) {
			if (optimized)
				st_x11_pixel_writer_upload(&writer, &image, pixels, &full);
			else
				old_upload(&writer, &image, pixels, &full);
		}
		CHECK(st_time_process_cpu_ns(&end) == 0);
		printf("X11 %s: %.3f CPU ms for 1000 640x400 uploads\n",
		    optimized ? "row-copy" : "old per-pixel",
		    (double)(end - start) / 1000000.0);
		CHECK(memcmp(image.data, pixels->pixels, 640 * 400 * 4) == 0);
	}
	free(image.data);
	st_rgb_buffer_destroy(pixels);
}

int
main(int argc, char **argv)
{
	for (int order = LSBFirst; order <= MSBFirst; ++order) {
		check_format(32, order, 0xff0000, 0xff00, 0xff, 0);
		check_format(32, order, 0xff, 0xff00, 0xff0000, 0);
		check_format(32, order, 0x3ff00000, 0xffc00, 0x3ff, 0);
		check_format(24, order, 0xff0000, 0xff00, 0xff, 0);
		check_format(16, order, 0xf800, 0x7e0, 0x1f, 0);
		check_format(8, order, 0xe0, 0x1c, 3, 0);
		check_format(1, order, 1, 0, 0, 0);
		check_format(32, order, 0xff0000, 0xff00, 0xff, 1);
	}
	if (argc == 2 && strcmp(argv[1], "--benchmark") == 0)
		benchmark();
	return EXIT_SUCCESS;
}
