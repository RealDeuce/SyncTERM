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

/*
 * Returns zero for a non-scalar value, otherwise one or two columns using
 * SyncTERM's Unicode East Asian Width plus Emoji_Presentation policy.
 */
uint8_t st_unicode_columns(uint32_t codepoint);

#endif
