/* Minimal connected-session loop for the modern CTerm component. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_TERM_H_
#define ST_TERM_H_

#include "st_audio_runtime.h"
#include "st_connection.h"
#include "st_cterm.h"
#include "st_font_catalog.h"
#include "st_profile.h"
#include "st_session_history.h"
#include "st_transfer.h"

#include <stddef.h>

typedef struct st_term_session st_term_session_t;
/* Owner-thread keep-alive for side-band work after terminal EOF. */
void st_term_session_set_auxiliary_active(st_term_session_t *session, bool active);

enum st_term_disposition {
	ST_TERM_RETURN_TO_PICKER,
	ST_TERM_EXIT_APPLICATION,
};

#define ST_TERM_WIRE_REPLACEMENT_CAPACITY 256

typedef int (*st_term_wire_filter_t)(void *context, uint8_t byte,
	bool *consumed, uint8_t *replacement, size_t capacity,
	size_t *replacement_size);

struct st_term_config {
	/* Borrowed for the session lifetime. */
	st_console_t *console;
	st_connection_t *connection;
	st_font_catalog_t *fonts;
	st_audio_runtime_t *audio;
	st_hyperlink_registry_t *hyperlinks;
	st_session_history_t *history;
	enum st_screen_mode screen_mode;
	/* Remote wire encoding. RIP sessions translate to/from UTF-8 at this
	 * boundary; CTerm and its RIP parser always receive UTF-8. */
	enum st_cterm_encoding encoding;
	enum st_cterm_rip_version rip_version;
	const char *rip_directory;
	const char *apc_directory;
	const char *application_version;
	enum st_codepage codepage;
	uint8_t font_selector;
	bool hide_status;
	/* Pass the entry's LF Expand setting through to CTerm. */
	bool expand_lf;
	bool force_lcf;
	bool yellow_is_yellow;
	const uint32_t *palette;
	size_t palette_size;
	/* MQTT accepts user input but not automatic terminal replies. */
	bool suppress_responses;
	/* BPS receive pacing for non-serial sessions; zero disables it. */
	bool throttle_supported;
	uint32_t initial_throttle_bps;
	st_cterm_rip_prompt_callback_t rip_prompt;
	void *rip_prompt_context;
	st_cterm_rip_transfer_callback_t rip_transfer;
	void *rip_transfer_context;
	/* Called before built-in event handling; true consumes the event. */
	int (*event_hook)(void *context, const struct st_console_event *event,
	    bool *consumed);
	void *event_hook_context;
	/* Exact received bytes, before RIP/CTerm parsing. Replacement bytes are
	 * reclassified by CTerm in order, without revisiting this filter. */
	st_term_wire_filter_t wire_filter;
	/* Optional fast path when no wire hooks are registered. */
	bool (*wire_filter_active)(void *context);
	void *wire_filter_context;
	/* Semantic input after CTerm's shared UTF-8 decoder or codepage map.
	 * Replacements bypass both the wire hook and RIP parser. */
	st_cterm_input_filter_t input_filter;
	bool (*input_filter_active)(void *context);
	void *input_filter_context;
	/* Original received bytes before wire hooks, RIP conversion, and decoding.
	 * Both capture callbacks use capture_active and capture_context. */
	int (*wire_capture)(void *context, const uint8_t *bytes, size_t size);
	/* Decoded text after semantic hooks, with protocol bytes tagged. */
	st_cterm_capture_t capture;
	bool (*capture_active)(void *context);
	void *capture_context;
	int cterm_log_level;
	st_cterm_log_callback_t cterm_diagnostic;
	void *cterm_diagnostic_context;
};

int st_term_connect_profile(st_connection_t **result,
	const struct st_profile *profile, st_console_t *console,
	const struct st_connection_hooks *hooks, struct st_connection_error *error);
/* The profile's explicit encoding determines the wire boundary. */
enum st_cterm_encoding st_term_profile_encoding(
	const struct st_profile *profile);
enum st_cterm_rip_version st_term_profile_rip_version(
	const struct st_profile *profile);
int st_term_session_create(st_term_session_t **result,
	const struct st_term_config *config);
void st_term_session_destroy(st_term_session_t **session);
/* Borrowed until session destruction; the caller must not write through it. */
st_cterm_t *st_term_session_cterm(st_term_session_t *session);
bool st_term_session_font_selector(const st_term_session_t *session,
	uint16_t identifier, uint8_t *selector);
void st_term_session_request_end(st_term_session_t *session, bool exit_app);
/* True only when the pump ended on a drained remote connection, not a local
 * hangup/exit request. Available until destruction. */
bool st_term_session_remote_closed(const st_term_session_t *session);
/* After closing the connection, make room for a local disconnect notice.
 * Its row becomes a blank main-display row in the published history. */
int st_term_session_prepare_disconnect(st_term_session_t *session);
void st_term_session_set_suspended(st_term_session_t *session, bool suspended);
/* Owner-only handoff. The session remains the sole transport reader and
 * feeds protocol input while a modal UI suspends ordinary terminal display. */
int st_term_session_begin_transfer(st_term_session_t *session, st_transfer_t *transfer);
int st_term_session_end_transfer(st_term_session_t *session);
void st_term_session_reserve_transfer(st_term_session_t *session, bool reserved);
/* A RIP block-mode command ends with CR; its optional LF is framing. */
void st_term_session_reserve_rip_transfer(st_term_session_t *session);
const uint8_t *st_term_session_prestel_frame(st_term_session_t *session, size_t *size);
void st_term_session_set_mouse_disabled(st_term_session_t *session,
	bool disabled);
uint32_t st_term_session_throttle_speed(const st_term_session_t *session);
void st_term_session_set_throttle_speed(st_term_session_t *session,
	uint32_t bps);
int st_term_session_ooii_mode(const st_term_session_t *session);
void st_term_session_set_ooii_mode(st_term_session_t *session, int mode);

/*
 * Runs until disconnect or a local command. The caller owns and closes the
 * connection. Alt-H and Ctrl-Q return to the picker; Alt-X and CLOSE exit.
 */
int st_term_session_run(st_term_session_t *session,
	enum st_term_disposition *disposition);
int st_term_session_pump(st_term_session_t *session,
	enum st_term_disposition *disposition, bool *finished);

/*
 * Sends finite Unicode text. UTF-8 is sent unchanged after validation/NFC.
 * A legacy session encodes each grapheme once. An unmappable grapheme beeps
 * locally and is omitted while representable neighbours continue.
 */
int st_term_session_send_text(st_term_session_t *session,
	const char *utf8, size_t size);
/* Nonblocking variant for Wren; queued_wire counts final encoded bytes. */
int st_term_session_send_text_poll(st_term_session_t *session,
	const char *utf8, size_t size, size_t *queued_wire);
/* Allocates clipboard text with legacy newline and bracketed-paste rules. */
int st_term_prepare_paste(char **result, size_t *result_size,
	const char *utf8, size_t size, bool bracketed);

/* RIP wire-byte boundary: preserve ASCII protocol bytes, transcode high
 * codepage bytes to UTF-8. result_size excludes any terminator (none is
 * written). The input and output buffers must not overlap. */
int st_term_rip_wire_to_utf8(char *result, size_t capacity,
	size_t *result_size, const uint8_t *wire, size_t size,
	enum st_codepage codepage);

#endif
