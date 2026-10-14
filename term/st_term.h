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

#include <stddef.h>

typedef struct st_term_session st_term_session_t;

enum st_term_disposition {
	ST_TERM_RETURN_TO_PICKER,
	ST_TERM_EXIT_APPLICATION,
};

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
	enum st_codepage codepage;
	uint8_t font_selector;
	bool hide_status;
	st_cterm_rip_prompt_callback_t rip_prompt;
	void *rip_prompt_context;
};

int st_term_connect_profile(st_connection_t **result,
	const struct st_profile *profile, st_console_t *console,
	const struct st_connection_hooks *hooks, struct st_connection_error *error);
/* Local PTY sessions exchange UTF-8; byte-oriented remote transports do not. */
enum st_cterm_encoding st_term_profile_encoding(
	const struct st_profile *profile);
enum st_cterm_rip_version st_term_profile_rip_version(
	const struct st_profile *profile);
int st_term_session_create(st_term_session_t **result,
	const struct st_term_config *config);
void st_term_session_destroy(st_term_session_t **session);

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

/* RIP wire-byte boundary: preserve ASCII protocol bytes, transcode high
 * codepage bytes to UTF-8. result_size excludes any terminator (none is
 * written). The input and output buffers must not overlap. */
int st_term_rip_wire_to_utf8(char *result, size_t capacity,
	size_t *result_size, const uint8_t *wire, size_t size,
	enum st_codepage codepage);

#endif
