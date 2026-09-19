/* SyncTERM's built-in legacy bitmap-font catalog. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_builtin_fonts.h"

#include "st_builtin_fonts_internal.h"

static const enum st_codepage codepages[ST_BUILTIN_FONT_COUNT] = {
	ST_CODEPAGE_CP437,
	ST_CODEPAGE_CP1251,
	ST_CODEPAGE_KOI8_R,
	ST_CODEPAGE_ISO_8859_2,
	ST_CODEPAGE_ISO_8859_4,
	ST_CODEPAGE_CP866M,
	ST_CODEPAGE_ISO_8859_9,
	ST_CODEPAGE_HAIK8,
	ST_CODEPAGE_ISO_8859_8,
	ST_CODEPAGE_KOI8_U,
	ST_CODEPAGE_ISO_8859_15,
	ST_CODEPAGE_ISO_8859_4,
	ST_CODEPAGE_KOI8_R,
	ST_CODEPAGE_ISO_8859_4,
	ST_CODEPAGE_ISO_8859_5,
	ST_CODEPAGE_ARMSCII8,
	ST_CODEPAGE_ISO_8859_15,
	ST_CODEPAGE_CP850,
	ST_CODEPAGE_CP850,
	ST_CODEPAGE_CP865,
	ST_CODEPAGE_CP1251,
	ST_CODEPAGE_ISO_8859_7,
	ST_CODEPAGE_KOI8_R,
	ST_CODEPAGE_ISO_8859_4,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_CP866M2,
	ST_CODEPAGE_CP437,
	ST_CODEPAGE_CP866M2,
	ST_CODEPAGE_CP865,
	ST_CODEPAGE_CP866U,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_CP1131,
	ST_CODEPAGE_PETSCII_UPPER,
	ST_CODEPAGE_PETSCII_LOWER,
	ST_CODEPAGE_PETSCII_UPPER,
	ST_CODEPAGE_PETSCII_LOWER,
	ST_CODEPAGE_ATASCII,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_ISO_8859_1,
	ST_CODEPAGE_PRESTEL,
	ST_CODEPAGE_ATARI_ST,
	ST_CODEPAGE_CP437,
};

size_t
st_builtin_font_count(void)
{
	return ST_BUILTIN_FONT_COUNT;
}

const char *
st_builtin_font_name(size_t selector)
{
	if (selector >= ST_BUILTIN_FONT_COUNT)
		return NULL;
	return st_builtin_font_records[selector].name;
}

bool
st_builtin_font_broken_vertical(size_t selector)
{
	return selector < ST_BUILTIN_FONT_COUNT
	    && st_builtin_font_records[selector].broken_vertical;
}

enum st_codepage
st_builtin_font_codepage(size_t selector)
{
	return selector < ST_BUILTIN_FONT_COUNT
	    ? codepages[selector] : ST_CODEPAGE_COUNT;
}

bool
st_builtin_font_get(size_t selector, uint32_t width, uint32_t height,
	struct st_console_font *font)
{
	enum st_builtin_font_bitmap_size size;
	const struct st_builtin_font_record *record;
	const struct st_builtin_font_bitmap *bitmap;
	size_t row_stride;

	if (font == NULL || selector >= ST_BUILTIN_FONT_COUNT)
		return false;
	if (width == 8 && height == 16)
		size = ST_BUILTIN_FONT_8X16;
	else if (width == 8 && height == 14)
		size = ST_BUILTIN_FONT_8X14;
	else if (width == 8 && height == 8)
		size = ST_BUILTIN_FONT_8X8;
	else if (width == 12 && height == 20)
		size = ST_BUILTIN_FONT_12X20;
	else
		return false;
	record = &st_builtin_font_records[selector];
	bitmap = &record->bitmap[size];
	if (bitmap->data == NULL)
		return false;
	row_stride = ((size_t)width + 7) / 8;
	*font = (struct st_console_font){
		.bitmap = bitmap->data,
		.bitmap_size = bitmap->size,
		.width = width,
		.height = height,
		.glyph_count = 256,
		.row_stride = row_stride,
		.glyph_stride = row_stride * height,
		.mapping = record->mapping,
		.mapping_count = record->mapping_count,
		.fallback_glyph = '?',
		.wide_fallback_glyph = ST_CONSOLE_FONT_NO_WIDE_FALLBACK,
	};
	return true;
}
