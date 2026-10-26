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

static int
check_incremental_scalar(uint32_t expected, const uint8_t *bytes, size_t size)
{
	for (size_t split = 0; split <= size; ++split) {
		struct st_utf8_decoder decoder = {0};
		struct st_utf8_decode_event event;
		size_t consumed;

		CHECK(st_utf8_decoder_feed(&decoder, bytes, split, &consumed,
		    &event) == 0);
		CHECK(consumed == split);
		if (split == size) {
			CHECK(event.kind == ST_UTF8_DECODE_EVENT_SCALAR);
			CHECK(event.data.scalar == expected);
		}
		else {
			CHECK(event.kind == ST_UTF8_DECODE_EVENT_NONE);
			CHECK(st_utf8_decoder_incomplete(&decoder) == (split != 0));
			CHECK(st_utf8_decoder_feed(&decoder, bytes + split,
			    size - split, &consumed, &event) == 0);
			CHECK(consumed == size - split);
			CHECK(event.kind == ST_UTF8_DECODE_EVENT_SCALAR);
			CHECK(event.data.scalar == expected);
		}
		CHECK(!st_utf8_decoder_incomplete(&decoder));
		CHECK(st_utf8_decoder_flush(&decoder, &event) == 0);
		CHECK(event.kind == ST_UTF8_DECODE_EVENT_NONE);
	}
	return 0;
}

static int
check_incremental_valid(void)
{
	static const uint8_t ascii[] = {'A'};
	static const uint8_t nul[] = {0};
	static const uint8_t two[] = {0xc2, 0xa2};
	static const uint8_t three[] = {0xe2, 0x82, 0xac};
	static const uint8_t four[] = {0xf0, 0x9f, 0x98, 0x80};
	struct st_utf8_decoder decoder;
	struct st_utf8_decode_event event;
	size_t consumed;

	CHECK(check_incremental_scalar('A', ascii, sizeof(ascii)) == 0);
	CHECK(check_incremental_scalar(0, nul, sizeof(nul)) == 0);
	CHECK(check_incremental_scalar(0xa2, two, sizeof(two)) == 0);
	CHECK(check_incremental_scalar(0x20ac, three, sizeof(three)) == 0);
	CHECK(check_incremental_scalar(0x1f600, four, sizeof(four)) == 0);

	st_utf8_decoder_init(&decoder);
	CHECK(st_utf8_decoder_feed(&decoder, "AB", 2, &consumed, &event) == 0);
	CHECK(consumed == 1 && event.kind == ST_UTF8_DECODE_EVENT_SCALAR
	    && event.data.scalar == 'A');
	CHECK(st_utf8_decoder_feed(&decoder, "B", 1, &consumed, &event) == 0);
	CHECK(consumed == 1 && event.kind == ST_UTF8_DECODE_EVENT_SCALAR
	    && event.data.scalar == 'B');
	return 0;
}

static int
check_incremental_malformed(void)
{
	static const uint8_t prefix[] = {0xe2, 0x82};
	static const uint8_t maximal[] = {0xe0, 0x80, 0x80, 'B'};
	static const uint8_t invalid_starts[] = {0xc0, 0xf5, 0x80};
	static const uint8_t invalid_ranges[] = {
		0xed, 0xa0, 0x80, 0xf4, 0x90, 0x80, 0x80,
	};
	struct st_utf8_decoder decoder = {0};
	struct st_utf8_decode_event event;
	size_t consumed;
	size_t offset;

	CHECK(st_utf8_decoder_feed(&decoder, prefix, sizeof(prefix), &consumed,
	    &event) == 0);
	CHECK(consumed == sizeof(prefix)
	    && event.kind == ST_UTF8_DECODE_EVENT_NONE);
	CHECK(st_utf8_decoder_feed(&decoder, "A", 1, &consumed, &event) == 0);
	CHECK(consumed == 0 && event.kind == ST_UTF8_DECODE_EVENT_MALFORMED
	    && event.data.malformed.size == sizeof(prefix)
	    && memcmp(event.data.malformed.bytes, prefix, sizeof(prefix)) == 0);
	CHECK(st_utf8_decoder_feed(&decoder, "A", 1, &consumed, &event) == 0);
	CHECK(consumed == 1 && event.kind == ST_UTF8_DECODE_EVENT_SCALAR
	    && event.data.scalar == 'A');

	offset = 0;
	for (size_t i = 0; i < 3; ++i) {
		CHECK(st_utf8_decoder_feed(&decoder, maximal + offset,
		    sizeof(maximal) - offset, &consumed, &event) == 0);
		CHECK(event.kind == ST_UTF8_DECODE_EVENT_MALFORMED
		    && event.data.malformed.size == 1
		    && event.data.malformed.bytes[0] == maximal[i]);
		offset += consumed;
	}
	CHECK(offset == 3);
	CHECK(st_utf8_decoder_feed(&decoder, maximal + offset,
	    sizeof(maximal) - offset, &consumed, &event) == 0);
	CHECK(consumed == 1 && event.kind == ST_UTF8_DECODE_EVENT_SCALAR
	    && event.data.scalar == 'B');

	for (size_t i = 0; i < sizeof(invalid_starts); ++i) {
		CHECK(st_utf8_decoder_feed(&decoder, invalid_starts + i, 1,
		    &consumed, &event) == 0);
		CHECK(consumed == 1
		    && event.kind == ST_UTF8_DECODE_EVENT_MALFORMED
		    && event.data.malformed.size == 1
		    && event.data.malformed.bytes[0] == invalid_starts[i]);
	}

	offset = 0;
	while (offset < sizeof(invalid_ranges)) {
		CHECK(st_utf8_decoder_feed(&decoder, invalid_ranges + offset,
		    sizeof(invalid_ranges) - offset, &consumed, &event) == 0);
		CHECK(event.kind == ST_UTF8_DECODE_EVENT_MALFORMED
		    && event.data.malformed.size == 1);
		offset += consumed;
	}
	return 0;
}

