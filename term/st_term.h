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
	enum st_cterm_encoding encoding;
	enum st_codepage codepage;
	uint8_t font_selector;
	bool hide_status;
};

int st_term_connect_profile(st_connection_t **result,
	const struct st_profile *profile, st_console_t *console,
	const struct st_connection_hooks *hooks, struct st_connection_error *error);
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

#endif
