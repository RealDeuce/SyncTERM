/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TABLE_HOST_H_
#define ST_RIP3_TABLE_HOST_H_

#include "st_rip3_builtin_reference.h"

#include <stddef.h>

struct st_rip3_backup;

enum st_rip3_table_type {
	ST_RIP3_TABLE_TW, ST_RIP3_TABLE_BUT, ST_RIP3_TABLE_STYLE,
	ST_RIP3_TABLE_PAL, ST_RIP3_TABLE_PORT, ST_RIP3_TABLE_MOUSE,
	ST_RIP3_TABLE_ENV, ST_RIP3_TABLE_SCREEN, ST_RIP3_TABLE_COUNT,
};

struct st_rip3_table_binding {
	void *live;
	struct st_rip3_backup *backup;
};

/* Borrowed session bindings: each live object must match its backup area's
 * typed callbacks. Unbound classes remain unsupported. The owner must keep
 * both alive and invoke this dispatcher on their single owner thread. */
struct st_rip3_table_host {
	struct st_rip3_table_binding tables[ST_RIP3_TABLE_COUNT];
	void *reset_context;
	int (*soft_reset)(void *context);
	/* Valid action callbacks report lower-level failure without a variable error
	 * or returned value; malformed syntax still returns private preflight status. */
	void *operation_context;
	void (*operation_failed)(void *context,int status);
};

/* The RIP parser's internal dispatcher handles BACKSTAT, whole-table COPY,
 * SCP/RCP, SBS/RBS, SMF/RMF, STW/RTW, CUR, INUSE, individual
 * ISPROT, PROT/UNPROT and eight-bit PALENTRY readback. Entry selection/protection
 * supports PAL/PORT/STYLE/BUT/TW selection and PAL/PORT/ENV/STYLE/BUT/TW metadata; backup-slot operations
 * work for any bound object class. INUSE(BACKUP)
 * and aggregate ISPROT undefined cases stay unsupported pending #16.
 * Palette live-entry COPY validates all destinations before mutation and
 * copies source protection except to zero. RESET(PAL,...) handles current or
 * specific entries, TBL and the base/numbered/stack/aggregate backup selectors.
 * It validates the full selector list and required bindings first. Palette
 * resets preserve selection even when it becomes undefined; ISPROT(PAL,CUR)
 * reads current protection without the numeric-slot definition check.
 * Parameter defaults and decimal slot syntax follow the reference. Remaining
 * live-entry COPY and RESET families return NOT_SUPPORTED for wider dispatch.
 * CUR(STYLE,n) selects/defaults the represented style and its font family,
 * independently of PORT/ENV/text-cursor state. CUR(ENV,n), SENV, RENV, SGS and
 * RGS remain unsupported until
 * complete scene context capture/activation is available. Generic ENV record snapshots do not
 * activate referenced table/UI state. STYLE snapshots capture only the resolved
 * drawing attributes currently stored in st_rip3_styles, not the unresolved
 * complete context under #24.
 * A TW live binding borrows st_rip3_text_window_operations; its native owner
 * handles selection/protection and complete definition-only STW/RTW snapshots.
 * Whole-TW COPY/status/protection and backup-only RESET selectors are bound;
 * individual TW-entry COPY and live TW reset remain separate.
 * Parsed references must come from builtin_reference_parse. Empty outputs
 * require capacity >= 1, validated before any active operation. Validation and
 * binding failures leave caller output and bound table/save state unchanged.
 * Individual runtime reset failures may follow completed earlier selectors;
 * their effects are not rolled back.
 * BUT has its own typed whole-table snapshots, SBS/RBS and RESET selector
 * lifecycle, including the documented current-reset metadata overwrite. SOFT
 * uses the ordinary table reset and then selects zero, not that CUR path.
 * Undefined-current explicit writes and protected-but-undefined first selection
 * remain unsupported under documentation issue #112.
 * MOUSE snapshots bind fields/buttons, persistent group maps/selection and shared hover handlers.
 * RESET(MOUSE) defaults to TBL; explicit backup selectors preserve protected
 * slots, and field-only resets retain handlers. Group restoration reconciles
 * current publications with fresh owners; failed saves preserve prior backups.
 * SCREEN has SAVE/RESTORE, all numbered aliases, COPY/status and RESET
 * selectors. Valid screen actions return no value/variable error after manager
 * failure; a lower-level callback may diagnose it. Numeric RESTORE requests
 * slot deletion even after failure, while POP remains success-dependent.
 * Screen snapshots capture pixels/current RGB only; selection/metadata and
 * status visibility remain live. SAVEALL/RESTOREALL are still separate work.
 * No host transmission, prompting, stream parsing or app/session routing occurs here. Execution
 * statuses are private to parser-owned recovery; use the public host-variable
 * process entry to consume malformed references without a session error. */

#endif