static int
check_incremental_lifecycle(void)
{
	static const uint8_t prefix[] = {0xf0, 0x9f};
	struct st_utf8_decoder decoder = {0};
	struct st_utf8_decoder saved;
	struct st_utf8_decode_event event;
	struct st_utf8_decode_event saved_event;
	size_t consumed;

	CHECK(st_utf8_decoder_feed(&decoder, prefix, sizeof(prefix), &consumed,
	    &event) == 0);
	CHECK(st_utf8_decoder_flush(&decoder, &event) == 0);
	CHECK(event.kind == ST_UTF8_DECODE_EVENT_MALFORMED
	    && event.data.malformed.size == sizeof(prefix)
	    && memcmp(event.data.malformed.bytes, prefix, sizeof(prefix)) == 0);
	CHECK(!st_utf8_decoder_incomplete(&decoder));
	CHECK(st_utf8_decoder_feed(&decoder, "A", 1, &consumed, &event) == 0
	    && consumed == 1 && event.kind == ST_UTF8_DECODE_EVENT_SCALAR
	    && event.data.scalar == 'A');

	CHECK(st_utf8_decoder_feed(&decoder, prefix, sizeof(prefix), &consumed,
	    &event) == 0);
	st_utf8_decoder_reset(&decoder);
	CHECK(!st_utf8_decoder_incomplete(&decoder));
	CHECK(st_utf8_decoder_flush(&decoder, &event) == 0
	    && event.kind == ST_UTF8_DECODE_EVENT_NONE);
	st_utf8_decoder_init(NULL);
	st_utf8_decoder_reset(NULL);
	CHECK(!st_utf8_decoder_incomplete(NULL));

	CHECK(st_utf8_decoder_feed(&decoder, prefix, sizeof(prefix), &consumed,
	    &event) == 0);
	saved = decoder;
	CHECK(st_utf8_decoder_feed(&decoder, NULL, 0, &consumed, &event) == 0);
	CHECK(consumed == 0 && event.kind == ST_UTF8_DECODE_EVENT_NONE);
	CHECK(memcmp(&decoder, &saved, sizeof(decoder)) == 0);
	consumed = 123;
	memset(&event, 0x5a, sizeof(event));
	saved_event = event;
	CHECK(st_utf8_decoder_feed(&decoder, NULL, 1, &consumed, &event)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&decoder, &saved, sizeof(decoder)) == 0);
	CHECK(consumed == 123 && memcmp(&event, &saved_event, sizeof(event)) == 0);
	CHECK(st_utf8_decoder_feed(NULL, "A", 1, &consumed, &event)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(consumed == 123 && memcmp(&event, &saved_event, sizeof(event)) == 0);
	CHECK(st_utf8_decoder_feed(&decoder, "A", 1, NULL, &event)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&decoder, &saved, sizeof(decoder)) == 0
	    && memcmp(&event, &saved_event, sizeof(event)) == 0);
	CHECK(st_utf8_decoder_feed(&decoder, "A", 1, &consumed, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&decoder, &saved, sizeof(decoder)) == 0
	    && consumed == 123);
	CHECK(st_utf8_decoder_flush(&decoder, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&decoder, &saved, sizeof(decoder)) == 0);
	CHECK(st_utf8_decoder_flush(NULL, &event) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(&event, &saved_event, sizeof(event)) == 0);
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
	CHECK(check_incremental_valid() == 0);
	CHECK(check_incremental_malformed() == 0);
	CHECK(check_incremental_lifecycle() == 0);
	return EXIT_SUCCESS;
}
