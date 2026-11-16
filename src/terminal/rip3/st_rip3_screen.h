/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_SCREEN_H_
#define ST_RIP3_SCREEN_H_

#include "st_rip3_surface.h"

#include <stdbool.h>

struct st_rip3_ports;
struct st_rip3_backup;

/* Borrowed session-owned bindings for the consistent 7.2 screen-save data:
 * full screen index plane, active display palette and status-bar visibility.
 * The palette is an explicit display value, not a palette-table slot. The
 * owner applies restored display/UI values; no hardware or UI calls occur
 * here. All bindings are required and used only during an operation. Saved
 * snapshots own their data and survive these bindings and their owners.
 *
 * This deliberately does not infer viewport, style/button-table or palette
 * slot activation/ownership rules pending #26. SAVE/RESTORE wire dispatch and
 * complete context backups remain unsupported until those are resolved. */
struct st_rip3_screen {
	struct st_rip3_ports *ports;
	struct st_rip3_palette *palette;
	bool *status_bar;
};

/* Typed backup callbacks accept st_rip3_screen bindings. A restore prepares a
 * same-canvas pixel clone before committing pixels/palette/status together.
 * Port definitions, viewport state, protection, current port and offscreen
 * pixels are untouched. Destroy using backup_destroy. Failures preserve live
 * state and the save, and are consumed by the owning parser, never returned
 * through a terminal/session processing entry. */
int st_rip3_screen_backup_create(struct st_rip3_backup **result);

#endif
