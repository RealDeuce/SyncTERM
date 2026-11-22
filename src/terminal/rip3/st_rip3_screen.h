/* Physical screen snapshots under the pinned screen-backup contract. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_SCREEN_H_
#define ST_RIP3_SCREEN_H_
#include "st_rip3_surface.h"
struct st_rip3_ports;
struct st_rip3_palettes;
struct st_rip3_backup;
struct st_pixel_buffer;
/* One session thread owns these borrowed bindings. Snapshots own raw screen
 * pixels and current-palette RGB only: no status/UI, current index, table,
 * viewport, text-window or mouse metadata. Optional paired native callbacks
 * merge ANSI ink into the prepared raw graphics capture (retaining its size
 * and ownership; adding an alternate plane is permitted) and present both
 * native planes on restore. They run only for explicit save/restore, never
 * ordinary drawing. Source callbacks must not reenter parsing/providers.
 * Capture failure preserves result/previous saves. Restore effects already
 * applied before a presentation failure are not rolled back. */
struct st_rip3_screen {
	struct st_rip3_ports *ports;
	struct st_rip3_palettes *palettes;
	void *context;
	int (*capture)(void *,struct st_pixel_buffer *);
	int (*restore)(void *,const struct st_pixel_buffer *,const struct st_rip3_palette *);
};
int st_rip3_screen_backup_create(struct st_rip3_backup **result);
int st_rip3_screen_clear(struct st_rip3_screen *);
#endif
