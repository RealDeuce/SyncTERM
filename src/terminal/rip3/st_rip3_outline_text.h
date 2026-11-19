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
struct st_rip3_outline_font;
struct st_rip3_outline_layout;

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
	/* One device-space displaced copy; {0,0} disables it. The displacement
	 * is independent of the glyph matrix. Foreground takes precedence. */
	struct st_rip3_point shadow;
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

/* Rasterize already-positioned bytes using the same coverage/atomicity policy.
 * positions supplies one viewport-relative device origin per byte; it is not
 * transformed by the glyph matrix or horizontal face percentage. The matrix
 * maps only glyph-local coordinates, permitting independent string direction,
 * character rotation, spacing and fractional placement. Raw font advances and
 * style.kerning are ignored: the caller has already performed layout. Missing
 * and damaged codes retain their positions but contribute no ink. All supplied
 * origins and transformed vertices must be finite and fit +/-4*INT32_MAX.
 * Empty text permits NULL text/positions. No cursor or font state changes. */
int st_rip3_draw_rff_positioned(struct st_rip3_ports *ports,
	const struct st_rip3_rff *font, const uint8_t *text, size_t length,
	const struct st_rip3_text_position *positions, const struct st_rip3_rff_text_style *style,
	uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode);

/* Prepared-font rendering for an already laid-out byte string. Derive the
 * glyph matrix from the realized point/DPI scales and independent character
 * rotation: rotate in font coordinates, then apply device-axis X/Y scales.
 * The kernel applies the selected horizontal face percentage exactly once.
 * Device origins, layout baseline/spacing and shadow displacement are not
 * transformed again. The font must be realized for layout.font_orientation.
 * Nonzero interior fill and realized device-space hollow width are selected
 * here. Plain solid/hollow and bold hollow faces are supported. Until their
 * renderers exist, solid BOLD, ITALIC and decorations return NOT_SUPPORTED
 * before allocation/painting rather than omit the selected effect. Parser
 * entry points must absorb all service failures nonfatally. Other validation,
 * atomicity and coverage contracts are those of draw_rff_positioned above. */
int st_rip3_draw_outline_positioned(struct st_rip3_ports *ports,
	const struct st_rip3_outline_font *font, const struct st_rip3_outline_layout *layout,
	const uint8_t *text, size_t length, const struct st_rip3_text_position *positions,
	uint8_t foreground, uint8_t background, enum st_rip3_write_mode mode);

#endif
