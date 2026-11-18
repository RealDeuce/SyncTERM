/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_HOST_VARIABLES_H_
#define ST_RIP3_HOST_VARIABLES_H_

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct st_rip3_features;
struct st_rip3_frame;
struct st_rip3_table_host;
struct st_rip3_input;

/* Client identity values, not protocol-version/capability advertisements.
 * All three strings must be supplied; their literal bytes are emitted unchanged.
 * Storage remains owned by the client for the duration of processing. */
struct st_rip3_terminal_identity {
	const char *name;
	const char *vendor;
	const char *version;
};

/* Borrowed current-session services; one session thread owns invocation and
 * state. A frame must have been initialized by frame_init. Missing services
 * are explicitly unsupported, never assumed to exist. No lifetime ownership
 * or terminal/application integration is implied by assembling these bindings. */
struct st_rip3_host_variables {
	const struct st_rip3_features *features;
	const struct st_rip3_terminal_identity *identity;
	/* For port geometry queries the bound current ENV's live world frame takes
	 * precedence. This snapshot is a fallback when no ENV service is bound;
	 * its device canvas must match the queried PORT table. */
	const struct st_rip3_frame *frame;
	const struct st_rip3_table_host *tables;
	/* One converted local-time snapshot for an expansion batch. The client
	 * owns clock conversion/time-zone selection; no global TZ or locale is
	 * changed by the parser. Years 1..9999, ordinary seconds 0..59, and
	 * consistent tm calendar/weekday/year-day fields are required. */
	const struct tm *local_time;
	/* Borrowed word/phrase; NULL or empty means the zone is unknown. */
	const char *time_zone;
	/* Current graphics-style background, already resolved to a palette index. */
	uint8_t background;
	/* Active D references set this gate. The enclosing evaluator must yield
	 * before executing another reference/host action while its wait is nonzero. */
	struct st_rip3_input *input;
};

enum st_rip3_host_diagnostic {
	ST_RIP3_HOST_INVALID_REFERENCE,
	ST_RIP3_HOST_UNSUPPORTED_REFERENCE,
	ST_RIP3_HOST_UNKNOWN_REFERENCE,
	ST_RIP3_HOST_OPERATION_FAILED,
	ST_RIP3_HOST_RESOURCE_FAILURE,
};

struct st_rip3_host_sink {
	void *context;
	/* Bytes are borrowed only during the callback. Empty expansions emit
	 * nothing. These callbacks cannot return a session error. */
	void (*reply)(void *context, const uint8_t *bytes, size_t length);
	/* Optional nonfatal observation; no raw variable/credential text is sent.
	 * Diagnostics do not latch a failed parser state or terminate a session. */
	void (*diagnostic)(void *context, enum st_rip3_host_diagnostic diagnostic);
};

/* Process one complete, already expanded ordinary $...$ reference. This is
 * the parser-owned recovery boundary, not a raw stream framer. It consumes a
 * rejected reference internally, optionally reports a nonfatal diagnostic,
 * and remains ready for the next reference. It never returns a parse or
 * command error to the terminal/session caller. Decoder/executor statuses are
 * private and must not be used as connection errors.
 *
 * Supports NULL, IFS, TERMINFO, RESX/RESY, D, the resolved date/time fields and the
 * implemented table/viewport/environment-variable families, plus PORTW/PORTH
 * and PORTX0/Y0/X1/Y1 with the shared decimal-port, PORT/VIEW, WORLD/DEVICE
 * grammar (only trailing fields optional). DOMAIN in the second position is
 * invalid. Replies use the documented intended numeric semantics, an explicit
 * correction to RIPtel 3.1's inverted-success callbacks; lower-right coordinates
 * are exclusive, and wide counts/ends are retained through world conversion.
 * TEXTXY reads the PORT-owned cursor as an unpadded signed-WORD X:Y pair.
 * Its optional decimal port/CUR precedes optional WORLD/DEVICE. WORLD returns
 * stored coordinates; DEVICE converts them again with the current frame,
 * truncating toward zero. Explicit undefined/inactive ports return -1; bare
 * omission reads the current cursor even with a deactivated viewport.
 * CUR(STYLE,n) uses fixed defaults for unused styles, permits selecting
 * protected records, and changes no PORT/ENV/cursor/encoding state.
 * Environment records support BASEMATH, COORDSIZE, WORLD/WORLDW/WORLDH, BAUDEMUL, COMPAT
 * and defined COLORMODE queries/sets; IMGSTYLE queries stored boundaries in
 * the current WORLD without undoing the far increment. Its decimal setter
 * replaces flags from ASPECT/DELETE/NOCLEAR/USEPAL keywords (none means zero),
 * restores prior port selection, and skips protected targets without text/error.
 * Undefined/inactive query/set targets reply -1 without mutation/error. Port
 * record backup/protection is separate from future scene-wide activation.
 * Undefined COLORMODE replies
 * remain blocked (#23). DATETIME formatting remains
 * blocked.
 * D requires one decimal duration 1..65535 in 60ths of a second (the detailed
 * rule; the milliseconds summary conflicts under #46). It emits no text and
 * starts the bound input gate. General same-host-command continuation/yield
 * integration remains with the enclosing evaluator, not this one-reference API.
 * Unknown names/user resolution and other built-ins remain pending; they emit
 * no reply and cannot abort later references. External launch names likewise
 * have no action. Framing, macro expansion and user-policy orchestration still
 * belong to the future enclosing RIP parser. The sink may be NULL; callbacks
 * must not reenter or change the bound state during processing. */
void st_rip3_host_variables_process(const struct st_rip3_host_variables *host,
	const struct st_rip3_host_sink *sink, const uint8_t *text, size_t length);

#endif
