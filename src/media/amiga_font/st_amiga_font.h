/* Native Amiga bitmap fonts; no fixed-cell conversion or borrowed file data. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_AMIGA_FONT_H_
#define ST_AMIGA_FONT_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_amiga_font st_amiga_font_t;
struct st_amiga_glyph {
	const uint8_t *bits;
	size_t stride;
	unsigned width, height;
	int bearing, advance, top;
	unsigned bit_offset;
};
struct st_amiga_strike {
	unsigned height;
	char *path;
	unsigned style;
};
struct st_amiga_family {
	char *name;
	struct st_amiga_strike *strikes;
	size_t count;
};
/* Exact case-insensitive Amiga name/size; NULL if absent. Static lifetime. */
const st_amiga_font_t *st_amiga_font_builtin(const char *name, unsigned height);
size_t st_amiga_font_builtin_count(void);
const char *st_amiga_font_builtin_name(size_t index, unsigned *height);
unsigned st_amiga_font_width(const st_amiga_font_t *font);
unsigned st_amiga_font_height(const st_amiga_font_t *font);
unsigned st_amiga_font_baseline(const st_amiga_font_t *font);
const struct st_amiga_glyph *st_amiga_font_glyph(const st_amiga_font_t *font, uint8_t ch);
/* Decode a monochrome disk-font HUNK image; output unchanged on failure. */
int st_amiga_font_decode(st_amiga_font_t **result, const void *bytes, size_t size);
void st_amiga_font_release(const st_amiga_font_t *font);
/* Descriptor paths are confined relative names. Bitmap loading is separate. */
int st_amiga_family_decode(struct st_amiga_family *result, const char *name, const void *bytes,
						   size_t size);
void st_amiga_family_dispose(struct st_amiga_family *family);
int st_amiga_font_read(st_amiga_font_t **result, const char *path);
#endif
