/* Owned separate-engine session lifecycle. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_SESSION_H_
#define ST_RIP3_SESSION_H_

#include "st_rip3_commands.h"
#include "st_rip3_geometry.h"
#include "st_rip3_text_commands.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_session;
struct st_rip3_host_variables;
struct st_rip3_classic_font;
struct st_rip3_rff;
struct st_rip3_variable_reference;
struct st_rip3_variable_value;
struct st_pixel_buffer;
struct st_rip3_text_window_layout;
struct st_rip3_text_cursor_position;

struct st_rip3_session_source {
	void *context;
	uint64_t (*now_ns)(void *context);
	/* Nonblocking: false means no byte available. The client owns its queue,
	 * connection/EOF policy and bytes not yet taken by this callback. */
	bool (*next)(void *context, uint8_t *byte);
};

struct st_rip3_session_sink {
	void *context;
	void (*text)(void *context, const uint8_t *bytes, size_t length);
	void (*reply)(void *context, const uint8_t *bytes, size_t length);
	bool (*at_line_start)(void *context);
	void (*diagnostic)(void *context, enum st_rip3_command_diagnostic kind);
	void (*selection_required)(void *context, unsigned group);
	/* Commit pending ordinary text before changing graphics/window state. */
	void (*before_command)(void *context);
	/* Resource/renderer adapter. Prepare before committing a new view; the
	 * parser consumes failures nonfatally and retains its prior layout/gate. */
	int (*text_window)(void *context, const struct st_rip3_text_window_layout *layout, bool identical);
	/* Native text-window cursor movement, without synthesized received bytes. */
	int (*text_cursor)(void *context, const struct st_rip3_text_cursor_position *position);
	int (*text_cursor_visible)(void *context, bool visible);
};

struct st_rip3_session_config {
	enum st_rip3_canvas canvas;
	/* Both zero selects canvas. Otherwise both supply the entry's physical
	 * raster, which must never be replaced by a parser preset. */
	uint32_t device_width, device_height;
	enum st_rip3_text_encoding encoding;
	struct st_rip3_session_source source;
	struct st_rip3_session_sink sink;
	/* Optional copied external service bindings. The frame, tables, input,
	 * templates and text_windows must be NULL: this session owns those.
	 * Identity, features, time, local
	 * queries and an optional random/user processor remain borrowed. A supplied
	 * user processor is reserved exclusively while a session query is pending.
	 * Otherwise private session/persistent memory stores and a processor are
	 * created; their lifetime ends here, without implicit disk persistence. */
	const struct st_rip3_host_variables *services;
	uint32_t random_seed; /* Used only when no external generator is supplied. */
	const struct st_rip3_classic_font *classic_font;
	const struct st_rip3_rff *outline_font;
	const struct st_rip3_resource_paths *resources;
	const uint64_t *offscreen_pixel_budget;
	const struct st_rip3_bitmap_error_sink *bitmap_errors;
	/* Bind the completed in-scene error renderer to this owner's PORT/PAL
	 * tables when no external bitmap error sink is supplied. */
	bool builtin_bitmap_errors;
	/* Optional exclusive player, owned by the client. Cancel/destroy stops it;
	 * the client destroys it after this session and before its mixer engine. */
	struct st_rip3_audio *audio;
};

/* One terminal/session thread owns every call and callback. Config/source/sink
 * structs are copied; referenced objects and callback contexts must outlive the
 * session. Callbacks must not reenter, cancel/destroy or change its bindings.
 * Creation prepares all represented tables/backups, font cache, variables,
 * templates, stream, query and input gate before publishing result. Failure
 * preserves result and borrowed services and invokes no client callback.
 * This does not select an application terminal mode or implement unbound
 * text-window/button/mouse tables, full reset, graphical-text preprocessing or
 * scene headers. Immediate host queries do include their angle preprocessor. */
int st_rip3_session_create(struct st_rip3_session **result,
	const struct st_rip3_session_config *config);
void st_rip3_session_destroy(struct st_rip3_session *session);
/* Take at most budget source bytes. Resume an owned host continuation before
 * taking more bytes, even when no new input is available. Pauses for delays or
 * user prompts; no sleeping or threads. Syntax/resource/operation failures are
 * absorbed internally and never returned to a terminal/session failure path. */
void st_rip3_session_process(struct st_rip3_session *session, size_t budget);
/* Text callbacks still receive ordinary bytes for ANSI/RIP control framing.
 * The client must suppress ANSI display/side effects while this gate is off;
 * RIP identification/control handling remains independent of TTY visibility. */
bool st_rip3_session_text_active(const struct st_rip3_session *session);
/* Presentation services. Revision advances after commands/host continuations;
 * it does not promise that pixels changed. Damage is borrowed through unbind
 * or destruction, one byte per canvas pixel; see surface_damage. */
uint64_t st_rip3_session_revision(const struct st_rip3_session *session);
int st_rip3_session_damage(struct st_rip3_session *session,
	uint8_t *damage, size_t length);
uint64_t st_rip3_session_wait_ns(const struct st_rip3_session *session);
/* Local host work remains, independently of source availability. If ready
 * and not waiting for a prompt, schedule another pump even without new bytes. */
bool st_rip3_session_pending(const struct st_rip3_session *session);
/* EOF/transport cancellation: discard incomplete framing and owned host/prompt
 * work, cancel the delay and stop the exclusive player. Never take queued bytes
 * or emit text/replies. Retain completed table/store/drawing/cache effects; this
 * is not a full graphics reset. The source owner decides how to clear its queue. */
void st_rip3_session_cancel(struct st_rip3_session *session);

struct st_rip3_session_prompt {
	const struct st_rip3_variable_reference *reference;
	const struct st_rip3_variable_value *initial;
};
/* Borrowed prompt views, invalidated by response/process/cancel/destruction.
 * Report only a query-owned prompt; do not answer another consumer's prompt. */
bool st_rip3_session_prompt(const struct st_rip3_session *session,
	struct st_rip3_session_prompt *result);
void st_rip3_session_respond(struct st_rip3_session *session,
	const uint8_t *bytes, size_t length, bool cancelled);

struct st_rip3_session_capture {
	/* Owned full screen RGB view using the currently selected PAL record.
	 * No drawing/parser pumping occurs during this coherent observation. */
	struct st_pixel_buffer *pixels;
	/* Accepted raw WORLD state can contain a zero dimension. Capture remains
	 * valid; projection consumers separately require a nonzero frame. */
	struct st_rip3_frame frame;
	int64_t cursor_x, cursor_y;
	unsigned environment, style, port, palette;
	uint64_t wait_ns;
	bool viewport_active, query_pending, outline_cache_valid;
};
/* Resource/accessor service, not incoming-data processing. Failure preserves
 * result. The observation owns only pixels; release it before reuse. This
 * selected-palette view does not claim complete SCREEN/UI backup ownership. */
int st_rip3_session_capture(const struct st_rip3_session *session,
	struct st_rip3_session_capture *result);
void st_rip3_session_capture_dispose(struct st_rip3_session_capture *capture);

#endif
