/* Locale-independent legacy codepage conversion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CODEPAGE_H_
#define ST_CODEPAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* These values are serialized and exposed to Wren. */
enum st_codepage {
	ST_CODEPAGE_CP437 = 0,
	ST_CODEPAGE_CP1251 = 1,
	ST_CODEPAGE_CP1251_B = 2,
	ST_CODEPAGE_KOI8_R = 3,
	ST_CODEPAGE_ISO_8859_2 = 4,
	ST_CODEPAGE_ISO_8859_4 = 5,
	ST_CODEPAGE_CP866M = 6,
	ST_CODEPAGE_ISO_8859_9 = 7,
	ST_CODEPAGE_ISO_8859_8 = 8,
	ST_CODEPAGE_KOI8_U = 9,
	ST_CODEPAGE_ISO_8859_15 = 10,
	ST_CODEPAGE_ISO_8859_5 = 11,
	ST_CODEPAGE_CP850 = 12,
	ST_CODEPAGE_CP850_B = 13,
	ST_CODEPAGE_CP865 = 14,
	ST_CODEPAGE_CP865_B = 15,
	ST_CODEPAGE_ISO_8859_7 = 16,
	ST_CODEPAGE_ISO_8859_1 = 17,
	ST_CODEPAGE_CP866M2 = 18,
	ST_CODEPAGE_CP866U = 19,
	ST_CODEPAGE_CP1131 = 20,
	ST_CODEPAGE_ARMSCII8 = 21,
	ST_CODEPAGE_HAIK8 = 22,
	ST_CODEPAGE_ATASCII = 23,
	ST_CODEPAGE_PETSCII_UPPER = 24,
	ST_CODEPAGE_PETSCII_LOWER = 25,
	ST_CODEPAGE_PRESTEL = 26,
	ST_CODEPAGE_PRESTEL_SEPARATED = 27,
	ST_CODEPAGE_ATARI_ST = 28,
	ST_CODEPAGE_COUNT = 29,
};

size_t st_codepage_count(void);
/* Returns a borrowed canonical name, or NULL for an invalid codepage. */
const char *st_codepage_name(enum st_codepage codepage);
bool st_codepage_broken_vertical(enum st_codepage codepage);

/*
 * Convert one encoded byte to Unicode. When graphic_controls is true,
 * display glyphs are selected for control bytes where the codepage defines
 * them; a broken-bar font's 0x7C glyph is also U+00A6 in that mode. With
 * graphic_controls false, 0x7C has its ASCII meaning U+007C. An invalid
 * codepage returns U+FFFD.
 */
uint32_t st_codepage_to_unicode(enum st_codepage codepage, uint8_t value,
	bool graphic_controls);

/* Convert one Unicode scalar to an encoded byte. Aliases resolve to the first
 * mapping in the legacy table. Result is unchanged if the codepoint is invalid
 * or has no mapping in the selected codepage. */
bool st_codepage_from_unicode(uint8_t *result, enum st_codepage codepage,
	uint32_t codepoint);

#endif
