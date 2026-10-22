/* Add the console's drawable missing-text glyphs to its fallback font. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_PRESENTATION_FONT_H_
#define ST_PRESENTATION_FONT_H_

#include "st_console.h"

struct st_presentation_font {
	struct st_console_font font;
};

int st_presentation_font_create(struct st_presentation_font *result,
	const struct st_console_font *base);
/* Unicode fallbacks prefer bundled emoji over base Unicode artwork. Both
 * constructors retain original byte-glyph indexes and add tofu. */
int st_presentation_font_create_fallback(struct st_presentation_font *result,
	const struct st_console_font *base);
void st_presentation_font_destroy(struct st_presentation_font *font);

#endif
