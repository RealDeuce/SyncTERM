/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_CLASSIC_TEXT_H_
#define ST_RIP3_CLASSIC_TEXT_H_

#include "st_rip3_bitmap_text.h"
#include "st_rip3_stroke_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_ports;
struct st_rip3_outline_font;
struct st_rip3_outline_layout;

enum st_rip3_classic_text_flag {
	ST_RIP3_CLASSIC_LEFT = 0x01,
	ST_RIP3_CLASSIC_RIGHT = 0x02,
	ST_RIP3_CLASSIC_CENTER = 0x04,
	ST_RIP3_CLASSIC_TOP = 0x08,
	ST_RIP3_CLASSIC_BOTTOM = 0x10,
	ST_RIP3_CLASSIC_BASELINE = 0x20,
	ST_RIP3_CLASSIC_MIDDLE = 0x40,
	ST_RIP3_CLASSIC_SHADOW = 0x80,
};

struct st_rip3_classic_text_style {
	unsigned font;
	unsigned direction;
	unsigned magnification;
	unsigned flags;
};

struct st_rip3_classic_scale {
	unsigned numerator, denominator;
};

/* Magnifications outside 1..10 return a zero denominator. */
struct st_rip3_classic_scale st_rip3_classic_stroke_scale(unsigned magnification);

/* Immutable borrowed views. A custom binding may supply a validated MAF/CHR
 * view for the corresponding font number. The selected canvas determines
 * physical scaling; it does not select a world-coordinate frame. */
struct st_rip3_classic_font {
	enum st_rip3_canvas canvas;
	unsigned number;
	union {
		struct st_rip3_maf_font bitmap;
		struct st_rip3_chr stroke;
	} data;
	/* Optional client raster override; both zero retains canvas. */
	uint32_t device_width, device_height;
};

struct st_rip3_classic_text_plan {
	/* Device geometry before orientation: advance is the pen displacement,
	 * ascent/descent are baseline-to-cell reference distances, not ink bounds.
	 * Vector spans may be fractional and include endpoint scanlines. */
	double advance, ascent, descent;
	/* Whole-string metric, rationally scaled then truncated once. Raster
	 * advance above remains fractional; wire consumers own WORD narrowing. */
	uint64_t integer_advance;
	uint64_t cell_height; /* Stable for an empty run. */
	struct st_rip3_text_position origin;
	struct st_rip3_chr_text_matrix matrix;
};

/* Font 0..10, direction 0/1, magnification 1..10. Each alignment group
 * accepts at most one bit. For font zero record the two omitted defaults.
 * Preserve higher bits in the two-digit UltraNum range. */
int st_rip3_classic_text_style_set(struct st_rip3_classic_text_style *result,
	unsigned font, unsigned direction, unsigned magnification, unsigned flags);
/* Realize classic font zero for graphics. The active WORLD determines DPI;
 * the device canvas selects the inter-glyph spacing table. */
int st_rip3_classic_default_outline(struct st_rip3_outline_font *font,
	struct st_rip3_outline_layout *layout, const struct st_rip3_classic_text_style *style,
	const struct st_rip3_frame *frame);
/* Original bundled RIPtel resources only: bitmap font zero is that canvas's
 * native 80x43 MAF bank, not the frozen 1.54 font. BGI fonts use their original
 * CHR views. */
int st_rip3_classic_font_builtin(struct st_rip3_classic_font *result,
	enum st_rip3_canvas canvas, unsigned number);
/* Use an exact MAF bank when available; other client rasters use the 640x480
 * bitmap bank. Stroke scaling always uses the actual device dimensions. */
int st_rip3_classic_font_builtin_device(struct st_rip3_classic_font *result,
	uint32_t width, uint32_t height, unsigned number);
/* Explicit font codes; transport conversion/expansion is outside this kernel.
 * Vector magnifications use the published rational table, then scale the
 * 640x350 reference to the device. Bitmap cells magnify integrally; their
 * nominal baseline projects the published (7*mag-1) reference metric into the
 * native cell height. Justification precedes counterclockwise rotation of the
 * physical glyph geometry. Wide/fractional anchors are retained until final
 * origin rounding to nearest, half ties positive; finite placements fit
 * +/-1024*INT32_MAX. Logical advances remain fractional when required.
 * No world/cursor/style table is changed or inferred (#24). */
int st_rip3_classic_text_plan(const struct st_rip3_classic_font *font,
	const struct st_rip3_classic_text_style *style, const uint8_t *text, size_t length,
	const struct st_rip3_text_position *anchor, struct st_rip3_classic_text_plan *result);
/* Canvas must match the ports object. Transparent bitmap graphics text and
 * solid CHR strokes delegate to the prepared-coverage kernels; shadow remains
 * one device pixel down/right in both orientations. Every error is for the
 * owning parser to consume, never a terminal/session processing return. */
int st_rip3_draw_classic_text(struct st_rip3_ports *ports,
	const struct st_rip3_classic_font *font,
	const struct st_rip3_classic_text_style *style, const uint8_t *text, size_t length,
	const struct st_rip3_text_position *anchor, uint32_t foreground, uint32_t background,
	enum st_rip3_write_mode mode);

#endif
