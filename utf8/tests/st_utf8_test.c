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
	return EXIT_SUCCESS;
}
