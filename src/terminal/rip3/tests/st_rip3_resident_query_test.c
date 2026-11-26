/* Stored source bytes, ownership and safe failure; no expression evaluator. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_resident_query.h"
#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)
extern bool st_rip3_resident_test_fail;

static bool
value(const struct st_rip3_resident_query *q, const char *expected)
{
	return q->length == strlen(expected) && (q->length == 0 || memcmp(q->bytes, expected, q->length) == 0);
}

int
main(void)
{
	struct st_rip3_resident_query a = {0}, b = {0};
	uint8_t                       bytes[] = "$TIME$<<NAME>>^m";
	CHECK(
	    st_rip3_resident_query_set(&a, bytes, sizeof(bytes) - 1, false) == 0 && value(&a, "$TIME$<<NAME>>^m"));
	memset(bytes, 'X', sizeof(bytes));
	CHECK(st_rip3_resident_query_copy(&b, &a) == 0 && value(&b, "$TIME$<<NAME>>^m"));
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)"$oFf$", 5, false) == 0 && a.bytes == NULL
	      && a.length == 0);
	CHECK(value(&b, "$TIME$<<NAME>>^m"));
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)" $OFF$", 6, false) == 0 && value(&a, " $OFF$"));
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)"$OFF$ ", 6, false) == 0 && value(&a, "$OFF$ "));
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)"$OFF$x", 6, false) == 0 && value(&a, "$OFF$x"));
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)"$OFF$\0suffix", 12, false) == 0 && a.bytes == NULL);
	uint8_t edge[4092];
	memset(edge, 'a', sizeof(edge));
	CHECK(st_rip3_resident_query_set(&a, edge, 1023, false) == 0 && a.length == 1023);
	CHECK(st_rip3_resident_query_set(&a, edge, 1024, false) == IO_ERROR_OUT_OF_RANGE && a.length == 1023);
	CHECK(st_rip3_resident_query_set(&a, NULL, 0, false) == IO_ERROR_INVALID_FORMAT && a.length == 1023);
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)"\0x", 2, false) == IO_ERROR_INVALID_FORMAT
	      && a.length == 1023);
	for (size_t i = 0; i < 1024; i++) {
		edge[2 * i]     = 0xc3;
		edge[2 * i + 1] = 0xa9;
	}
	CHECK(st_rip3_resident_query_set(&a, edge, 2046, true) == 0 && a.length == 2046);
	CHECK(st_rip3_resident_query_set(&a, edge, 2048, true) == IO_ERROR_OUT_OF_RANGE && a.length == 2046);
	CHECK(st_rip3_resident_query_set(&a, edge, 2046, false) == IO_ERROR_OUT_OF_RANGE && a.length == 2046);
	st_rip3_resident_test_fail = true;
	CHECK(st_rip3_resident_query_set(&b, (const uint8_t *)"lost", 4, false) == IO_ERROR_OUT_OF_MEMORY
	      && value(&b, "$TIME$<<NAME>>^m"));
	CHECK(st_rip3_resident_query_copy(&b, &a) == IO_ERROR_OUT_OF_MEMORY && value(&b, "$TIME$<<NAME>>^m"));
	CHECK(st_rip3_resident_query_set(&a, (const uint8_t *)"$OFF$", 5, false) == 0 && a.bytes == NULL);
	st_rip3_resident_test_fail = false;
	CHECK(st_rip3_resident_query_copy(&b, &a) == 0 && b.bytes == NULL);
	st_rip3_resident_query_clear(&a);
	st_rip3_resident_query_clear(&b);
	extern size_t st_rip3_resident_test_live;
	CHECK(st_rip3_resident_test_live == 0);
	return 0;
}
