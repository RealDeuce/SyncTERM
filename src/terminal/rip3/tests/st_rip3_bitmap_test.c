/* Authored BMP rows/palettes; no emulator-generated reference images. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; \
} } while (0)

static void
put16(uint8_t *data, unsigned value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void
put32(uint8_t *data, uint32_t value)
{
	put16(data, value & 65535);
	put16(data + 2, value >> 16);
}

static size_t
fixture(uint8_t *data, unsigned depth, unsigned colours, const uint8_t *rows,
	size_t row_size)
{
	size_t offset = 54 + colours * 4;
	size_t size = offset + row_size;
	memset(data, 0, size);
	data[0] = 'B'; data[1] = 'M';
	put32(data + 2, (uint32_t)size); put32(data + 10, (uint32_t)offset);
	put32(data + 14, 40); put32(data + 18, 5); put32(data + 22, 2);
	put16(data + 26, 1); put16(data + 28, depth);
	if (colours != 0 && colours != (1U << depth)) put32(data + 46, colours);
	for (unsigned i = 0; i < colours; ++i) {
		data[54 + 4 * i] = (uint8_t)(i ^ 0x55);
		data[55 + 4 * i] = (uint8_t)(255 - i);
		data[56 + 4 * i] = (uint8_t)i;
		data[57 + 4 * i] = 0xee; /* reserved byte is not alpha */
	}
	memcpy(data + offset, rows, row_size);
	return size;
}

static int
test_four_bit_stride(void)
{
	/* Both images are 9x2. Short rows share the last pixel's high
	 * nibble with the next stored row; the top right pixel is absent. */
	static const uint8_t standard_rows[] = {
		0x12,0x34,0x56,0x78,0x9f,0xee,0xee,0xee,
		0xab,0xcd,0xef,0x01,0x2f,0xee,0xee,0xee,
	};
	static const uint32_t standard_pixels[] = {10,11,12,13,14,15,0,1,2, 1,2,3,4,5,6,7,8,9};
	static const uint8_t short_rows[] = {0x12,0x34,0x56,0x78, 0xab,0xcd,0xef,0x01};
	static const uint32_t short_pixels[] = {10,11,12,13,14,15,0,1,0, 1,2,3,4,5,6,7,8,10};
	uint8_t bytes[118 + sizeof(standard_rows)];
	struct st_rip3_bitmap *result = NULL;
	size_t size = fixture(bytes, 4, 16, standard_rows, sizeof(standard_rows));
	put32(bytes + 18, 9);
	CHECK(st_rip3_bitmap_decode(&result, bytes, size) == 0);
	CHECK(memcmp(result->pixels->primary, standard_pixels, sizeof(standard_pixels)) == 0);
	st_rip3_bitmap_destroy(result);

	size = fixture(bytes, 4, 16, short_rows, sizeof(short_rows));
	put32(bytes + 18, 9);
	bytes[size] = 0xff; /* Outside bfSize, even when present in the buffer. */
	CHECK(st_rip3_bitmap_decode(&result, bytes, size + 1) == 0);
	CHECK(memcmp(result->pixels->primary, short_pixels, sizeof(short_pixels)) == 0);
	st_rip3_bitmap_destroy(result);
	result = NULL;
	put32(bytes + 2, (uint32_t)size - 1);
	CHECK(st_rip3_bitmap_decode(&result, bytes, size) == IO_ERROR_INVALID_FORMAT);
	put32(bytes + 2, (uint32_t)size + 1);
	CHECK(st_rip3_bitmap_decode(&result, bytes, size + 1) == IO_ERROR_INVALID_FORMAT);
	put32(bytes + 18, 1);
	put32(bytes + 2, 118);
	CHECK(st_rip3_bitmap_decode(&result, bytes, 118) == IO_ERROR_INVALID_FORMAT);
	CHECK(result == NULL);
	return 0;
}

