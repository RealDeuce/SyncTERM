/* Persistent Wren host for SyncTERM's menu and picker scripts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_HOST_H_
#define ST_WREN_HOST_H_

#include "st_profile.h"
#include "st_hyperlink.h"
#include "st_cterm.h"
#include "st_sftp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_wren_host st_wren_host_t;
typedef struct st_wren_connected st_wren_connected_t;
typedef struct st_console st_console_t;
typedef struct st_console_display st_console_display_t;

struct st_app_paths;
struct st_audio_runtime;
struct st_profile;
struct st_session_history;
struct st_connection;
struct st_term_session;


struct st_wren_connected_config {
	st_wren_host_t *host;
	st_console_t *console;
	const struct st_profile *profile;
	/* NULL for connections without an SFTP provider. */
	const struct st_sftp_provider *sftp;
	/* Optional only for isolated binding tests. Borrowed for VM lifetime. */
	st_cterm_t *cterm;
	struct st_connection *connection;
	struct st_term_session *session;
	bool status_enabled;
	/* Owner-thread transfer diagnostics; protocol diagnostics retain their
	 * independent connection-hook threshold. */
	void (*transfer_log)(void *context, int level, const char *message);
	void *transfer_log_context;
	/* Optional application-owned current-session log. On success return a
	 * malloc-owned UTF-8 snapshot (empty is allowed), freed by the binding. */
	int (*session_log)(void *context, char **text);
	void *session_log_context;
};

struct st_wren_host_config {
	st_console_display_t *display;
	st_console_t *console;
	/* NULL retains the single-console compatibility path used by tests. */
	st_console_t *picker_console;
	const struct st_app_paths *paths;
	const char *application_title;
	const char *launch_script;
	struct st_session_history *session_history;
	struct st_audio_runtime *audio_runtime;
	/* Borrowed for the host and every connected-context lifetime. */
	st_hyperlink_registry_t *hyperlinks;
	bool safe_mode;
};

/* The console and paths remain borrowed for the host's lifetime. */
int st_wren_host_create(st_wren_host_t **result,
	const struct st_wren_host_config *config);
void st_wren_host_destroy(st_wren_host_t *host);
bool st_wren_host_safe_mode(const st_wren_host_t *host);
/* Update session counters even in safe mode; persist only saved entries. */
int st_wren_host_record_connection(st_wren_host_t *host,
	struct st_profile *profile);
void st_wren_connected_log_transfer(st_wren_connected_t *connected,
	int level, const char *message);

/* Refreshes configured web-list caches with the menu progress overlay. */
int st_wren_host_refresh_web_lists(st_wren_host_t *host);

/*
 * Runs the persistent main-menu VM until it selects an entry or exits.
 * selected must be initialized; it is replaced only when has_selection is
 * set true. A selected entry is returned to the future connection layer.
 */
int st_wren_host_run_menu(st_wren_host_t *host,
	struct st_profile *selected, bool *has_selection);

/* C-owned connected-session prompt, rendered by the persistent menu VM.
 * A NULL result means the user cancelled; an accepted string is malloc-owned.
 * The connected console is restored after the dialog. */
int st_wren_host_prompt(st_wren_host_t *host, const char *title,
	const char *question, const char *initial, size_t max_length,
	char **result);
int st_wren_host_prompt_masked(st_wren_host_t *host, const char *title,
	const char *question, const char *initial, size_t max_length,
	bool masked, char **result);
/* Returns a choice index, or IO_ERROR_CANCELLED when dismissed. */
int st_wren_host_choice(st_wren_host_t *host, const char *title,
	const char *question, const char *const *choices, size_t count);
int st_wren_host_alert(st_wren_host_t *host, const char *title, const char *message);
int st_wren_host_help(st_wren_host_t *host, const char *title, const char *body);
/* Enter/close exits; F1 opens details/log, Alt-B (Cmd-B on macOS) opens history.
 * Caller must first finish output and publish history before local UI drawing. */
int st_wren_host_disconnected(st_wren_host_t *host, const char *details);
/* Nonblocking progress; NULL message clears and restores the saved screen. */
int st_wren_host_status(st_wren_host_t *host, const char *title, const char *message);
bool st_wren_host_can_trust_tls(st_wren_host_t *host, const struct st_profile *profile);
int st_wren_host_trust_tls(st_wren_host_t *host, const struct st_profile *profile,
	const char *pem, char **filename);
int st_wren_host_remember_tls_version(st_wren_host_t *host,
	const struct st_profile *profile, int version);
/* Reapplies session-only observations after a directory reload, but never
 * across changed peer/trust settings or an explicitly edited version floor. */
