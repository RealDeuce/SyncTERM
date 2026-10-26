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
	/* U+1D127 MUSICAL SYMBOL COMBINING STRESS was added in Unicode 18.0. */
	CHECK(st_unicode_canonical_combining_class(UINT32_C(0x1d127)) == 220);
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
	CHECK(st_unicode_canonical_decompose('A', NULL, 0) == 1);
	CHECK(st_unicode_canonical_decompose(UINT32_C(0x01fa), NULL, 0) == 3);
	CHECK(st_unicode_canonical_decompose('A', NULL, 1) == 0);
	CHECK(st_unicode_canonical_decompose(UINT32_MAX, output, 4) == 0);
	composition = 123;
	CHECK(!st_unicode_canonical_compose(UINT32_MAX, 'A', &composition));
	CHECK(composition == 123);
	/* Every algorithmic Hangul syllable must decompose and recompose. */
	for (uint32_t cp = UINT32_C(0xac00); cp < UINT32_C(0xd7a4); ++cp) {
		size_t count = st_unicode_canonical_decompose(cp, output, 4);
		CHECK(count == 2 || count == 3);
		CHECK(st_unicode_canonical_compose(output[0], output[1], &composition));
		if (count == 3)
			CHECK(st_unicode_canonical_compose(composition, output[2], &composition));
		CHECK(composition == cp);
	}
	return EXIT_SUCCESS;
}
