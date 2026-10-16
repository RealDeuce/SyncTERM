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

/* Cc and Cf ranges independently transcribed from pinned UnicodeData.
 * Mn/Me and default ignorables have separate generated property tables. */
static bool
control_or_format(uint32_t cp)
{
	static const uint32_t ranges[][2] = {
		{0, 0x1f}, {0x7f, 0x9f}, {0xad, 0xad}, {0x600, 0x605},
		{0x61c, 0x61c}, {0x6dd, 0x6dd}, {0x70f, 0x70f}, {0x890, 0x891},
		{0x8e2, 0x8e2}, {0x180e, 0x180e}, {0x200b, 0x200f},
		{0x202a, 0x202e}, {0x2060, 0x2064}, {0x2066, 0x206f},
		{0xfeff, 0xfeff}, {0xfff9, 0xfffb}, {0x110bd, 0x110bd},
		{0x110cd, 0x110cd}, {0x13430, 0x1343f}, {0x1bca0, 0x1bca3},
		{0x1d173, 0x1d17a}, {0xe0001, 0xe0001}, {0xe0020, 0xe007f},
	};
	for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i)
		if (cp >= ranges[i][0] && cp <= ranges[i][1])
			return true;
	return false;
}

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
	/* U+1F6D9 LIGHTHOUSE was added with Emoji 18.0. */
	CHECK(st_unicode_emoji_presentation(UINT32_C(0x1f6d9)));
	CHECK(!st_unicode_emoji_presentation(UINT32_C(0x00a9)));
	CHECK(st_unicode_columns('A') == 1);
	CHECK(st_unicode_columns(UINT32_C(0x00a1)) == 1);
	CHECK(st_unicode_columns(UINT32_C(0x1100)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x4e00)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x1f600)) == 2);
	CHECK(st_unicode_columns(UINT32_C(0x1f6d9)) == 2);
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
	static const uint32_t zero[] = {
		0, 7, 0x7f, 0x85, 0xad, 0x301, 0x34f, 0x600, 0x1160,
		0x200b, 0x200d, 0x2060, 0x20dd, 0xfe0f, 0xfeff, 0x1e4ec,
		0xe0001, 0xe0100,
	};
	for (size_t i = 0; i < sizeof(zero) / sizeof(zero[0]); ++i)
		CHECK(st_unicode_columns(zero[i]) == 0);
	CHECK(st_unicode_nonspacing_mark(0x301));
	CHECK(!st_unicode_default_ignorable(0x301));
	CHECK(st_unicode_default_ignorable(0x34f));
	CHECK(st_unicode_default_ignorable(0x2060));
	CHECK(!st_unicode_nonspacing_mark(0x2060));
	/* The width union and the independently stored property tables must
	 * agree throughout the complete scalar space, not just at sample glyphs.
	 */
	for (uint32_t cp = 0; cp <= UINT32_C(0x10ffff); ++cp) {
		enum st_unicode_east_asian_width eaw = st_unicode_east_asian_width(cp);
		bool emoji = st_unicode_emoji_presentation(cp);
		bool zero = st_unicode_default_ignorable(cp)
		    || st_unicode_nonspacing_mark(cp) || control_or_format(cp);
		if (cp >= UINT32_C(0xd800) && cp <= UINT32_C(0xdfff)) {
			CHECK(eaw == ST_UNICODE_EAW_INVALID);
			CHECK(!emoji);
			CHECK(st_unicode_columns(cp) == 0);
		} else {
			CHECK(eaw != ST_UNICODE_EAW_INVALID);
			CHECK(st_unicode_columns(cp) == (zero ? 0 : (emoji
			    || eaw == ST_UNICODE_EAW_WIDE
			    || eaw == ST_UNICODE_EAW_FULLWIDTH) ? 2 : 1));
		}
		if (st_unicode_default_ignorable(cp) || st_unicode_nonspacing_mark(cp))
			CHECK(st_unicode_columns(cp) == 0);
	}
	return EXIT_SUCCESS;
}
