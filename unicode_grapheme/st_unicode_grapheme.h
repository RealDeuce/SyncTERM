/* Unicode grapheme and RGI emoji property oracles. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UNICODE_GRAPHEME_H_
#define ST_UNICODE_GRAPHEME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_unicode_grapheme_break {
	ST_UNICODE_GRAPHEME_OTHER,
	ST_UNICODE_GRAPHEME_CR,
	ST_UNICODE_GRAPHEME_LF,
	ST_UNICODE_GRAPHEME_CONTROL,
	ST_UNICODE_GRAPHEME_EXTEND,
	ST_UNICODE_GRAPHEME_ZWJ,
	ST_UNICODE_GRAPHEME_REGIONAL_INDICATOR,
	ST_UNICODE_GRAPHEME_PREPEND,
	ST_UNICODE_GRAPHEME_SPACING_MARK,
	ST_UNICODE_GRAPHEME_L,
	ST_UNICODE_GRAPHEME_V,
	ST_UNICODE_GRAPHEME_T,
	ST_UNICODE_GRAPHEME_LV,
	ST_UNICODE_GRAPHEME_LVT,
};

enum st_unicode_indic_conjunct_break {
	ST_UNICODE_INCB_NONE,
	ST_UNICODE_INCB_CONSONANT,
	ST_UNICODE_INCB_EXTEND,
	ST_UNICODE_INCB_LINKER,
};

enum st_unicode_grapheme_break st_unicode_grapheme_break(uint32_t codepoint);
enum st_unicode_indic_conjunct_break st_unicode_indic_conjunct_break(
	uint32_t codepoint);
bool st_unicode_extended_pictographic(uint32_t codepoint);

/* Returns the longest RGI emoji sequence at the start of input. */
size_t st_unicode_rgi_emoji_prefix(const uint32_t *codepoints, size_t count);

#endif