void st_wren_host_apply_tls_version(st_wren_host_t *host, struct st_profile *profile);
int st_wren_host_remember_ssh_key(st_wren_host_t *host,
	const struct st_profile *profile, const uint8_t fingerprint[32],
	bool explicit_update);

/*
 * Creates the same temporary profile as Menu.quickConnect(). An UNKNOWN
 * default uses the BBS-list default; an explicit URL scheme still wins.
 */
int st_wren_host_select_url(st_wren_host_t *host, const char *url,
	int default_connection_type, struct st_profile *selected);
/* Offer a temporary URL entry at application exit. Respect PromptSave, safe
 * mode and HidePopups. Prepare the directory before copying the borrowed
 * profile into menu-owned storage. Cancellation is success with saved=false. */
int st_wren_host_offer_save_profile(st_wren_host_t *host,
	const struct st_profile *profile, bool *saved);

bool st_wren_host_exit_requested(const st_wren_host_t *host);
int st_wren_host_set_screen_mode(st_wren_host_t *host,
	enum st_screen_mode selected);
int st_wren_host_get_screen_mode(const st_wren_host_t *host,
	enum st_screen_mode *result);
/* Capture the actual session grid for detached history presentation. */
int st_wren_host_capture_history_mode(st_wren_host_t *host);
int st_wren_host_begin_skypix_session(st_wren_host_t *host, bool hide_status);
/* Bracket the entire connection attempt, including dialogs and teardown.
 * Begin saves the menu's current mode/geometry before applying selected;
 * CURRENT keeps that mode. Nested begins are rejected. End clears the
 * session surface and restores the menu mode, fonts, palette and cursor.
 * End is harmless without a successful begin. Both return an IO_ERROR on
 * failure; a failed begin also attempts to restore the saved menu mode. */
int st_wren_host_begin_session_mode(st_wren_host_t *host,
	enum st_screen_mode selected);
int st_wren_host_end_session_mode(st_wren_host_t *host);
const char *st_wren_host_last_error(const st_wren_host_t *host);

/* Connected Wren lifetime; application connection wiring is deliberately
 * separate from this provider-neutral host. */
int st_wren_connected_create(st_wren_connected_t **result,
	const struct st_wren_connected_config *config);
void st_wren_connected_destroy(st_wren_connected_t *connected);
int st_wren_connected_pump(st_wren_connected_t *connected,
	uint32_t timeout_ms);
/* Paints the connected status row when enabled and a callable is installed. */
int st_wren_connected_render_status(st_wren_connected_t *connected,
	bool enabled);
/* Snapshots Status.details once. Returns a malloc-owned string, or NULL
 * when no callable/string is available (including a script error). */
char *st_wren_connected_status_details(st_wren_connected_t *connected);
int st_wren_connected_interpret(st_wren_connected_t *connected,
	const char *module, const char *source);
int st_wren_connected_shell_closed(st_wren_connected_t *connected);
/* Synchronous CTerm owner callback; never enters the Wren VM. */
int st_wren_connected_skypix_transfer(st_wren_connected_t *connected, unsigned mode,
	unsigned width, unsigned height, const uint8_t *name, size_t size);
int st_wren_connected_rip_transfer(st_wren_connected_t *connected,
	const struct st_cterm_rip_transfer_request *request);
bool st_wren_connected_auxiliary_active(const st_wren_connected_t *connected);
int st_wren_connected_handle_event(st_wren_connected_t *connected,
	const struct st_console_event *event, bool *consumed);
int st_wren_connected_filter_wire_byte(st_wren_connected_t *connected,
	uint8_t byte, bool *consumed, uint8_t *replacement, size_t capacity,
	size_t *replacement_size);
bool st_wren_connected_has_wire_hooks(const st_wren_connected_t *connected);
int st_wren_connected_filter_input_scalar(st_wren_connected_t *connected,
	uint32_t scalar, bool *consumed, char *replacement, size_t capacity,
	size_t *replacement_size);
bool st_wren_connected_has_input_hooks(const st_wren_connected_t *connected);
bool st_wren_connected_capture_active(const st_wren_connected_t *connected);
/* Original connection bytes for ANSI capture; decoded input is plain text only. */
int st_wren_connected_capture_wire(st_wren_connected_t *connected,
	const uint8_t *bytes, size_t size);
int st_wren_connected_capture_input(st_wren_connected_t *connected,
	const char *bytes, size_t size, bool protocol);
const char *st_wren_connected_last_error(
	const st_wren_connected_t *connected);

#endif
