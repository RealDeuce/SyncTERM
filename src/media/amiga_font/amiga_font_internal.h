/* Private native bitmap representation shared with generated constants. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_AMIGA_FONT_INTERNAL_H_
#define ST_AMIGA_FONT_INTERNAL_H_
#include "st_amiga_font.h"
struct st_amiga_font {
	unsigned width, height, baseline;
	struct st_amiga_glyph glyphs[256];
	uint8_t *storage;
};
struct st_amiga_builtin {
	const char *name;
	const struct st_amiga_font *font;
};
extern const struct st_amiga_builtin st_amiga_builtins[];
extern const size_t st_amiga_builtins_count;
#endif
