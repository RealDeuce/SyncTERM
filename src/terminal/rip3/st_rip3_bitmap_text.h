/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_TEXT_H_
#define ST_RIP3_BITMAP_TEXT_H_

#include "st_rip3_geometry.h"
#include "st_rip3_maf.h"
#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_ports;

struct st_rip3_bitmap_text_style {
	unsigned magnification; /* 1..10, actual integral bitmap enlargement. */
	bool vertical; /* Rotate counterclockwise: read bottom to top. */
	bool opaque; /* Terminal cells include their background; graphics need not. */
	bool shadow; /* One device pixel down/right, using background color. */
};

struct st_rip3_bitmap_text_metrics {
	uint32_t width, height;
	struct st_rip3_point advance;
};

int st_rip3_bitmap_text_measure(const struct st_rip3_maf_font *font,
	const struct st_rip3_bitmap_text_style *style, size_t length,
	struct st_rip3_bitmap_text_metrics *result);
/* Explicit CP437 byte span (including blank character zero), immutable MAF
 * view and device-pixel origin relative to the active viewport. Horizontal
 * origin is the top-left address, rounded once to nearest with half ties
 * positive. Finite wide positions fit +/-1024*INT32_MAX before rounding.
 * For vertical text the same address rotates
 * about origin: x'=origin.x+y, y'=origin.y-x. Shadow uses device-pixel offsets,
 * independent of rotation/magnification, and is outside measured text bounds.
 * Foreground wins over shadow/background; each pixel applies mode once for the
 * entire text primitive. Visible work is bounded by the viewport, even for a
 * long/offscreen string. Errors precede all drawing. Cursor, justification,
 * font selection, terminal control/UTF-8 transport and wire policy are callers. */
int st_rip3_draw_bitmap_text(struct st_rip3_ports *ports,
	const struct st_rip3_maf_font *font,
	const struct st_rip3_bitmap_text_style *style,
	const uint8_t *text, size_t length, const struct st_rip3_text_position *origin,
	uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode);

#endif
