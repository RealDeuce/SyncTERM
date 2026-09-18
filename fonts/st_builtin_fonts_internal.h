/* Private representation of the built-in bitmap-font catalog. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_BUILTIN_FONTS_INTERNAL_H_
#define ST_BUILTIN_FONTS_INTERNAL_H_

#include "st_builtin_fonts.h"

enum st_builtin_font_bitmap_size {
	ST_BUILTIN_FONT_8X16,
	ST_BUILTIN_FONT_8X14,
	ST_BUILTIN_FONT_8X8,
	ST_BUILTIN_FONT_12X20,
	ST_BUILTIN_FONT_BITMAP_SIZE_COUNT,
};

struct st_builtin_font_bitmap {
	const uint8_t *data;
	size_t size;
};

struct st_builtin_font_record {
	const char *name;
	struct st_builtin_font_bitmap bitmap[ST_BUILTIN_FONT_BITMAP_SIZE_COUNT];
	const struct st_console_font_mapping *mapping;
	size_t mapping_count;
	bool broken_vertical;
};

extern const struct st_builtin_font_record
	st_builtin_font_records[ST_BUILTIN_FONT_COUNT];

#endif
