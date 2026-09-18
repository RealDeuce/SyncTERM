/* Tests for locale-independent IBM Code Page 437 conversion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cp437.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	uint8_t byte;

	for (unsigned value = 0; value <= UINT8_MAX; ++value) {
		uint32_t codepoint = st_cp437_to_unicode((uint8_t)value, true);

		CHECK(st_cp437_from_unicode(&byte, codepoint));
		CHECK(byte == value);
	}
	CHECK(st_cp437_to_unicode(0x1e, false) == 0x1e);
	CHECK(st_cp437_to_unicode(0x1e, true) == UINT32_C(0x25b2));
	CHECK(!st_cp437_from_unicode(&byte, UINT32_C(0x1f642)));
	CHECK(!st_cp437_from_unicode(NULL, 'A'));
	return 0;
}
