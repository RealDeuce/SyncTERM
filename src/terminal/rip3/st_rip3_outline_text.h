/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OUTLINE_TEXT_H_
#define ST_RIP3_OUTLINE_TEXT_H_

#include "st_rip3_geometry.h"
#include "st_rip3_rff.h"
#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_ports;

enum st_rip3_outline_fill_rule {
	ST_RIP3_OUTLINE_NONZERO,
	ST_RIP3_OUTLINE_EVEN_ODD,
};

struct st_rip3_rff_text_style {
	size_t face; /* Index of the selected RFF style record. */
	/* Font coordinates are y-down. Map u=(advance+x)*face-percent/100,
	 * v=glyph-y to device x=origin.x+xx*u+xy*v, y=origin.y+yx*u+yy*v.
	 * Normal em-sized text uses {em/units_per_em,0,0,em/units_per_em}.
	 * World scaling, orientation and justification are caller policy. */
	double xx, xy, yx, yy;
	bool kerning;
	bool shadow; /* One device pixel down/right, foreground takes precedence. */
	enum st_rip3_outline_fill_rule fill_rule;
	/* Positive device-pixel width for hollow faces, independent of scaling.
	 * Ignored for solid faces. Outline-font realization supplies the wire
	 * point/DPI-dependent nominal width; this kernel takes it explicitly. */
	uint32_t hollow_thickness;
};

/* Raw font-unit advance, before face-percent/matrix scaling. Kerning applies
 * only to adjacent present characters, not across missing glyphs. Damaged
 * glyphs retain their declared advance and have no ink. Explicit font codes,
 * including zero; missing characters have zero advance. Every intermediate
 * advance must fit int32_t. Result is unchanged on failure. */
int st_rip3_rff_text_measure(const struct st_rip3_rff *font,
	const uint8_t *text, size_t length, bool kerning, int32_t *result);

/* Explicit whole-text union coverage: contours combine inside each glyph by
 * fill_rule, then glyph ink is united. Hollow faces stroke implicitly closed
 * contours at hollow_thickness. Cubics flatten adaptively to 1/64 device pixel;
 * emitted/control coordinates must fit +/-4*INT32_MAX. Hints are not applied.
 * All input validation, geometry, coverage and shadow allocation precede any
 * painting. Each covered pixel receives its write mode once. The font, text,
 * matrix, port cursor and graphics state remain unchanged.
 *
 * This geometry kernel does not select wire point sizes, text placement,
 * expansion, encoding or session-error policy. Its explicit whole-text union
 * is a coverage choice; the adopted protocol requires graphics-text W while
 * treating original per-glyph fixed-copy observations as historical behavior.
 * Parser processing must absorb any helper failure at its nonfatal boundary.
 */
int st_rip3_draw_rff_text(struct st_rip3_ports *ports,
	const struct st_rip3_rff *font, const uint8_t *text, size_t length,
	const struct st_rip3_point *origin, const struct st_rip3_rff_text_style *style,
	uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode);

#endif
