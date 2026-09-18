/* Tests for generated Unicode grapheme and emoji oracles. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_grapheme.h"

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
	const uint32_t family[] = {
		UINT32_C(0x1f468), UINT32_C(0x200d), UINT32_C(0x1f469),
		UINT32_C(0x200d), UINT32_C(0x1f467), 'A',
	};
	const uint32_t flag[] = {UINT32_C(0x1f1fa), UINT32_C(0x1f1f8)};
	const uint32_t keycap[] = {'#', UINT32_C(0xfe0f), UINT32_C(0x20e3)};

	CHECK(st_unicode_grapheme_break('\r') == ST_UNICODE_GRAPHEME_CR);
	CHECK(st_unicode_grapheme_break(UINT32_C(0x0301))
	    == ST_UNICODE_GRAPHEME_EXTEND);
	CHECK(st_unicode_grapheme_break(UINT32_C(0x200d))
	    == ST_UNICODE_GRAPHEME_ZWJ);
	CHECK(st_unicode_grapheme_break(UINT32_C(0x1f1e6))
	    == ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR);
	CHECK(st_unicode_grapheme_break(UINT32_C(0xac00))
	    == ST_UNICODE_GRAPHEME_LV);
	CHECK(st_unicode_indic_conjunct_break(UINT32_C(0x0915))
	    == ST_UNICODE_INCB_CONSONANT);
	CHECK(st_unicode_indic_conjunct_break(UINT32_C(0x094d))
	    == ST_UNICODE_INCB_LINKER);
	CHECK(st_unicode_extended_pictographic(UINT32_C(0x00a9)));
	CHECK(!st_unicode_extended_pictographic('A'));
	CHECK(st_unicode_rgi_emoji_prefix(family,
	    sizeof(family) / sizeof(family[0])) == 5);
	CHECK(st_unicode_rgi_emoji_prefix(flag,
	    sizeof(flag) / sizeof(flag[0])) == 2);
	CHECK(st_unicode_rgi_emoji_prefix(keycap,
	    sizeof(keycap) / sizeof(keycap[0])) == 3);
	CHECK(st_unicode_rgi_emoji_prefix(family, 0) == 0);
	CHECK(st_unicode_rgi_emoji_prefix(NULL, 1) == 0);
	return EXIT_SUCCESS;
}
