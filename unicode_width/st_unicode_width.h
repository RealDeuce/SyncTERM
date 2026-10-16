/* Locale-independent Unicode scalar widths for fixed-cell display. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UNICODE_WIDTH_H_
#define ST_UNICODE_WIDTH_H_

#include <stdint.h>
#include <stdbool.h>

enum st_unicode_east_asian_width {
	ST_UNICODE_EAW_NEUTRAL,
	ST_UNICODE_EAW_AMBIGUOUS,
	ST_UNICODE_EAW_HALFWIDTH,
	ST_UNICODE_EAW_NARROW,
	ST_UNICODE_EAW_WIDE,
	ST_UNICODE_EAW_FULLWIDTH,
	ST_UNICODE_EAW_INVALID,
};

enum st_unicode_east_asian_width st_unicode_east_asian_width(
	uint32_t codepoint);
bool st_unicode_emoji_presentation(uint32_t codepoint);
/* UCD properties; false for non-scalars. Nonspacing marks are Mn or Me. */
bool st_unicode_default_ignorable(uint32_t codepoint);
bool st_unicode_nonspacing_mark(uint32_t codepoint);

/*
 * Returns zero for non-scalars, default ignorables, Mn, Me, Cf and Cc.
 * Otherwise returns one or two using East Asian Width plus Emoji_Presentation.
 * This scalar advance is not the presentation width of an isolated mark.
 */
uint8_t st_unicode_columns(uint32_t codepoint);

#endif
