/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap.h"
#include "filesystem.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

/* 8x2, indexes 0..7 then 8..15. Planes 3,2,1,0 are independent literal
 * bit patterns. Two garbage tail bytes are deliberately nonzero. */
static const uint8_t fixture[] = {7,0,1,0, 0,15,51,85, 255,15,51,85, 0x39,0x3a};

static void
palette_values(uint32_t palette[16])
{
	for (unsigned i = 0; i < 16; i++)
		palette[i] = (uint32_t)i << 16 | (uint32_t)(15 - i) << 8 | i;
}

static int
read_fixture(const char *path, uint8_t *bytes, size_t size)
{
	FILE *stream = NULL;

	CHECK(fs_open(&stream, path, "rb") == 0);
	CHECK(fread(bytes, 1, size, stream) == size);
	CHECK(fgetc(stream) == EOF && !ferror(stream));
	CHECK(fclose(stream) == 0);
	return 0;
}

static int
reference_conversion(const char *legacy_path, const char *bmp_path)
{
	uint8_t legacy[94], expected[206], *encoded = NULL;
	uint32_t palette[16];
	struct st_rip3_bitmap *bitmap = NULL;
	size_t size;

	CHECK(read_fixture(legacy_path, legacy, sizeof(legacy)) == 0);
	CHECK(read_fixture(bmp_path, expected, sizeof(expected)) == 0);
	CHECK(legacy[0] == 14 && legacy[2] == 10 && expected[0] == 'B' && expected[1] == 'M');
	/* Palette comes explicitly from the original expected BMP, not from the
	 * decoder or an inferred policy for on-the-fly file conversion. */
	for (size_t i = 0; i < 16; i++) {
		const uint8_t *entry = expected + 54 + 4 * i;
		palette[i] = (uint32_t)entry[2] << 16 | (uint32_t)entry[1] << 8 | entry[0];
	}
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, legacy, sizeof(legacy), palette) == 0);
	CHECK(bitmap->pixels->width == 15 && bitmap->pixels->height == 11);
	CHECK(st_rip3_bitmap_encode(&encoded, &size, bitmap) == 0);
	/* Complete independently authored golden file, including every row. */
	CHECK(size == sizeof(expected) && memcmp(encoded, expected, sizeof(expected)) == 0);
	free(encoded);
	st_rip3_bitmap_destroy(bitmap);
	return 0;
}

static int
literal_and_conversion(void)
{
	struct st_rip3_bitmap *bitmap = NULL, *bmp = NULL;
	uint32_t palette[16];
	uint8_t *encoded = NULL;
	size_t size;
	/* Literal packed BMP rows, bottom-up, independently specified. */
	const uint8_t expected_rows[] = {0x89,0xab,0xcd,0xef, 0x01,0x23,0x45,0x67};

	palette_values(palette);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, fixture, sizeof(fixture), palette) == 0);
	CHECK(bitmap->bit_depth == 4 && bitmap->palette_size == 16);
	CHECK(bitmap->pixels->width == 8 && bitmap->pixels->height == 2);
	for (unsigned i = 0; i < 16; i++) {
		CHECK(bitmap->pixels->primary[i] == i);
		CHECK(bitmap->palette[i] == palette[i]);
	}
	palette[0] = 0xffffff;
	CHECK(bitmap->palette[0] == 0x000f00); /* Independent palette ownership. */
	CHECK(st_rip3_bitmap_encode(&encoded, &size, bitmap) == 0 && size == 126);
	CHECK(encoded[0] == 'B' && encoded[1] == 'M' && encoded[2] == 126);
	CHECK(encoded[10] == 118 && encoded[14] == 40 && encoded[28] == 4);
	CHECK(memcmp(encoded + 118, expected_rows, sizeof(expected_rows)) == 0);
	CHECK(encoded[54] == 0 && encoded[55] == 15 && encoded[56] == 0 && encoded[57] == 0);
	CHECK(st_rip3_bitmap_decode(&bmp, encoded, size) == 0);
	CHECK(memcmp(bmp->palette, bitmap->palette, sizeof(bitmap->palette)) == 0);
	for (unsigned i = 0; i < 16; i++) CHECK(bmp->pixels->primary[i] == i);
	free(encoded);
	st_rip3_bitmap_destroy(bmp);
	st_rip3_bitmap_destroy(bitmap);
	return 0;
}

