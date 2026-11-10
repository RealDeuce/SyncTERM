/* Canonical IGS system fonts with decoded one-byte glyph masks. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_FONTS_H_
#define CTERM_IGS_FONTS_H_

#include <stdbool.h>
#include <stdint.h>

struct cterm_igs_font {
	int points, top, ascent, half, descent, bottom;
	int char_width, cell_width, left, right, thicken, underline;
	uint16_t lighten, skew;
	unsigned width, height;
	bool doubled;
	const uint8_t *pixels;
};

const struct cterm_igs_font *cterm_igs_font_for_points(int points);

#endif
