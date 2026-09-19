/* SyncTERM's built-in legacy bitmap-font catalog. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_BUILTIN_FONTS_H_
#define ST_BUILTIN_FONTS_H_

#include "st_codepage.h"
#include "st_console.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST_BUILTIN_FONT_COUNT 46U

size_t st_builtin_font_count(void);
const char *st_builtin_font_name(size_t selector);
bool st_builtin_font_broken_vertical(size_t selector);
enum st_codepage st_builtin_font_codepage(size_t selector);

/*
 * Returns a borrowed font matching the exact cell dimensions. Bitmap and
 * mapping storage remain valid for the life of the process.
 */
bool st_builtin_font_get(size_t selector, uint32_t width, uint32_t height,
	struct st_console_font *font);

#endif