int
main(void)
{
	static const uint8_t rows1[] = {0xa7,0xcc,0xcc,0xcc, 0x5f,0xcc,0xcc,0xcc};
	static const uint8_t rows4[] = {0x43,0x21,0x0f,0xcc, 0x01,0x23,0x4f,0xcc};
	static const uint8_t rows8[] = {255,127,16,15,0,0xcc,0xcc,0xcc, 0,15,16,127,255,0xcc,0xcc,0xcc};
	static const uint8_t rows24[] = {
		0x21,0x43,0x65, 0,0,0, 0x80,0x80,0x80, 0xff,0xff,0, 0xff,0,0xff, 0xcc,
		0,0,0xff, 0,0xff,0, 0xff,0,0, 0xff,0xff,0xff, 0x56,0x34,0x12, 0xcc,
	};
	static const uint8_t rows16[] = {
		0,0x80,0x10,0x42,0xe0,0x7f,0x1f,0x7c,0xff,3,0xcc,0xcc,
		0,0x7c,0xe0,3,0x1f,0,0xff,0xff,0x43,8,0xcc,0xcc,
	};
	static const uint8_t written16[] = {
		0,0,0x10,0x42,0xe0,0x7f,0x1f,0x7c,0xff,3,0,0,
		0,0x7c,0xe0,3,0x1f,0,0xff,0x7f,0x43,8,0,0,
	};
	static const uint8_t rows32[] = {
		0x21,0x43,0x65,255,0,0,0,17,0x80,0x80,0x80,0,0xff,0xff,0,90,0xff,0,0xff,55,
		0,0,0xff,0,0,0xff,0,1,0xff,0,0,128,0xff,0xff,0xff,255,0x56,0x34,0x12,127,
	};
	static const uint8_t written32[] = {
		0x21,0x43,0x65,0,0,0,0,0,0x80,0x80,0x80,0,0xff,0xff,0,0,0xff,0,0xff,0,
		0,0,0xff,0,0,0xff,0,0,0xff,0,0,0,0xff,0xff,0xff,0,0x56,0x34,0x12,0,
	};
	/* RGB555 expansion uses the geometric renderer's floor(v*255/31)
	 * policy; bit15 and the 32-bit high byte carry no alpha. */
	static const uint32_t expected16[] = {0x80ff0000,0x8000ff00,0x800000ff,0x80ffffff,0x80101018,
	    0x80000000,0x80838383,0x80ffff00,0x80ff00ff,0x8000ffff};
	static const uint32_t expected1[] = {0,1,0,1,1, 1,0,1,0,0};
	static const uint32_t expected4[] = {0,1,2,3,4, 4,3,2,1,0};
	static const uint32_t expected8[] = {0,15,16,127,255, 255,127,16,15,0};
	static const uint32_t expected24[] = {
		0x80ff0000,0x8000ff00,0x800000ff,0x80ffffff,0x80123456,
		0x80654321,0x80000000,0x80808080,0x8000ffff,0x80ff00ff,
	};
	static const uint8_t written1[] = {0xa0,0,0,0, 0x58,0,0,0};
	static const uint8_t written4[] = {0x43,0x21,0,0, 0x01,0x23,0x40,0};
	static const uint8_t written8[] = {255,127,16,15,0,0,0,0, 0,15,16,127,255,0,0,0};
	static const uint8_t written24[] = {
		0x21,0x43,0x65, 0,0,0, 0x80,0x80,0x80, 0xff,0xff,0, 0xff,0,0xff, 0,
		0,0,0xff, 0,0xff,0, 0xff,0,0, 0xff,0xff,0xff, 0x56,0x34,0x12, 0,
	};
	static const struct {
		unsigned depth, colours;
		const uint8_t *rows;
		size_t row_size;
		const uint32_t *expected;
		const uint8_t *written;
	} cases[] = {
		{1,2,rows1,sizeof(rows1),expected1,written1},
		{4,16,rows4,sizeof(rows4),expected4,written4},
		{8,256,rows8,sizeof(rows8),expected8,written8},
		{16,0,rows16,sizeof(rows16),expected16,written16},
		{24,0,rows24,sizeof(rows24),expected24,written24},
		{32,0,rows32,sizeof(rows32),expected24,written32},
	};
	uint8_t bytes[1200], bad[sizeof(bytes)];
	struct st_rip3_bitmap sentinel = {0}, *result = &sentinel;
	size_t size = 0;

	CHECK(test_four_bit_stride() == 0);
	for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); ++c) {
		size = fixture(bytes, cases[c].depth, cases[c].colours, cases[c].rows, cases[c].row_size);
		CHECK(st_rip3_bitmap_decode(&result, bytes, size) == 0);
		CHECK(result->bit_depth == cases[c].depth && result->palette_size == cases[c].colours);
		CHECK(result->pixels->width == 5 && result->pixels->height == 2 && result->pixels->alternate == NULL);
		CHECK(memcmp(result->pixels->primary, cases[c].expected, 10 * sizeof(uint32_t)) == 0);
		if (result->palette_size != 0) {
			CHECK(result->palette[0] == 0x00ff55 && result->palette[1] == 0x01fe54);
		}
		{
			uint8_t *encoded = NULL;
			size_t length = 0, offset = 54 + cases[c].colours * 4;
			struct st_rip3_bitmap *again = NULL;
			CHECK(st_rip3_bitmap_encode(&encoded, &length, result) == 0 && length == size);
			CHECK(encoded[0] == 'B' && encoded[1] == 'M' && encoded[14] == 40);
			CHECK(encoded[38] == 5 && encoded[42] == 2 && encoded[34] == 0);
			CHECK(memcmp(encoded + offset, cases[c].written, cases[c].row_size) == 0);
			if (cases[c].colours != 0) CHECK(encoded[54] == 0x55 && encoded[55] == 0xff && encoded[56] == 0 && encoded[57] == 0);
			CHECK(st_rip3_bitmap_decode(&again, encoded, length) == 0);
			CHECK(memcmp(again->pixels->primary, cases[c].expected, 10 * sizeof(uint32_t)) == 0);
			st_rip3_bitmap_destroy(again);
			free(encoded);
		}
		st_rip3_bitmap_destroy(result);
		result = &sentinel;
		for (size_t length = 0; length < size; ++length) {
			CHECK(st_rip3_bitmap_decode(&result, bytes, length) == IO_ERROR_INVALID_FORMAT);
			CHECK(result == &sentinel);
		}
	}
	CHECK(st_rip3_bitmap_decode(NULL, bytes, size) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_bitmap_decode(&result, NULL, size) == IO_ERROR_INVALID_ARGUMENT);
	{
		static const struct { unsigned offset; uint32_t value; int status; } broken[] = {
			{0,0,IO_ERROR_INVALID_FORMAT}, {2,0,IO_ERROR_INVALID_FORMAT},
			{2,UINT32_MAX,IO_ERROR_INVALID_FORMAT}, {10,0,IO_ERROR_INVALID_FORMAT},
			{10,UINT32_MAX,IO_ERROR_INVALID_FORMAT}, {14,12,IO_ERROR_NOT_SUPPORTED},
			{14,124,IO_ERROR_NOT_SUPPORTED}, {18,0,IO_ERROR_INVALID_FORMAT},
			{22,0,IO_ERROR_INVALID_FORMAT}, {22,UINT32_MAX,IO_ERROR_NOT_SUPPORTED},
			{26,0,IO_ERROR_INVALID_FORMAT}, {28,15,IO_ERROR_NOT_SUPPORTED},
			{30,1,IO_ERROR_NOT_SUPPORTED},
		};
		for (size_t i = 0; i < sizeof(broken) / sizeof(*broken); ++i) {
			memcpy(bad, bytes, size);
			if (broken[i].offset == 26 || broken[i].offset == 28)
				put16(bad + broken[i].offset, (unsigned)broken[i].value);
			else put32(bad + broken[i].offset, broken[i].value);
			CHECK(st_rip3_bitmap_decode(&result, bad, size) == broken[i].status);
			CHECK(result == &sentinel);
		}
	}
	{
		static const uint8_t rows[] = {1,0,1,0,0,0xcc,0xcc,0xcc, 0,1,0,1,1,0xcc,0xcc,0xcc};
		size = fixture(bytes, 8, 2, rows, sizeof(rows));
		CHECK(st_rip3_bitmap_decode(&result, bytes, size) == 0);
		CHECK(result->palette_size == 2 && memcmp(result->pixels->primary, expected1, sizeof(expected1)) == 0);
		{
			uint8_t *encoded = NULL;
			size_t length = 0;
			CHECK(st_rip3_bitmap_encode(&encoded, &length, result) == 0);
			CHECK(length == 78 && encoded[10] == 62 && encoded[46] == 2);
			CHECK(encoded[62] == 1 && encoded[70] == 0 && encoded[75] == 0);
			free(encoded);
		}
		st_rip3_bitmap_destroy(result); result = &sentinel;
		memcpy(bad, bytes, size); put32(bad + 46, 257);
		CHECK(st_rip3_bitmap_decode(&result, bad, size) == IO_ERROR_INVALID_FORMAT && result == &sentinel);
		memcpy(bad, bytes, size); put32(bad + 46, 0);
		CHECK(st_rip3_bitmap_decode(&result, bad, size) == IO_ERROR_INVALID_FORMAT && result == &sentinel);
		memcpy(bad, bytes, size); bad[62] = 2;
		CHECK(st_rip3_bitmap_decode(&result, bad, size) == IO_ERROR_INVALID_FORMAT && result == &sentinel);
		memcpy(bad, bytes, size); put32(bad + 18, INT32_MAX); put32(bad + 22, INT32_MAX);
		CHECK(st_rip3_bitmap_decode(&result, bad, size) < 0 && result == &sentinel);
	}
	{
		uint32_t pixel = 3;
		struct st_pixel_buffer pixels = {.primary = &pixel, .width = 1, .height = 1};
		struct st_rip3_bitmap invalid = {.pixels = &pixels, .palette_size = 2, .bit_depth = 8};
		uint8_t saved_byte = 9, *output = &saved_byte;
		size_t length = 77;
		CHECK(st_rip3_bitmap_encode(&output, &length, &invalid) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(output == &saved_byte && length == 77 && saved_byte == 9);
		pixel = 0; invalid.palette[0] = 0x1000000;
		CHECK(st_rip3_bitmap_encode(&output, &length, &invalid) == IO_ERROR_INVALID_ARGUMENT);
		invalid.palette[0] = 0; invalid.palette_size = 0; invalid.bit_depth = 24;
		CHECK(st_rip3_bitmap_encode(&output, &length, &invalid) == IO_ERROR_INVALID_ARGUMENT);
		invalid.bit_depth = 15;
		CHECK(st_rip3_bitmap_encode(&output, &length, &invalid) == IO_ERROR_NOT_SUPPORTED);
		invalid.bit_depth = 24; pixels.width = 0;
		CHECK(st_rip3_bitmap_encode(&output, &length, &invalid) == IO_ERROR_INVALID_ARGUMENT);
		pixels.width = INT32_MAX; pixels.height = INT32_MAX;
		CHECK(st_rip3_bitmap_encode(&output, &length, &invalid) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_bitmap_encode(NULL, &length, &invalid) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_bitmap_encode(&output, NULL, &invalid) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_bitmap_encode(&output, &length, NULL) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(output == &saved_byte && length == 77 && saved_byte == 9);
	}
	st_rip3_bitmap_destroy(NULL);
	return 0;
}
