/* Original native byte layout (IGS reference 03/05/159), literal index results. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_igs_image.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

static int
literal_layout(void)
{
	struct st_igs_image *image = NULL;
	/* Four plane words cover 16 pixels, MSB first in big-endian words. */
	const uint8_t native[] = {0x80,0x01, 0x40,0x80, 0x20,0x00, 0x10,0x00};
	const unsigned low[] = {1,2,4,8,0,0,0,0,2,0,0,0,0,0,0,1};
	CHECK(st_igs_image_create(&image, 4) == 0);
	CHECK(image->pixels->width == 64000 && image->pixels->height == 1);
	CHECK(image->pixels->alternate == NULL);
	CHECK(st_igs_image_import(image, 0, native, sizeof(native)) == 0);
	for (size_t i = 0; i < sizeof(low)/sizeof(*low); ++i)
		CHECK(image->pixels->primary[i] == low[i]);
	CHECK(image->pixels->primary[16] == 0);
	uint8_t out[8];
	CHECK(st_igs_image_export(image, 0, out, sizeof(out)) == 0);
	CHECK(memcmp(out, native, sizeof(out)) == 0);
	/* Two-plane interpretation spans two groups rather than truncating
	 * former indices. These values follow the four original plane words. */
	CHECK(st_igs_image_replane(image, 2) == 0);
	CHECK(image->pixels->width == 128000 && image->planes == 2);
	CHECK(image->pixels->primary[0] == 1 && image->pixels->primary[1] == 2);
	CHECK(image->pixels->primary[8] == 2 && image->pixels->primary[15] == 1);
	CHECK(image->pixels->primary[18] == 1 && image->pixels->primary[19] == 2);
	CHECK(st_igs_image_replane(image, 1) == 0);
	CHECK(image->pixels->width == 256000);
	const unsigned set[] = {0,15,17,24,34,51};
	for (unsigned x = 0; x < 64; ++x) {
		unsigned expected = 0;
		for (size_t i = 0; i < sizeof(set)/sizeof(*set); ++i)
			if (x == set[i]) expected = 1;
		CHECK(image->pixels->primary[x] == expected);
	}
	CHECK(st_igs_image_export(image, 0, out, sizeof(out)) == 0);
	CHECK(memcmp(out, native, sizeof(out)) == 0);
	CHECK(st_igs_image_replane(image, 4) == 0);
	for (size_t i = 0; i < sizeof(low)/sizeof(*low); ++i)
		CHECK(image->pixels->primary[i] == low[i]);
	/* A one-byte import changes one plane and half a group only. */
	const uint8_t replacement = 0x80;
	CHECK(st_igs_image_import(image, 1, &replacement, 1) == 0);
	CHECK(image->pixels->primary[0] == 1);
	CHECK(image->pixels->primary[8] == 3 && image->pixels->primary[15] == 0);
	CHECK(image->pixels->primary[9] == 0 && image->pixels->primary[3] == 8);
	st_igs_image_destroy(image);
	return 0;
}

