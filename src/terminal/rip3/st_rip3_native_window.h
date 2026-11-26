/* RIP3 services for preparing and saving the native text interpreter. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_NATIVE_WINDOW_H_
#define ST_RIP3_NATIVE_WINDOW_H_

#include "st_cterm.h"

/* RIP3-only preparation of a started, idle fresh window: inherit unfinished
 * ordinary ANSI syntax and, when preserve_tabs is true, the old tab stops.
 * Extended b keeps fresh default tabs. Do not copy rendition/geometry or
 * UTF-8 presenter state. Ordinary definitions (preserve_tabs) also retain
 * RIP3 charset designations; extended definitions keep fresh US/US defaults.
 * No rendering/input callbacks. Failure preserves both
 * objects; success leaves source independently owned. Pending optional media
 * and font-download payloads remain unsupported, never partially transferred. */
int st_cterm_inherit_window_interpreter(st_cterm_t *destination, const st_cterm_t *source, bool preserve_tabs);
/* RIP3 window selection retains the destination's window state and resumes
 * connection-owned ordinary ANSI syntax in the destination. Wire first use copies represented
 * ANSI/window state and independently owned fonts/tabs from the source.
 * Copy targets must be fresh, same-sized private consoles; discard on error. */
int st_cterm_select_window_interpreter(st_cterm_t *destination, const st_cterm_t *source);
int st_cterm_copy_window_state(st_cterm_t *destination, const st_cterm_t *source);
/* Explicit RIP3 definition saves own metadata/font resources, never cells,
 * pixels, history or connection parser state. Capture survives source teardown.
 * Restore into a fresh same-sized unpublished target and discard on error.
 * The caller supplies the current connection interpreter independently. */
struct st_cterm_window_definition;

/* ANSI saves own metadata and logical font selectors only. Restore prepares
 * a fresh unpublished target, resolving fonts from the current compatible
 * resource owner. It neither restores encoding nor owns saved font resources.
 * The snapshot's CSI saved position is derived from the captured live cursor. */
int st_cterm_window_ansi_create(struct st_cterm_window_definition **result, const st_cterm_t *source);
int st_cterm_window_ansi_restore(st_cterm_t *target, const struct st_cterm_window_definition *definition,
    const st_cterm_t *resources);
int st_cterm_window_definition_create(struct st_cterm_window_definition **result, const st_cterm_t *source);
int st_cterm_window_definition_restore(st_cterm_t *target, const struct st_cterm_window_definition *definition);
/* Prepare a differently sized RIP3 view with full margins and a
 * homed live cursor. Retain the saved cursor and the rest of the definition. */
int  st_cterm_window_definition_restore_maximized(st_cterm_t *target,
    const struct st_cterm_window_definition                 *definition);
void st_cterm_window_definition_destroy(struct st_cterm_window_definition *definition);
/* CUR first use: fresh per-window defaults and empty explicit tabs, retaining
 * only connection-owned ordinary ANSI syntax. Target must be fresh and idle. */
int st_cterm_default_window_interpreter(st_cterm_t *destination, const st_cterm_t *source);
/* Outer RIP3 SOFT's preliminary reset of the old current TW, even protected:
 * normal ANSI 7/0, full vertical margins, saved home and default tabs. Home
 * the live cursor only if active. Retain parser, visibility and other modes;
 * do not clear pixels, history, font resources or stop audio. */
int  st_cterm_rip3_ansi_reset(st_cterm_t *terminal, bool active);
bool st_cterm_rip3_keyboard_locked(const st_cterm_t *terminal);

#endif
