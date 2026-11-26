/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OUTLINE_FONT_H_
#define ST_RIP3_OUTLINE_FONT_H_

#include "st_rip3_geometry.h"
#include "st_rip3_rff.h"

#include <stddef.h>
#include <stdint.h>

struct st_rip3_outline_bounds {
	int64_t xmin, ymin, xmax, ymax;
};

struct st_rip3_outline_font {
	struct st_rip3_rff file;
	struct st_rip3_rff_style style;
	size_t face;
	unsigned points, orientation;
	uint32_t dpi_x, dpi_y;
	uint32_t flags;
	/* Device-axis units per font unit, before the face's horizontal percent.
	 * Layout supplies string/character rotation and spacing independently. */
	double scale_x, scale_y;
	/* Geometric DEVICE edges, y-down, after face percentage and orientation.
	 * Round each transformed edge to nearest, half ties positive. Spans use
	 * max-min, with no inclusive-pixel increment. These are not ink bounds or
	 * typographic ascent/descent lines; exact native hinting is not reproduced.
	 * Wide edges retain legal custom-font/frame realizations before clipping. */
	struct st_rip3_outline_bounds bounds;
	/* Zero for solid faces; nominal device-space stamp diameter for hollow.
	 * The glyph's horizontal percentage never multiplies this width. */
	uint32_t hollow_thickness;
};

struct st_rip3_outline_column_metrics {
	int64_t baseline, fit, advance; /* Device pixels, upright text. */
};

/* Column fit is inclusive and uses the cap alignment and descent. The
 * advance uses the separate ascender alignment, descent and face leading.
 * This consumes a successfully bound font. */
struct st_rip3_outline_column_metrics st_rip3_outline_font_column_metrics(
	const struct st_rip3_outline_font *);

/* Pure font realization services, not parser/session entry points. Bind an
 * already-loaded RFF face, revalidating its immutable bytes. The caller owns
 * those bytes for the complete lifetime of the returned view. Point size is
 * 1..4095; flags retain all 24 wire bits. Engine orientation 0..3 selects X
 * DPI for 0/2 and Y for 1/3 when choosing the hollow stamp. The connection's
 * device and current world dimensions determine DPI, truncated before use.
 * Failures preserve result, permitting a session to retain its prior cache. */
int st_rip3_outline_font_bind(struct st_rip3_outline_font *result,
	const struct st_rip3_rff *file, size_t face, const struct st_rip3_frame *frame,
	unsigned points, uint32_t flags, unsigned orientation);
/* Resolve an exact, ASCII-case-insensitive catalogue style name, including
 * its suffix/spaces. No filename aliases, trimming or fallback font. Bundled
 * views live for the application lifetime. No files/cache are created. */
int st_rip3_outline_font_builtin(struct st_rip3_outline_font *result,
	const char *name, const struct st_rip3_frame *frame,
	unsigned points, uint32_t flags, unsigned orientation);
/* Horizontal span of this realized, oriented bbox for spacing flag 0x400.
 * A zero span remains zero; narrow layout bounds reject spans above int32_t.
 * Failure leaves result unchanged. No advance-width substitution. */
int st_rip3_outline_font_bbox_width(int32_t *result,
	const struct st_rip3_outline_font *font);
/* Independent, unkerned DEVICE advances for raw unsigned font bytes. Scale
 * the declared advance by points*20*face-percent/(units_per_em*100), rounding
 * to the nearest 1/1440-inch unit with positive half ties upward, then truncate
 * each byte's logical advance*DPI/1440 separately. String rotation is exactly
 * 0/900/1800/2700: use X DPI for 0/1800, Y for 900/2700. Character orientation
 * does not rotate this scalar; layout separately chooses string direction.
 * The adopted policy uses wide intermediates, without native WORD narrowing.
 * No spacing, pair kerning, alignment or cached cursor-width policy is applied.
 * Missing codes have zero advance; damaged glyphs keep the declared advance.
 * Synthetic effects do not change the metric. Result is one int32_t per byte;
 * its storage must not overlap the inputs. NULL text/result is valid for zero
 * length. Validation and all fallible metric work precede any output change.
 * Parser/session entry points must absorb failures at their nonfatal boundary. */
int st_rip3_outline_font_advances(int32_t *result,
	const struct st_rip3_outline_font *font, unsigned string_rotation,
	const uint8_t *text, size_t length);

#endif
