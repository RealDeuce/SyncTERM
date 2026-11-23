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
struct st_rip3_text_window_snapshot;
struct st_rip3_text_cursor_position;
struct st_rip3_rect;
struct st_rip3_palette;

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
	 * parser consumes failures nonfatally and retains its prior layout/gate.
	 * clear_rgb resolves current PAL index0 for extended flag16; ignored otherwise. */
	int (*text_window)(void *context, const struct st_rip3_text_window_layout *layout, bool identical,
		uint32_t clear_rgb);
	/* First wire selection clones the source; first CUR selection supplies
	 * fresh default geometry instead. Neither path copies pixels/history. */
	int (*text_select)(void *context, unsigned number, bool first, bool active,
		const struct st_rip3_text_window_layout *fresh);
	/* Preliminary ANSI reset on the old current TW, then replace TW0 with
	 * the supplied defaults and discard unprotected nonzero views. Protected
	 * views retain their definitions/activation. Preserve connection ANSI syntax
	 * and TW0 tabs; preserve only protected nonzero views from keep[]. */
	int (*text_reset)(void *context, const struct st_rip3_text_window_layout *layout,
		const bool keep[36], bool old_active);
	/* Observe the specified native owner without selecting it or presenting. */
	int (*text_snapshot)(void *context, unsigned number, struct st_rip3_text_window_snapshot *result);
	/* One live TW definition copy, below complete permission preflight. Retain
	 * current selection and connection ANSI syntax; replace destination metadata
	 * without copying cells/history/pixels. The owner handles query/protection.
	 * A later failed helper must not prevent subsequent destinations. */
	int (*text_copy)(void *context,unsigned source,unsigned destination,bool active);
	/* Explicit whole-TW backup support. Capture owns native definitions/fonts,
	 * not screen pixels, cells, history or connection parser syntax. Restore prepares
	 * replacement views before publishing, preserves displayed pixels and
	 * retains current partial ANSI input. All three or none; destroy has no
	 * session dependency. Saved objects are private to this connection. */
	int (*text_backup_capture)(void *context,const bool defined[36],void **result);
	int (*text_backup_restore)(void *context,const void *saved,unsigned current,bool active);
	void (*text_backup_destroy)(void *saved);
	/* Native text-window cursor movement, without synthesized received bytes. */
	int (*text_cursor)(void *context, unsigned number, const struct st_rip3_text_cursor_position *position);
	int (*text_cursor_visible)(void *context, unsigned number, bool visible);
	/* Paint the native window with untagged RGB. Return the absolute clipped
	 * DEVICE region for the owner's matching index-plane COPY. Whole erase
	 * homes the live cursor only; EOL retains it. Empty is 0,0,-1,-1. */
	int (*text_erase)(void *context, unsigned number, bool whole, uint32_t rgb, struct st_rip3_rect *area);
	/* Native ANSI-background EOL erase. Like ordinary ANSI text, this paints
	 * the terminal surface rather than changing the graphics palette indices. */
	int (*text_erase_eol)(void *context, unsigned number);
	/* Transient mouse feedback on the shared native text/graphics raster. */
	void (*mouse_invert)(void *context, const struct st_rip3_rect *area);
	/* Publish completed owned-button redraws, including momentary keyboard
	 * feedback before its action runs. No additional raster ownership. */
	void (*mouse_redraw)(void *context);
	/* Capture the committing field event before starting its host program. */
	void (*mouse_capture)(void *context, int32_t x, int32_t y);
	/* Explicit screen backups only: merge native ANSI into a prepared raw
	 * graphics capture; restore both planes without changing ANSI/window state.
	 * Bind both or neither. Accessor reads are allowed, parser reentry is not. */
	int (*screen_capture)(void *context,struct st_pixel_buffer *);
	int (*screen_restore)(void *context,const struct st_pixel_buffer *,const struct st_rip3_palette *);
	/* Enrich a bounded raw screen-source snapshot with native ANSI pixels.
	 * Called only for protocol read transfers, never ordinary text rendering.
	 * Preserve raw graphic indexes where native output agrees, and do not
	 * replace graphics written by the current still-unpresented command. */
	int (*screen_read)(void *context,const struct st_rip3_rect *,struct st_pixel_buffer *);
	/* Prepare/commit one live TW reset without painting. A non-NULL fresh
	 * layout replaces zero with forced CUR-first-use defaults, retaining current
	 * selection. NULL deletes a nonzero native view; if selected, fall back to
	 * the existing zero with zero_active, without resetting/activating it.
	 * Failure preserves the target definition and current selection. */
	int (*text_reset_entry)(void *context,unsigned number,
		const struct st_rip3_text_window_layout *fresh,bool zero_active);
	/* ESC7/8 native metadata only, separate from whole-table backups. Capture
	 * owns logical font descriptors, not resources. Restore prepares a complete
	 * current target before publication, paints nothing, and retains selection.
	 * All three callbacks or none; destroy has no live-session dependency. */
	int (*text_ansi_capture)(void *context,bool font_known,void **result);
	int (*text_ansi_restore)(void *context,const void *saved);
	void (*text_ansi_destroy)(void *saved);
};

