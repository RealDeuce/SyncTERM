/* Tests for locale-independent Unicode scalar widths. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_width.h"

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
	CHECK(st_unicode_east_asian_width('A') == ST_UNICODE_EAW_NARROW);
	CHECK(st_unicode_east_asian_width(UINT32_C(0x00a1))
	    == ST_UNICODE_EAW_AMBIGUOUS);
	CHECK(st_unicode_east_asian_width(UINT32_C(0x4e00))
	    == ST_UNICODE_EAW_WIDE);
	CHECK(st_unicode_east_asian_width(UINT32_C(0xd800))
	    == ST_UNICODE_EAW_INVALID);
	CHECK(st_unicode_emoji_presentation(UINT32_C(0x1f1e6)));
	CHECK(!st_unicode_emoji_presentation(UINT32_C(0x00a9)));
	CHECK(st_unicode_columns('A') == 1);
	CHECK(st_unicode_columns(UINT32_C(0x00a1)) == 1);
	CHECK(st_unicode_columns(UINT32_C(0x1100)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x4e00)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x1f600)) == 2);
	/* Regional indicators are emoji-presentation but East Asian neutral. */
	CHECK(st_unicode_columns(UINT32_C(0x1f1e6)) == 2);
	/* Copyright sign is text-default unless presentation is selected later. */
	CHECK(st_unicode_columns(UINT32_C(0x00a9)) == 1);
	CHECK(st_unicode_columns(UINT32_C(0x2fffd)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x3fffd)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x3fffe)) == 1);
	CHECK(st_unicode_columns(UINT32_C(0x10ffff)) == 1);
	CHECK(st_unicode_columns(UINT32_C(0xd800)) == 0);
	CHECK(st_unicode_columns(UINT32_C(0x110000)) == 0);
	CHECK(st_unicode_columns(UINT32_MAX) == 0);
	/* The width union and the independently stored property tables must
	 * agree throughout the complete scalar space, not just at sample glyphs.
	 */
	for (uint32_t cp = 0; cp <= UINT32_C(0x10ffff); ++cp) {
		enum st_unicode_east_asian_width eaw = st_unicode_east_asian_width(cp);
		bool emoji = st_unicode_emoji_presentation(cp);
		if (cp >= UINT32_C(0xd800) && cp <= UINT32_C(0xdfff)) {
			CHECK(eaw == ST_UNICODE_EAW_INVALID);
			CHECK(!emoji);
			CHECK(st_unicode_columns(cp) == 0);
		} else {
			CHECK(eaw != ST_UNICODE_EAW_INVALID);
			CHECK(st_unicode_columns(cp) == ((emoji
			    || eaw == ST_UNICODE_EAW_WIDE
			    || eaw == ST_UNICODE_EAW_FULLWIDTH) ? 2 : 1));
		}
	}
	return EXIT_SUCCESS;
}
