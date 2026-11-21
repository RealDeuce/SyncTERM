/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_STYLES_H_
#define ST_RIP3_STYLES_H_

#include "st_rip3_classic_text.h"
#include "st_rip3_fill.h"

#include <stdbool.h>
#include <stdint.h>

struct st_rip3_styles;
struct st_rip3_backup;

/* Resolved ordinary drawing attributes. Colours retain their raw index/RGB
 * values (up to 24 bits); the owning scene resolves its current colour mode.
 * Pen thickness and fill phase are explicit DEVICE values for the existing
 * kernels. Wire '=' converts/clamps and truncates thickness to a byte, then
 * normalizes the cosmetic zero byte to effective width one. Wire fills derive
 * context phase at paint time, independently of the device fill.origin value
 * retained here. Bootstrap orchestration remains separate from this service.
 *
 * This is the represented subset of STYLE. Font family is owned here;
 * outline parameters are owned values; realization is separate. Cursor/image
 * state belongs to PORT and mouse/color mode to ENV.
 * The border setting belongs here and follows record protection/backup rules. */
enum st_rip3_font_mode { ST_RIP3_FONT_CLASSIC, ST_RIP3_FONT_OUTLINE };

struct st_rip3_outline_style {
	unsigned compatibility_s, compatibility_f;
	uint32_t flags; /* All 24 wire bits are retained. */
	unsigned size, shadow, string_rotation, character_rotation, spacing;
	/* Nonnegative device magnitude, converted once when y requests world Y
	 * units. Flags retain the sign and conversion bits for later consumers. */
	uint32_t baseline;
	char name[40]; /* Owned, NUL terminated, at most 39 protocol bytes. */
};

struct st_rip3_style {
	uint32_t drawing_color, background_color, fill_color;
	struct st_rip3_pen pen;
	/* Original logical type 0..4, distinct from the resolved/custom mask.
	 * K maps this to SOLID/DOT/DASHDOT/DASH/DASHDOTDOT. */
	unsigned line_type;
	struct st_rip3_fill fill;
	struct st_rip3_classic_text_style font;
	enum st_rip3_write_mode write_mode;
	bool borders;
	enum st_rip3_font_mode font_mode;
	struct st_rip3_outline_style outline;
};

struct st_rip3_style_info {
	bool defined;
	bool protected;
};

/* One session thread owns 36 value records. Entry zero is initially current,
 * defined and unprotectable. Supply its attributes explicitly, using
 * style_defaults for normal protocol defaults or a deliberate prepared scene. */
/* Normal startup/reset/unused-entry defaults for the fields represented here. */
int st_rip3_style_defaults(struct st_rip3_style *result);
int st_rip3_styles_create(struct st_rip3_styles **result,
	const struct st_rip3_style *initial);
void st_rip3_styles_destroy(struct st_rip3_styles *styles);
int st_rip3_styles_current(const struct st_rip3_styles *styles, unsigned *result);
int st_rip3_styles_info(const struct st_rip3_styles *styles,
	unsigned number, struct st_rip3_style_info *result);
int st_rip3_styles_get(const struct st_rip3_styles *styles,
	unsigned number, struct st_rip3_style *result);
/* Validate the complete supplied value before creating/replacing an entry.
 * Definition does not select it. Protected entries reject edits. Set requires
 * an already-defined entry. Errors preserve the table, selection and outputs. */
int st_rip3_styles_define(struct st_rip3_styles *styles,
	unsigned number, const struct st_rip3_style *value);
int st_rip3_styles_set(struct st_rip3_styles *styles,
	unsigned number, const struct st_rip3_style *value);
/* First use defines an unprotected record with fixed normal defaults.
 * Existing entries retain their values/protection. Selection changes no
 * PORT/ENV/cursor/image state. Extended-font activation remains separate. */
int st_rip3_styles_switch(struct st_rip3_styles *styles, unsigned number);
int st_rip3_styles_protect(struct st_rip3_styles *styles, unsigned number, bool protect);
/* Outer SOFT defaults unprotected records, clears nonzero in-use flags and
 * selects zero. All protected configuration and independent backups survive. */
int st_rip3_styles_soft_reset(struct st_rip3_styles *styles);
/* Owned snapshots of every attribute stored here and table metadata.
 * Whole restore bypasses live protection and retains the immutable snapshot.
 * The shipping restore retains the LIVE current index, reselecting/defaulting
 * it after restoring table contents; the saved index does not become current.
 * This does not capture the unresolved parts of a complete RIP STYLE context,
 * implement SGS/RGS or perform scene reset/default initialization. */
int st_rip3_styles_backup_create(struct st_rip3_backup **result);

#endif