struct st_rip3_session_config {
	enum st_rip3_canvas canvas;
	/* Both zero selects canvas. Otherwise both supply the entry's physical
	 * raster, which must never be replaced by a parser preset. */
	uint32_t device_width, device_height;
	/* Client's default MicroANSI font 0..4 and wrap preference for newly
	 * defaulted windows. Independent of the entry raster and ordinary TTY. */
	unsigned default_text_font;
	bool default_text_wrap;
	enum st_rip3_text_encoding encoding;
	/* The terminal normalized a single-byte wire codepage to UTF-8. Graphical
	 * limits still count original bytes; standalone byte parsers leave false. */
	bool graphical_codepage_converted;
	struct st_rip3_session_source source;
	struct st_rip3_session_sink sink;
	/* Optional copied external service bindings. The frame, tables, input,
 * templates, text_windows, mouse and field_context must be NULL: this session
 * owns those.
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
 * session. Callbacks must not reenter parsing, cancel/destroy or change its bindings.
 * Creation prepares all represented tables/backups, font cache, variables,
 * templates, stream, query and input gate before publishing result. Failure
 * preserves result and borrowed services and invokes no client callback.
 * This does not select an application terminal mode or implement field zoom
 * or complete scene headers. Full TW backups require the three native
 * text_backup callbacks; clients may leave that service unbound. T/@, button filename/label and metric arguments share the bounded
 * graphical preprocessor. Metrics retain expanded TEXTDATA outside the tables;
 * exact r/@ sentinels read it without expansion. Full drawing resets empty it.
 * Immediate host queries include their angle preprocessor. */
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
/* CTerm calls through its owner after recognizing ESC7/8 and unwinding input.
 * One reusable connection save; failures are diagnosed here and consumed.
 * May be called from sink.text, but invokes no received-input/parser callback. */
void st_rip3_session_ansi_window(struct st_rip3_session *session,bool restore);
bool st_rip3_session_mouse_active(const struct st_rip3_session *session);
/* Current ENV metadata only; no raster allocation/presentation. */
bool st_rip3_session_mouse_pointer(const struct st_rip3_session *session, unsigned *result);
/* Input/gesture failures are absorbed internally just like wire failures. */
bool st_rip3_session_mouse(struct st_rip3_session *session, int32_t x, int32_t y, bool left);
/* Physical movement only; shares pending()/prompt ownership with clicks. */
void st_rip3_session_mouse_move(struct st_rip3_session *session,int32_t x,int32_t y);
bool st_rip3_session_key(struct st_rip3_session *session, uint8_t byte);
uint64_t st_rip3_session_press_token(const struct st_rip3_session *session);
void st_rip3_session_mouse_cancel(struct st_rip3_session *session);
/* Focus/pointer-delivery boundary, distinct from capture-only cancellation. */
void st_rip3_session_pointer_boundary(struct st_rip3_session *session);
/* Presentation services. Revision advances after commands/host continuations;
 * it does not promise that pixels changed. Damage is borrowed through unbind
 * or destruction, one byte per canvas pixel; see surface_damage. */
uint64_t st_rip3_session_revision(const struct st_rip3_session *session);
int st_rip3_session_damage(struct st_rip3_session *session,
	uint8_t *damage, size_t length);
uint64_t st_rip3_session_wait_ns(const struct st_rip3_session *session);
/* Local host work remains, independently of source availability. If ready
 * and not waiting for a prompt, schedule another pump even without new bytes. */
/* Consumer-only gate; host queries retain their existing scheduling policy. */
bool st_rip3_session_graphical_pending(const struct st_rip3_session *session);
/* Abandon only a failed render continuation; preserve stream framing/stores. */
void st_rip3_session_graphical_cancel(struct st_rip3_session *session);
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
 * graphics-only view is not the native physical SCREEN capture; explicit
 * screen backups also use the paired sink services for ANSI/blink pixels. */
int st_rip3_session_capture(const struct st_rip3_session *session,
	struct st_rip3_session_capture *result);
void st_rip3_session_capture_dispose(struct st_rip3_session_capture *capture);

#endif
