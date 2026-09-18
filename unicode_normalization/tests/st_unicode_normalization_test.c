/* Tests for generated canonical-normalization primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_normalization.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

int
main(void)
{
	uint32_t output[4] = {1, 2, 3, 4};
	uint32_t composition = 0;

	CHECK(st_unicode_canonical_combining_class('A') == 0);
	CHECK(st_unicode_canonical_combining_class(UINT32_C(0x0301)) == 230);
	CHECK(st_unicode_canonical_decompose(UINT32_C(0x01fa), output, 4) == 3);
	CHECK(output[0] == 'A' && output[1] == UINT32_C(0x030a)
	    && output[2] == UINT32_C(0x0301));
	CHECK(st_unicode_canonical_decompose(UINT32_C(0xac01), output, 4) == 3);
	CHECK(output[0] == UINT32_C(0x1100)
	    && output[1] == UINT32_C(0x1161)
	    && output[2] == UINT32_C(0x11a8));
	output[0] = 9;
	CHECK(st_unicode_canonical_decompose(UINT32_C(0x01fa), output, 1) == 3);
	CHECK(output[0] == 9);
	CHECK(st_unicode_canonical_decompose(UINT32_C(0xd800), output, 4) == 0);
	CHECK(st_unicode_canonical_compose('A', UINT32_C(0x030a),
	    &composition));
	CHECK(composition == UINT32_C(0x00c5));
	CHECK(st_unicode_canonical_compose(UINT32_C(0x00c5),
	    UINT32_C(0x0301), &composition));
	CHECK(composition == UINT32_C(0x01fa));
	CHECK(st_unicode_canonical_compose(UINT32_C(0x1100),
	    UINT32_C(0x1161), &composition));
	CHECK(composition == UINT32_C(0xac00));
	CHECK(!st_unicode_canonical_compose(UINT32_C(0x0308),
	    UINT32_C(0x0301), &composition));
	CHECK(!st_unicode_canonical_compose('A', 'B', NULL));
	return EXIT_SUCCESS;
}
