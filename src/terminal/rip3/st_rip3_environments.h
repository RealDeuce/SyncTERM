/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ENVIRONMENTS_H_
#define ST_RIP3_ENVIRONMENTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st_rip3_geometry.h"

struct st_rip3_environments;
struct st_rip3_backup;

enum st_rip3_charset {
	ST_RIP3_CHARSET_UNSET, /* Inherited ENV override or disabled shared fallback. */
	ST_RIP3_CHARSET_USASCII,
	ST_RIP3_CHARSET_KANJI,
};

/* Environment records own values/references, not the referenced objects.
 * Device dimensions belong to the client canvas and cannot vary per entry.
 * The shipping v3 limits supersede the inherited 2.x parameter tables:
 * stored world dimensions 0..65535, RGB precision zero (palette) or 1..8.
 * A zero stored dimension is queryable but has no usable projection frame. */
struct st_rip3_environment {
	unsigned             graphics_style, button_style, port, text_window, palette;
	uint32_t             world_width, world_height;
	unsigned             base_math;
	unsigned             coordinate_size;
	unsigned             rgb_bits;
	unsigned             mouse_pointer;
	enum st_rip3_charset charset;
};

struct st_rip3_environment_info {
	bool defined;
	bool protected;
};

/* One owner/session thread. Initially only entry zero is defined/current.
 * Basic values are 640x350, base 36, two coordinate bytes, palette mode,
 * mouse pointer zero and referenced entries zero. Playback starts at full speed.
 * This does not apply the values to other tables or reset an active scene. */
int  st_rip3_environment_defaults(struct st_rip3_environment *result);
int  st_rip3_environments_create(struct st_rip3_environments **result, enum st_rip3_canvas canvas);
int  st_rip3_environments_create_device(struct st_rip3_environments **result, uint32_t width, uint32_t height);
void st_rip3_environments_destroy(struct st_rip3_environments *environments);
int  st_rip3_environments_current(const struct st_rip3_environments *environments, unsigned *result);
int  st_rip3_environments_info(const struct st_rip3_environments *environments, unsigned number,
    struct st_rip3_environment_info *result);
/* Explicit numbered lookup requires a defined entry. */
int st_rip3_environments_get(const struct st_rip3_environments *environments, unsigned number,
    struct st_rip3_environment *result);
/* A numbered reset can turn off the selected entry's definition. Its
 * stored defaults are still active. The output index is optional. */
int st_rip3_environments_active(const struct st_rip3_environments *environments, unsigned *number,
    struct st_rip3_environment *result);
/* Protected entries reject definition and edits. Set accepts a defined
 * or active entry and leaves its definition unchanged. */
int st_rip3_environments_define(struct st_rip3_environments *environments, unsigned number,
    const struct st_rip3_environment *value);
int st_rip3_environments_set(struct st_rip3_environments *environments, unsigned number,
    const struct st_rip3_environment *value);
/* COLORMODE callback's narrow protection-bypassing edit of a defined or active
 * slot. Zero selects palette mapping; 1..8 selects RGB component precision.
 * Selection and every other record field remain unchanged. Wire M uses set. */
int st_rip3_environments_color(struct st_rip3_environments *environments, unsigned number, unsigned bits);
/* COMPAT edits the drawing format even on protected entries. Only the
 * default/CUR form can address a selected but undefined entry. */
int st_rip3_environments_compat(struct st_rip3_environments *environments, unsigned number, bool current_form);
enum st_rip3_charset st_rip3_environments_shared_charset(const struct st_rip3_environments *);
void                 st_rip3_environments_set_shared_charset(struct st_rip3_environments *, enum st_rip3_charset);
/* DCS name setters bypass protection. OFF clears only a protected override. */
void st_rip3_environments_set_charset(struct st_rip3_environments *, unsigned number, enum st_rip3_charset);
/* RESET clears defined unprotected overrides. The table form ignores number. */
void st_rip3_environments_reset_charset(struct st_rip3_environments *, unsigned number, bool table);
/* MCURSOR's protection-bypassing edit of the current ENV. A successful
 * choice cancels a pending scene return to arrow. */
int st_rip3_environments_mouse(struct st_rip3_environments *environments, unsigned pointer);
/* Scene lock selects wait and requests an arrow on full release. Release
 * applies that request only while the mouse scene gate is still set. */
void st_rip3_environments_scene_lock(struct st_rip3_environments *environments);
void st_rip3_environments_scene_release(struct st_rip3_environments *environments, bool mouse_locked);
/* Low-level selection of an existing record without protocol side effects.
 * Used when constructing a prepared state. Wire/CUR selectors use activate. */
int st_rip3_environments_select(struct st_rip3_environments *environments, unsigned number);
/* Protocol selection defaults unused targets, then writes target base/RGB/
 * coordinate width into the departing record, bypassing its protection, before
 * changing current. WORLD and pointer stay owned by their respective records.
 * No other table selection changes. Consumers read active values from current;
 * the existing connection pointer refresh observes its new mouse number. */
int st_rip3_environments_activate(struct st_rip3_environments *, unsigned number);
/* Interpreter-wide local scene rate, saved alongside the ENV table. */
uint32_t st_rip3_environments_playback_rate(const struct st_rip3_environments *);
int      st_rip3_environments_set_playback_rate(struct st_rip3_environments *, uint32_t rate);
/* Validate the defined source and all destinations before copying. Protected
 * sources are readable; protected/self/invalid targets reject the whole call.
 * Copy definition/protection, clearing protection at zero. Keep selection and
 * device dimensions; repeated destinations are legal. No allocation occurs. */
int st_rip3_environments_copy_entries(struct st_rip3_environments *, unsigned source, const unsigned *destinations,
    size_t count);
int st_rip3_environments_protect(struct st_rip3_environments *environments, unsigned number, bool protect);
/* Apply the already validated environment segment in flag order. */
void st_rip3_environments_header(struct st_rip3_environments *, unsigned flags, unsigned slot,
    unsigned coordinate_width, unsigned rgb_bits);
/* CUR marks the selected entry defined; a numbered reset leaves only zero
 * defined. The selected record takes effect as soon as it is reset. */
int st_rip3_environments_reset_entry(struct st_rip3_environments *, unsigned number, bool current_form);
/* Outer SOFT: default unprotected records, discard their nonzero definitions
 * and select zero. Departing protected current retains WORLD/pointer but gets
 * base 36, width 2 and RGB precision 0. Independent backups are untouched. */
int st_rip3_environments_soft_reset(struct st_rip3_environments *environments);
/* Resolve a usable projection frame. Zero stored WORLD dimensions reject
 * here before any caller can divide by them; the record itself stays valid. */
int st_rip3_environments_frame(const struct st_rip3_environments *environments, unsigned number,
    struct st_rip3_frame *result);
/* Physical canvas with its initial compatibility WORLD, for consumers such
 * as ordinary w that must not depend on any record's stored WORLD. */
int st_rip3_environments_device(const struct st_rip3_environments *environments, struct st_rip3_frame *result);

/* Whole record table, current index and live playback rate in owned snapshots.
 * Restore bypasses live protection and requires the same device canvas.
 * Current settings are read from the restored ENV by their normal consumers.
 * SENV/RENV use this snapshot family. The scene reader owns its pacing clock. */
int st_rip3_environments_backup_create(struct st_rip3_backup **result);

#endif
