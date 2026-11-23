/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_HOST_VARIABLES_H_
#define ST_RIP3_HOST_VARIABLES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct st_rip3_features;
struct st_rip3_frame;
struct st_rip3_table_host;
struct st_rip3_input;
struct st_rip3_user_variables;
struct st_rip3_template_host;
struct st_rip3_local_queries;
struct st_rip3_text_window_operations;
struct st_rip3_mouse;
struct st_rip3_field_context;
struct st_rip3_refresh;
struct st_rip3_builtin_reference;
struct st_rip3_tones;

enum {
	ST_RIP3_HOST_DIRECTORY_LIMIT = 12,
	ST_RIP3_HOST_DIRECTORY_CAPACITY = 4 * ST_RIP3_HOST_DIRECTORY_LIMIT + 1,
};

/* Borrowed connection owner. current returns the selected uppercase spelling
 * (empty when unset). select receives a validated name that lives only for the
 * call. An empty name clears selection. create still applies to the root. */
struct st_rip3_host_directory {
	void *context;
	const char *(*current)(void *context);
	int (*select)(void *context, const char *name, bool create);
	int (*file_exists)(void *context, const char *name, bool *present);
	/* FILEDEL attempts each parsed filename without reporting a result. */
	void (*remove_file)(void *context, const char *name);
};

/* Client identity values, not protocol-version/capability advertisements.
 * All three strings must be supplied; their literal bytes are emitted unchanged.
 * Storage remains owned by the client for the duration of processing. */
struct st_rip3_terminal_identity {
	const char *name;
	const char *vendor;
	const char *version;
};

/* One session thread owns this explicit generator state. Initialize state to
 * the client's chosen seed before binding it; the protocol promises no initial
 * seed or cross-session sequence. No process-global CRT random state is used. */
struct st_rip3_random {
	uint32_t state;
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
	/* Borrowed live pixel limit, shared with bitmap allocation. */
	const uint64_t *offscreen_pixel_budget;
	/* Borrowed local-time snapshot for a caller with a fixed clock sample.
	 * Live sessions supply local_clock instead. Each valid clock reference
	 * reads it once, including references reached after a delay. */
	const struct tm *local_time;
	void *clock_context;
	bool (*local_clock)(void *context, struct tm *local);
	/* Explicit zone override, with an empty string for an unknown zone.
	 * When NULL, a live client uses the zone associated with local_clock. */
	const char *time_zone;
	/* Fallback background, already resolved to a palette index. A bound live
	 * STYLE takes precedence, using live ENV/PAL for direct-color resolution. */
	uint8_t background;
	/* Active D references set this gate. The enclosing evaluator must yield
	 * before executing another reference/host action while its wait is nonzero. */
	struct st_rip3_input *input;
	/* The audio owner copies valid requests before returning. No service
	 * means successful tone actions are silent. */
	void *tones_context;
	void (*tones)(void *context, const struct st_rip3_tones *);
	/* RAND advances only this state, after validation and reply preparation. */
	struct st_rip3_random *random;
	/* Optional borrowed user-variable processor for the enclosing immediate
	 * query evaluator, not the single-built-in API below. Keep this binding
	 * stable and reserve the processor exclusively while that query is pending.
	 * Its stores, format/dialog adapter and diagnostics belong to the client. */
	struct st_rip3_user_variables *user_variables;
	/* Optional borrowed template slots and required-selection callback. Stable
	 * while a query is pending; definitions become active in command order. */
	const struct st_rip3_template_host *templates;
	/* Optional local modifier/captured-mouse/device-capacity/query-cell services.
	 * Missing values are unsupported, never invented from palette size or a
	 * default keyboard/mouse state. The client owns their platform mechanism. */
	const struct st_rip3_local_queries *local;
	/* Session setting, independent of screen saves and renderer availability. */
	bool *status_bar;
	void *status_bar_context;
	void (*status_bar_changed)(void *context, bool enabled);
	const struct st_rip3_host_directory *directory;
	void *transfer_context;
	int (*transfer)(void *context, bool upload, unsigned protocol, unsigned type,
		const char *filenames);
	/* The session normalized one wire codepage byte per UTF-8 scalar. */
	bool codepage_converted;
	/* The text-window owner supplies activation with its protection/undefined
	 * policy. Parameter lists are validated completely before any callback. */
	const struct st_rip3_text_window_operations *text_windows;
	struct st_rip3_mouse *mouse;
	/* Callback-scoped numeric snapshot. Logical field programs retain their
	 * identity across interpreter yields; unrelated work never inherits it. */
	const struct st_rip3_field_context *field_context;
	struct st_rip3_refresh *refresh;
	void *overflow_context;
	int (*overflow)(void *context, const struct st_rip3_builtin_reference *,
	    char *result, size_t capacity);
};

enum st_rip3_host_diagnostic {
	ST_RIP3_HOST_INVALID_REFERENCE,
	ST_RIP3_HOST_UNSUPPORTED_REFERENCE,
	ST_RIP3_HOST_UNKNOWN_REFERENCE,
	ST_RIP3_HOST_OPERATION_FAILED,
	ST_RIP3_HOST_RESOURCE_FAILURE,
	ST_RIP3_HOST_ABORTED,
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
 * Supports NULL, IFS, TERMINFO, RESX/RESY, D, RAND, MKILL, scoped FIELDID,
 * SHIFT/CTRL, MSTAT, COLORS,
 * captured XY, ATW/DTW through the bound text-window owner,
 * the date/time fields and the
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
 * Environment records support BASEMATH, COORDSIZE, WORLD/WORLDW/WORLDH, COMPAT
 * and defined COLORMODE queries/sets. BAUDEMUL uses the live local playback
 * rate shared by all ENV slots. IMGSTYLE queries stored boundaries in
 * the current WORLD without undoing the far increment. Its decimal setter
 * replaces flags from ASPECT/DELETE/NOCLEAR/USEPAL keywords (none means zero),
 * restores prior port selection, and skips protected targets without text/error.
 * Undefined/inactive query/set targets reply -1 without mutation/error. Port
 * record backup/protection is separate from future scene-wide activation.
 * COLORMODE queries undefined slots as -1 and edits defined precision 0..8,
 * bypassing ENV protection. Its wire counterpart accepts only eight-bit RGB.
 * D requires one decimal duration 1..65535 in 60ths of a second (the detailed
 * rule; the milliseconds summary conflicts under #46). It emits no text and
 * starts the bound input gate. General same-host-command continuation/yield
 * integration remains with the enclosing evaluator, not this one-reference API.
 * Local queries use explicit borrowed client services. SHIFT/CTRL read current
 * modifier state; MSTAT/COLORS report client mouse/device capacity rather than
 * guessing from the palette. XY formats captured DEVICE/WORLD signed-WORD pairs
 * as decimal with minimum width four, using current ENV before fallback frame.
 * In a text-window query, omission instead computes/clamps supplied DEVICE-grid
 * cells with minimum width two; explicit domains bypass that grid. Missing local
 * services diagnose unsupported. No input injection or global device probe.
 * Unknown names/user resolution and other built-ins remain pending; they emit
 * no reply and cannot abort later references. External launch names likewise
 * have no action. Framing, macro expansion and user-policy orchestration still
 * belong to the future enclosing RIP parser. The sink may be NULL; callbacks
 * must not reenter or change the bound state during processing. */
void st_rip3_host_variables_process(const struct st_rip3_host_variables *host,
	const struct st_rip3_host_sink *sink, const uint8_t *text, size_t length);

#endif