static int
padding_and_masks(void)
{
	/* RADIO.ICN's documented first-row plane bytes, with pad bits set.
	 * Synthetic one-row height; this is not a full reference-file capture. */
	uint8_t radio_row[] = {14,0,0,0, 0,0,0xf8,0x3f,0xf8,0x3f,0xf8,0x3f};
	const uint8_t radio_expected[] = {7,7,7,7,7,0,0,0,0,0,7,7,7,7,7};
	const uint8_t mask[] = {2,0,0,0, 0xa0,0xa0,0xa0,0xa0};
	uint32_t palette[16];
	struct st_rip3_bitmap *bitmap = NULL;

	palette_values(palette);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, radio_row, sizeof(radio_row), palette) == 0);
	CHECK(bitmap->pixels->width == 15 && bitmap->pixels->height == 1);
	for (unsigned i = 0; i < 15; i++) CHECK(bitmap->pixels->primary[i] == radio_expected[i]);
	memset(radio_row + 4, 0, sizeof(radio_row) - 4);
	CHECK(bitmap->pixels->primary[0] == 7); /* Pixel ownership survives input changes. */
	st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, mask, sizeof(mask), palette) == 0);
	CHECK(bitmap->pixels->primary[0] == 15 && bitmap->pixels->primary[1] == 0
	    && bitmap->pixels->primary[2] == 15);
	st_rip3_bitmap_destroy(bitmap);
	return 0;
}

static int
dimension_boundaries(void)
{
	uint32_t palette[16];
	struct st_rip3_bitmap *bitmap = NULL;
	uint8_t *bytes = calloc(1, 65538);

	CHECK(bytes != NULL);
	palette_values(palette);
	/* WORD maximum width_minus_one must widen before adding one. */
	bytes[0] = 255; bytes[1] = 255;
	memset(bytes + 4, 0xaa, 8192);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, bytes, 32772, palette) == 0);
	CHECK(bitmap->pixels->width == 65536 && bitmap->pixels->height == 1);
	for (size_t i = 0; i < 65536; i++) CHECK(bitmap->pixels->primary[i] == (i % 2 == 0 ? 8u : 0u));
	st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	/* There is no legacy magic: a valid ICN can coincidentally start BM. */
	memset(bytes, 0, 65538); bytes[0] = 'B'; bytes[1] = 'M';
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, bytes, 9896, palette) == 0);
	CHECK(bitmap->pixels->width == 19779 && bitmap->pixels->height == 1);
	CHECK(bitmap->pixels->primary[19778] == 0);
	st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	/* Largest valid multiple-of-four payload: 65,528 bytes + six overhead. */
	memset(bytes, 0x80, 65538);
	bytes[0] = 0; bytes[1] = 0; bytes[2] = 0xfd; bytes[3] = 0x3f;
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, bytes, 65532, palette) == 0);
	CHECK(bitmap->pixels->width == 1 && bitmap->pixels->height == 16382);
	for (size_t i = 0; i < 16382; i++) CHECK(bitmap->pixels->primary[i] == 15);
	st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	bytes[2] = 0xfe; /* One more row exceeds the BGI format's 16-bit buffer. */
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, bytes, 65538, palette) == IO_ERROR_OUT_OF_RANGE && bitmap == NULL);
	memset(bytes, 255, 4);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, bytes, 4, palette) == IO_ERROR_OUT_OF_RANGE && bitmap == NULL);
	free(bytes);
	return 0;
}

static int
tails_and_failures(void)
{
	struct st_rip3_bitmap *bitmap = NULL, *original;
	uint32_t palette[16];
	uint8_t data[32];

	palette_values(palette);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, fixture, sizeof(fixture), palette) == 0);
	original = bitmap;
	for (size_t i = 0; i < 12; i++) {
		CHECK(st_rip3_bitmap_decode_legacy(&bitmap, fixture, i, palette) == IO_ERROR_INVALID_FORMAT);
		CHECK(bitmap == original && bitmap->pixels->primary[15] == 15);
	}
	CHECK(st_rip3_bitmap_decode_legacy(NULL, fixture, sizeof(fixture), palette) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, NULL, 0, palette) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, fixture, sizeof(fixture), NULL) == IO_ERROR_INVALID_ARGUMENT);
	palette[15] = 0x1000000;
	CHECK(st_rip3_bitmap_decode_legacy(&bitmap, fixture, sizeof(fixture), palette) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(bitmap == original);
	st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	palette_values(palette);
	memcpy(data, fixture, 12); memset(data + 12, 0xcd, sizeof(data) - 12);
	/* No tail, one-byte historical appendix tail, real two-byte tail, or
	 * arbitrary larger tail all decode identically. Tail bytes are not pixels. */
	for (size_t size = 12; size <= sizeof(data); size++) {
		CHECK(st_rip3_bitmap_decode_legacy(&bitmap, data, size, palette) == 0);
		for (unsigned i = 0; i < 16; i++) CHECK(bitmap->pixels->primary[i] == i);
		st_rip3_bitmap_destroy(bitmap); bitmap = NULL;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	CHECK(argc == 3);
	CHECK(reference_conversion(argv[1], argv[2]) == 0);
	CHECK(literal_and_conversion() == 0);
	CHECK(padding_and_masks() == 0);
	CHECK(dimension_boundaries() == 0);
	CHECK(tails_and_failures() == 0);
	return 0;
}
