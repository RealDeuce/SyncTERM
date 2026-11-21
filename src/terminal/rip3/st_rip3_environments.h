/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ENVIRONMENTS_H_
#define ST_RIP3_ENVIRONMENTS_H_

#include "st_rip3_geometry.h"

#include <stdbool.h>
#include <stdint.h>

struct st_rip3_environments;
struct st_rip3_backup;

/* Environment records own values/references, not the referenced objects.
 * Device dimensions belong to the client canvas and cannot vary per entry.
 * The shipping v3 limits supersede the inherited 2.x parameter tables:
 * stored world dimensions 0..65535, RGB precision zero (palette) or 1..8.
 * A zero stored dimension is queryable but has no usable projection frame. */
struct st_rip3_environment {
	unsigned graphics_style, button_style, port, text_window, palette;
	uint32_t world_width, world_height;
	unsigned base_math;
	unsigned coordinate_size;
	unsigned rgb_bits;
	unsigned mouse_pointer;
	uint64_t baud_rate;
};

struct st_rip3_environment_info {
	bool defined;
	bool protected;
};

/* One owner/session thread. Initially only entry zero is defined/current.
 * Basic values are 640x350, base 36, two coordinate bytes, palette mode,
 * mouse pointer zero, full-speed playback and referenced entries zero.
 * This does not apply the values to other tables or reset an active scene. */
int st_rip3_environment_defaults(struct st_rip3_environment *result);
int st_rip3_environments_create(struct st_rip3_environments **result,
	enum st_rip3_canvas canvas);
int st_rip3_environments_create_device(struct st_rip3_environments **result,
	uint32_t width, uint32_t height);
void st_rip3_environments_destroy(struct st_rip3_environments *environments);
int st_rip3_environments_current(const struct st_rip3_environments *environments,
	unsigned *result);
int st_rip3_environments_info(const struct st_rip3_environments *environments,
	unsigned number, struct st_rip3_environment_info *result);
int st_rip3_environments_get(const struct st_rip3_environments *environments,
	unsigned number, struct st_rip3_environment *result);
/* Explicit record creation/replacement; do not infer SWITCH_ENVIRONMENT's
 * undefined-entry policy or reset in-use semantics (#16). Protected entries
 * reject definition and edits. Set requires an already-defined entry. All
 * fields are validated before any change; failed calls preserve state/output. */
int st_rip3_environments_define(struct st_rip3_environments *environments,
	unsigned number, const struct st_rip3_environment *value);
int st_rip3_environments_set(struct st_rip3_environments *environments,
	unsigned number, const struct st_rip3_environment *value);
/* COLORMODE callback's narrow protection-bypassing edit of an already defined
 * slot. Zero selects palette mapping; 1..8 selects RGB component precision.
 * Selection and every other record field remain unchanged. Wire M uses set. */
int st_rip3_environments_color(struct st_rip3_environments *environments,
	unsigned number, unsigned bits);
/* Select an existing record only. The future scene owner must activate its
 * references in other tables as part of a complete context transaction.
 * CUR(ENV,n)/SWITCH_ENVIRONMENT runtime activation is not implemented here. */
int st_rip3_environments_select(struct st_rip3_environments *environments,
	unsigned number);
int st_rip3_environments_protect(struct st_rip3_environments *environments,
	unsigned number, bool protect);
/* Outer SOFT: default unprotected records, discard their nonzero definitions
 * and select zero. Departing protected current retains WORLD/pointer but gets
 * base 36, width 2 and RGB precision 0. Independent backups are untouched. */
int st_rip3_environments_soft_reset(struct st_rip3_environments *environments);
/* Resolve a usable projection frame. Zero stored WORLD dimensions reject
 * here before any caller can divide by them; the record itself stays valid. */
int st_rip3_environments_frame(const struct st_rip3_environments *environments,
	unsigned number, struct st_rip3_frame *result);
/* Physical canvas with its initial compatibility WORLD, for consumers such
 * as ordinary w that must not depend on any record's stored WORLD. */
int st_rip3_environments_device(const struct st_rip3_environments *environments,
	struct st_rip3_frame *result);

/* Whole record table/current index, including protection, in owned snapshots.
 * Restore bypasses live protection and requires the same device canvas.
 * Referenced table contents, pixels and external UI/playback state are not
 * captured or activated; that scene-level orchestration remains separate. */
int st_rip3_environments_backup_create(struct st_rip3_backup **result);

#endif
