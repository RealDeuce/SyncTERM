/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_STROKE_TEXT_H_
#define ST_RIP3_STROKE_TEXT_H_

#include "st_rip3_chr.h"
#include "st_rip3_geometry.h"
#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_ports;

struct st_rip3_chr_text_metrics {
	uint32_t advance; /* Sum of unscaled table advances, not ink bounds. */
	unsigned height; /* capital - descender; independent of string length. */
};

/* Map font coordinates (u = cumulative advance + glyph x, v = glyph y up)
 * to device pixels: x=origin.x+xx*u+xy*v, y=origin.y+yx*u+yy*v.
 * Normal one-unit strokes use {1,0,0,-1}. Origin is a device baseline point.
 * World/style scaling, justification and orientation choose the matrix outside
 * this geometry kernel. All generated endpoints must fit +/-1024*INT32_MAX;
 * fractional device baselines are retained through stroke transformation. */
struct st_rip3_chr_text_matrix {
	double xx, xy, yx, yy;
};

/* Button CHR strokes are first truncated at the captured size in WORLD,
 * then projected to DEVICE relative to the port/viewport origin. */
struct st_rip3_chr_label_projection {
	struct st_rip3_frame frame;
	int numerator, denominator;
	int64_t offset_x, offset_y;
};

int st_rip3_draw_chr_label(struct st_rip3_ports *ports,
	const struct st_rip3_chr *font, const uint8_t *text, size_t length,
	const struct st_rip3_text_position *world_baseline,
	const struct st_rip3_chr_label_projection *projection,
	bool shadow, uint32_t foreground, uint32_t background);

/* Explicit CP437 span, including zero. Missing glyphs have zero advance/ink.
 * Widths are accumulated before any scaling, avoiding per-character drift. */
int st_rip3_chr_text_measure(const struct st_rip3_chr *font,
	const uint8_t *text, size_t length, struct st_rip3_chr_text_metrics *result);
/* Solid one-device-pixel strokes, independent of line style. Whole-text union
 * coverage applies each write mode once, including repeated strokes/glyphs.
 * Shadow is one device pixel down/right, with foreground precedence. All input
 * validation/allocations precede painting. SCAN preserves the pen position
 * and contributes no ink. Inactive viewports have no painting work.
 * Font and text bytes stay immutable for the call. Caller inputs and protocol
 * cursor/style state are not changed. */
int st_rip3_draw_chr_text(struct st_rip3_ports *ports,
	const struct st_rip3_chr *font, const uint8_t *text, size_t length,
	const struct st_rip3_text_position *origin, const struct st_rip3_chr_text_matrix *matrix,
	bool shadow, uint32_t foreground, uint32_t background,
	enum st_rip3_write_mode mode);

#endif