static int
retained_storage(void)
{
	struct st_igs_image *image = NULL;
	CHECK(st_igs_image_create(&image, 4) == 0);
	const uint8_t first = 0x80, last = 0x01;
	CHECK(st_igs_image_import(image, 4000, &first, 1) == 0);
	CHECK(st_igs_image_import(image, 31999, &last, 1) == 0);
	CHECK(image->pixels->primary[8000] == 1);
	CHECK(image->pixels->primary[63999] == 8);
	/* View stride changes use the same indices, including untouched rows.
	 * Eight thousand pixels are row 25 at stride 320 and row 500 at 16. */
	CHECK(image->pixels->primary[25 * 320] == 1);
	CHECK(image->pixels->primary[500 * 16] == 1);
	CHECK(st_igs_image_replane(image, 2) == 0);
	CHECK(image->pixels->primary[16000] == 1);
	CHECK(image->pixels->primary[127999] == 2);
	CHECK(st_igs_image_replane(image, 1) == 0);
	CHECK(image->pixels->primary[32000] == 1);
	CHECK(image->pixels->primary[255999] == 1);
	CHECK(st_igs_image_replane(image, 4) == 0);
	CHECK(image->pixels->primary[8000] == 1);
	CHECK(image->pixels->primary[63999] == 8);
	uint8_t out = 0;
	CHECK(st_igs_image_export(image, 4000, &out, 1) == 0 && out == first);
	CHECK(st_igs_image_export(image, 31999, &out, 1) == 0 && out == last);
	struct st_pixel_buffer *before = image->pixels;
	CHECK(st_igs_image_replane(image, 4) == 0 && image->pixels == before);
	CHECK(st_igs_image_replane(image, 3) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(image->pixels == before && image->planes == 4);
	uint8_t guard[2] = {0x5a,0xa5};
	CHECK(st_igs_image_export(image, 31999, guard, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(guard[0] == 0x5a && guard[1] == 0xa5);
	CHECK(st_igs_image_import(image, 31999, guard, 2) == IO_ERROR_OUT_OF_RANGE);
	CHECK(image->pixels->primary[63999] == 8);
	CHECK(st_igs_image_import(image, 0, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_igs_image_import(image, 32000, NULL, 0) == 0);
	CHECK(st_igs_image_export(image, 32000, NULL, 0) == 0);
	CHECK(st_igs_image_export(image, SIZE_MAX, NULL, 0) == IO_ERROR_OUT_OF_RANGE);
	/* RGB/foreign palette-bank values are invalid in native memory images.
	 * Export validates before writing even the first output byte. */
	image->pixels->primary[15] = ST_PIXEL_COLOR_RGB_TAG;
	CHECK(st_igs_image_export(image, 0, guard, 2) == IO_ERROR_INVALID_FORMAT);
	CHECK(guard[0] == 0x5a && guard[1] == 0xa5);
	CHECK(st_igs_image_replane(image, 2) == IO_ERROR_INVALID_FORMAT);
	CHECK(image->pixels == before && image->planes == 4);
	st_igs_image_destroy(image);
	return 0;
}

static int
captured_phases(void)
{
	struct st_igs_image *image = NULL, *copy = NULL;
	CHECK(st_igs_image_create(&image,4) == 0);
	CHECK(st_pixel_buffer_ensure_alternate(image->pixels) == 0);
	image->pixels->primary[0] = 1; image->pixels->alternate[0] = 8;
	CHECK(st_igs_image_clone_layout(&copy,image,1) == 0);
	/* A four-plane pixel at bit15 goes to bit15 of its own native word. */
	CHECK(copy->pixels->primary[0] == 1 && copy->pixels->alternate[0] == 0);
	CHECK(copy->pixels->primary[48] == 0 && copy->pixels->alternate[48] == 1);
	CHECK(st_igs_image_replane(copy,4) == 0);
	CHECK(copy->pixels->primary[0] == 1 && copy->pixels->alternate[0] == 8);
	const uint8_t byte = 0xff;
	CHECK(st_igs_image_import(copy,0,&byte,1) == 0);
	CHECK(copy->pixels->primary[0] == 1 && copy->pixels->alternate[0] == 9);
	CHECK(copy->pixels->primary[7] == 1 && copy->pixels->alternate[7] == 1);
	uint8_t output = 0;
	CHECK(st_igs_image_export(copy,0,&output,1) == 0 && output == 0xff);
	copy->pixels->alternate[0] = ST_PIXEL_COLOR_RGB_TAG;
	struct st_igs_image *invalid = NULL;
	CHECK(st_igs_image_clone_layout(&invalid,copy,2) == IO_ERROR_INVALID_FORMAT && invalid == NULL);
	CHECK(st_igs_image_clone_layout(NULL,copy,2) == IO_ERROR_INVALID_ARGUMENT);
	st_igs_image_destroy(copy); st_igs_image_destroy(image);
	return 0;
}

static int
input_partitions(void)
{
	/* This is preservation/partition coverage, not a generated pixel golden.
	 * Literal pixel results are checked separately above. */
	const uint8_t source[] = {0,255,'G','#',17,19,'@',0x80,0x55,0xaa,1,2,3,4,5,6};
	const unsigned planes[] = {1,2,4};
	for (size_t mode = 0; mode < sizeof(planes)/sizeof(*planes); ++mode) {
		for (size_t split = 0; split <= sizeof(source); ++split) {
			struct st_igs_image *image = NULL;
			CHECK(st_igs_image_create(&image, planes[mode]) == 0);
			CHECK(st_igs_image_import(image, 3997, source, split) == 0);
			CHECK(st_igs_image_import(image, 3997+split, source+split, sizeof(source)-split) == 0);
			uint8_t result[sizeof(source)+2];
			CHECK(st_igs_image_export(image, 3996, result, sizeof(result)) == 0);
			CHECK(result[0] == 0 && result[sizeof(result)-1] == 0);
			CHECK(memcmp(result+1, source, sizeof(source)) == 0);
			st_igs_image_destroy(image);
		}
	}
	struct st_igs_image *image = NULL;
	CHECK(st_igs_image_create(&image, 0) == IO_ERROR_INVALID_ARGUMENT && image == NULL);
	CHECK(st_igs_image_create(&image, 3) == IO_ERROR_INVALID_ARGUMENT && image == NULL);
	st_igs_image_destroy(NULL);
	return 0;
}

int
main(void)
{
	CHECK(literal_layout() == 0);
	CHECK(retained_storage() == 0);
	CHECK(captured_phases() == 0);
	CHECK(input_partitions() == 0);
	return 0;
}
