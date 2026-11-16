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
 * kernels. No wire thickness/phase defaults are inferred (#11/#12).
 *
 * This is the implemented subset of a STYLE record, not a complete protocol
 * context. Extended-font selection (#3), bootstrap values and ownership of
 * cursor/image/mouse/colour-mode state (#24) remain outside this component. */
struct st_rip3_style {
	uint32_t drawing_color, background_color, fill_color;
	struct st_rip3_pen pen;
	struct st_rip3_fill fill;
	struct st_rip3_classic_text_style font;
	enum st_rip3_write_mode write_mode;
};

struct st_rip3_style_info {
	bool defined;
	bool protected;
};

/* One session thread owns 36 value records. Entry zero is initially current,
 * defined and unprotectable. Its attributes must be supplied explicitly:
 * this service does not invent the unresolved protocol bootstrap defaults. */
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
/* First-use SWITCH_STYLE copies current attribute values to an unprotected
 * new record. Existing entries retain their values/protection. This is only
 * record selection: it does not activate scene/ENV/port/UI state or implement
 * CUR(STYLE,n)'s distinct, unresolved default-initialization policy. */
int st_rip3_styles_switch(struct st_rip3_styles *styles, unsigned number);
int st_rip3_styles_protect(struct st_rip3_styles *styles, unsigned number, bool protect);
/* Owned snapshots of every attribute stored here, current index and metadata.
 * Whole restore bypasses live protection and retains the immutable snapshot.
 * This does not capture the unresolved parts of a complete RIP STYLE context,
 * implement SGS/RGS or perform scene reset/default initialization. */
int st_rip3_styles_backup_create(struct st_rip3_backup **result);

#endif
