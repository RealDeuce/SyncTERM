/* Private native font objects and embedded disk-font files. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_AMIGA_FONT_INTERNAL_H_
#define ST_AMIGA_FONT_INTERNAL_H_
#include "st_amiga_font.h"
struct st_amiga_font {
	unsigned width, height, baseline;
	struct st_amiga_glyph glyphs[256];
	uint8_t *storage;
	bool builtin;
};
struct st_amiga_builtin {
	const char *name;
	unsigned height, sets;
	const uint8_t *bytes;
	size_t size;
	st_amiga_font_t *font;
};
extern struct st_amiga_builtin st_amiga_builtins[];
extern const size_t st_amiga_builtins_count;
#endif
