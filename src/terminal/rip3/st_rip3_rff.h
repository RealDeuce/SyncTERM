/* RIPtel FastFont outline/style/kerning file views. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_RFF_H
#define ST_RIP3_RFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_rff {
	const uint8_t *data;
	size_t         size, style_offset, styles, base, advances, offsets, trailer;
	unsigned       first_character, characters, units_per_em;
	/* Signed y-down font-unit rectangle from the validated metrics block.
	 * Header copies repeat these edges; none is a typographic ascent/advance. */
	int     xmin, ymin, xmax, ymax;
	int     cap_alignment, ascent_alignment, descent_alignment; /* Signed y-down font units. */
	uint8_t bold_hollow_strength, hollow_strength;
};

struct st_rip3_rff_style {
	/* NUL-terminated inside its validated 34-byte field; padding is ignored. */
	const char *name;
	unsigned    flags, horizontal_percent;
	int         leading;
	bool        hollow;
};

struct st_rip3_rff_glyph {
	unsigned       advance;
	int            xmin, ymin, xmax, ymax;
	const uint8_t *path;
	size_t         path_size;
	/* Damaged records render blank, retaining their declared advance. */
	bool damaged;
};

enum st_rip3_rff_operation {
	ST_RIP3_RFF_MOVE,
	ST_RIP3_RFF_LINE,
	ST_RIP3_RFF_CURVE,
};

struct st_rip3_rff_segment {
	enum st_rip3_rff_operation operation;
	unsigned                   points;
	int                        x[3], y[3];
};

/* Views borrow immutable bytes for their complete lifetime. The decoder
 * recognizes RFF, not its differently framed FF1 predecessor. Results and
 * cursors are unchanged on failure. Glyph records are individually framed:
 * aliases and SYMBOL's bogus trailer offset must not truncate valid glyphs. */
int st_rip3_rff_open(struct st_rip3_rff *result, const void *data, size_t size);
int st_rip3_rff_style_at(struct st_rip3_rff_style *result, const struct st_rip3_rff *file, size_t index);
int st_rip3_rff_find_style(struct st_rip3_rff_style *result, const struct st_rip3_rff *file, const char *name);
/* Declared font-unit advance, independent of outline framing/ink. Missing
 * codes return NOT_FOUND; failures preserve result. The validated view borrows
 * immutable bytes as above. Damaged glyph paths do not invalidate metrics. */
int st_rip3_rff_advance(unsigned *result, const struct st_rip3_rff *file, uint8_t character);
int st_rip3_rff_glyph(struct st_rip3_rff_glyph *result, const struct st_rip3_rff *file, uint8_t character);
/* NOT_FOUND marks end of an outline; coordinates remain signed, y-down,
 * in units_per_em font units. Contours are implicitly closed by the renderer. */
int st_rip3_rff_path_next(struct st_rip3_rff_segment *result, const struct st_rip3_rff_glyph *glyph,
    size_t *cursor);
/* A missing/non-kerning trailer returns zero adjustment. A malformed
 * recognized kerning table returns INVALID_FORMAT, without changing result. */
int st_rip3_rff_kerning(int *result, const struct st_rip3_rff *file, uint8_t left, uint8_t right);

#endif
