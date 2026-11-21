/* Separate RIP3 button-style records; no legacy RIP dependencies. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BUTTON_STYLES_H_
#define ST_RIP3_BUTTON_STYLES_H_

#include <stdbool.h>
#include <stdint.h>

struct st_rip3_button_styles;
struct st_rip3_backup;
/* One session owner thread owns calls and live state; backups own independent
 * value snapshots and do not borrow the live table's storage. */

enum st_rip3_button_color {
	ST_RIP3_BUTTON_LABEL_FOREGROUND, ST_RIP3_BUTTON_LABEL_BACKGROUND,
	ST_RIP3_BUTTON_BRIGHT, ST_RIP3_BUTTON_DARK, ST_RIP3_BUTTON_SURFACE,
	ST_RIP3_BUTTON_UNDERLINE, ST_RIP3_BUTTON_CORNER, ST_RIP3_BUTTON_COLOR_COUNT,
};

struct st_rip3_button_style {
	/* Definition-time projected device values, stored as protocol WORDs.
	 * Creation takes the low byte of bevel. Colors are always palette indices. */
	uint16_t width,height,flags,bevel;
	unsigned orientation,group;
	uint8_t flags2;
	uint8_t colors[ST_RIP3_BUTTON_COLOR_COUNT];
};
struct st_rip3_button_style_info {bool defined,protected;};

int st_rip3_button_style_defaults(struct st_rip3_button_style *result);
int st_rip3_button_styles_create(struct st_rip3_button_styles **result);
void st_rip3_button_styles_destroy(struct st_rip3_button_styles *styles);
int st_rip3_button_styles_current(const struct st_rip3_button_styles *, unsigned *result);
/* Creation reads stored current fields even after a reset clears in-use;
 * unlike selection, this does not default or define that record. */
int st_rip3_button_styles_current_value(const struct st_rip3_button_styles *, struct st_rip3_button_style *result);
/* Successful renderer auto-capture changes only current flags, even when
 * protected/undefined. Use persistent field flags (RECESS is removed here),
 * retain record metadata and return the new field flags for later rendering.
 * The caller invokes this only after clipboard capture succeeds. */
int st_rip3_button_styles_autoclip(struct st_rip3_button_styles *,
	uint16_t field_flags, uint16_t *result);
int st_rip3_button_styles_info(const struct st_rip3_button_styles *, unsigned number,
	struct st_rip3_button_style_info *result);
int st_rip3_button_styles_get(const struct st_rip3_button_styles *, unsigned number,
	struct st_rip3_button_style *result);
/* Explicit edits check raw protection, then define the record on success.
 * An undefined current record does not require implicit first-use selection. */
int st_rip3_button_styles_set(struct st_rip3_button_styles *, unsigned number,
	const struct st_rip3_button_style *value);
int st_rip3_button_styles_select(struct st_rip3_button_styles *, unsigned number);
int st_rip3_button_styles_protect(struct st_rip3_button_styles *, unsigned number, bool protect);
/* Specific/TBL resets preserve selection and protected records. Outer SOFT
 * selects zero separately. Independent backup objects are never cleared. */
int st_rip3_button_styles_reset_entry(struct st_rip3_button_styles *, unsigned number);
int st_rip3_button_styles_reset_table(struct st_rip3_button_styles *);
/* Safely models the documented shipped current-reset metadata overwrite;
 * records/protection are unchanged, selection becomes zero. */
int st_rip3_button_styles_reset_current(struct st_rip3_button_styles *);
/* Restores all records, metadata and the saved current index, bypassing live
 * protection. This is distinct from STYLE's documented current-index defect. */
int st_rip3_button_styles_backup_create(struct st_rip3_backup **result);

#endif
