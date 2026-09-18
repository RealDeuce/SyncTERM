/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_utf8.h"

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", \
    __FILE__, __LINE__); return EXIT_FAILURE; } } while (0)

static int
check_scalar(uint32_t value, const unsigned char *encoded, size_t length)
{
	char output[4] = {1, 2, 3, 4};
	uint32_t decoded = UINT32_MAX;
	size_t consumed = SIZE_MAX;

	CHECK(st_utf8_encode_one(NULL, 0, value) == (ptrdiff_t)length);
	CHECK(st_utf8_encode_one(output, length, value) == (ptrdiff_t)length);
	CHECK(memcmp(output, encoded, length) == 0);
	CHECK(st_utf8_decode_one(&decoded, &consumed, output, length) == 0);
	CHECK(decoded == value && consumed == length);
	return 0;
}

static int
check_boundaries(void)
{
	static const struct {
		uint32_t scalar;
		unsigned char bytes[4];
		size_t size;
	} vectors[] = {
		{0x7ff, {0xdf, 0xbf}, 2},
		{0xd7ff, {0xed, 0x9f, 0xbf}, 3},
		{0xe000, {0xee, 0x80, 0x80}, 3},
		{0xffff, {0xef, 0xbf, 0xbf}, 3},
		{0x10000, {0xf0, 0x90, 0x80, 0x80}, 4},
	};
	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
		CHECK(check_scalar(vectors[i].scalar, vectors[i].bytes,
		    vectors[i].size) == 0);
		for (size_t size = 0; size < vectors[i].size; ++size) {
			uint32_t scalar = 123;
			size_t consumed = 456;
			char buffer[4] = {1, 2, 3, 4};
			CHECK(st_utf8_decode_one(&scalar, &consumed,
			    vectors[i].bytes, size) == IO_ERROR_INVALID_ENCODING);
			CHECK(scalar == 123 && consumed == 456);
			CHECK(st_utf8_encode_one(buffer, size, vectors[i].scalar)
			    == (ptrdiff_t)vectors[i].size);
			CHECK(memcmp(buffer, "\1\2\3\4", 4) == 0);
		}
	}
	/* Every scalar, not arbitrary machine representations. Known vectors
	 * above independently check the encoding rather than only round trips. */
	for (uint32_t scalar = 0; scalar <= 0x110000; ++scalar) {
		char buffer[5] = {1, 2, 3, 4, 5};
		uint32_t decoded = UINT32_MAX;
		size_t consumed = SIZE_MAX;
		bool valid = scalar < 0x110000
		    && !(scalar >= 0xd800 && scalar <= 0xdfff);
		CHECK(st_unicode_scalar_valid(scalar) == valid);
		ptrdiff_t length = st_utf8_encode_one(buffer, 4, scalar);
		if (!valid) {
			CHECK(length == IO_ERROR_INVALID_ARGUMENT);
			CHECK(memcmp(buffer, "\1\2\3\4\5", 5) == 0);
			continue;
		}
		CHECK(length >= 1 && length <= 4 && buffer[4] == 5);
		CHECK(st_utf8_decode_one(&decoded, &consumed, buffer, 5) == 0);
		CHECK(decoded == scalar && consumed == (size_t)length);
	}
	return 0;
}

static int
check_invalid_sequences(void)
{
	static const unsigned char invalid[][4] = {
		{0xe0, 0x80, 0x80, 0}, {0xf0, 0x80, 0x80, 0x80},
		{0xf4, 0x90, 0x80, 0x80}, {0xed, 0xbf, 0xbf, 0},
		{0xc2, 0x7f, 0, 0}, {0xe1, 0x80, 0xc0, 0},
		{0xf1, 0x80, 0x80, 0xff},
	};
	uint32_t scalar = 123;
	size_t consumed = 456;
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		CHECK(st_utf8_decode_one(&scalar, &consumed, invalid[i], 4)
		    == IO_ERROR_INVALID_ENCODING);
		CHECK(scalar == 123 && consumed == 456);
	}
	for (unsigned int i = 0x80; i <= 0xff; ++i) {
		unsigned char byte = (unsigned char)i;
		CHECK(st_utf8_decode_one(&scalar, &consumed, &byte, 1)
		    == IO_ERROR_INVALID_ENCODING);
		CHECK(scalar == 123 && consumed == 456);
	}
	CHECK(st_utf8_decode_one(&scalar, &consumed, NULL, 0)
	    == IO_ERROR_INVALID_ENCODING);
	CHECK(st_utf8_decode_one(&scalar, &consumed, NULL, 1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_utf8_decode_one(NULL, &consumed, "a", 1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_utf8_decode_one(&scalar, NULL, "a", 1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(scalar == 123 && consumed == 456);
	CHECK(st_utf8_encode_one(NULL, 1, 'a') == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_utf8_encode_one(NULL, 0, UINT32_MAX) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

int
main(void)
{
	static const unsigned char zero[] = {0x00};
	static const unsigned char ascii[] = {0x7f};
	static const unsigned char two[] = {0xc2, 0x80};
	static const unsigned char three[] = {0xe0, 0xa0, 0x80};
	static const unsigned char four[] = {0xf4, 0x8f, 0xbf, 0xbf};
	static const unsigned char overlong[] = {0xc0, 0x80};
	static const unsigned char surrogate[] = {0xed, 0xa0, 0x80};
	static const unsigned char too_large[] = {0xf4, 0x90, 0x80, 0x80};
	uint32_t value = 123;
	size_t consumed = 456;
	char unchanged[3] = {'a', 'b', 'c'};

	CHECK(check_scalar(0, zero, sizeof(zero)) == 0);
	CHECK(check_scalar(0x7f, ascii, sizeof(ascii)) == 0);
	CHECK(check_scalar(0x80, two, sizeof(two)) == 0);
	CHECK(check_scalar(0x800, three, sizeof(three)) == 0);
	CHECK(check_scalar(0x10ffff, four, sizeof(four)) == 0);
	CHECK(st_utf8_decode_one(&value, &consumed, overlong,
	    sizeof(overlong)) == IO_ERROR_INVALID_ENCODING);
	CHECK(st_utf8_decode_one(&value, &consumed, surrogate,
	    sizeof(surrogate)) == IO_ERROR_INVALID_ENCODING);
	CHECK(st_utf8_decode_one(&value, &consumed, too_large,
	    sizeof(too_large)) == IO_ERROR_INVALID_ENCODING);
	CHECK(st_utf8_decode_one(&value, &consumed, four, 3)
	    == IO_ERROR_INVALID_ENCODING);
	CHECK(value == 123 && consumed == 456);
	CHECK(st_utf8_encode_one(unchanged, 2, 0x20ac) == 3);
	CHECK(memcmp(unchanged, "abc", 3) == 0);
	CHECK(st_utf8_encode_one(unchanged, sizeof(unchanged), 0xd800)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(!st_unicode_scalar_valid(0xdfff));
	CHECK(!st_unicode_scalar_valid(0x110000));
	CHECK(check_boundaries() == 0);
	CHECK(check_invalid_sequences() == 0);
	return EXIT_SUCCESS;
}
