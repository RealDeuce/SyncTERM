/* Stateful byte-stream connections used by SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONNECTION_H_
#define ST_CONNECTION_H_

#include "io_error.h"
#include "st_profile.h"
#include "syncterm_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct st_connection st_connection_t;

enum st_connection_setup_phase {
	ST_CONNECTION_SETUP_NONE,
	ST_CONNECTION_SETUP_RESOLVING,
	ST_CONNECTION_SETUP_CONNECTING,
	ST_CONNECTION_SETUP_OPENING_DEVICE,
	ST_CONNECTION_SETUP_INITIALIZING_MODEM,
	ST_CONNECTION_SETUP_DIALING,
	ST_CONNECTION_SETUP_STARTING_SHELL,
};

enum st_connection_setup_action {
	ST_CONNECTION_SETUP_CONTINUE,
	ST_CONNECTION_SETUP_CANCEL,
};

enum st_connection_log_level {
	ST_CONNECTION_LOG_ERROR,
	ST_CONNECTION_LOG_WARNING,
	ST_CONNECTION_LOG_INFO,
	ST_CONNECTION_LOG_DEBUG,
};

struct st_screen_geometry {
	uint32_t columns;
	uint32_t rows;
	uint32_t pixel_width;
	uint32_t pixel_height;
};

/*
 * Optional POSIX shell environment values. The strings are values only, are
 * borrowed for the duration of connect, and may be NULL or empty to omit the
 * corresponding variable. Inherited TERMCAP and MM_CHARSET values are always
 * removed. Windows shell transports accept this argument but ignore it.
 */
struct st_connection_shell_environment {
	const char *termcap;
	const char *mm_charset;
};

typedef enum st_connection_setup_action (*st_connection_setup_fn)(
	void *context, enum st_connection_setup_phase phase);
typedef void (*st_connection_log_fn)(void *context,
	enum st_connection_log_level level, const char *source,
	const char *message);

struct st_connection_hooks {
	st_connection_setup_fn setup;
	void *setup_context;
	st_connection_log_fn log;
	void *log_context;
};

struct st_connection_error {
	int code;
	enum st_connection_setup_phase phase;
	uint32_t native_error;
	bool has_native_error;
	char detail[1024];
};

/*
 * Result is unchanged on failure. Profile, shell environment, and the hooks
 * structure may be released on return. Hook contexts remain owned by the
 * caller and must stay valid through close; the log hook may be called by
 * either worker thread.
 * For RLogin and shell, current_screen is the complete current screen when
 * the profile selects CURRENT; the initial terminal height excludes the
 * status row unless hide_status is set. Other modes obtain their complete
 * screen dimensions from the profile and global custom-mode settings.
 * On POSIX, a nonempty shell profile address is interpreted by /bin/sh -c;
 * an empty address directly executes the nonempty SHELL path, or /bin/sh when
 * SHELL is absent or empty.
 */
int st_connection_connect_raw(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error);
int st_connection_connect_rlogin(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error);
int st_connection_connect_serial(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error);
int st_connection_connect_modem(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error);
int st_connection_connect_shell(st_connection_t **result,
	const struct st_profile *profile,
	const struct st_screen_geometry *current_screen,
	const struct st_connection_shell_environment *shell_environment,
	const struct st_connection_hooks *hooks,
	struct st_connection_error *error);

/*
 * Timeouts are milliseconds: zero polls, -1 waits forever, and values below
 * -1 are invalid. Poll exhaustion is WOULD_BLOCK and a positive timeout
 * expires with TIMED_OUT. Positive short counts are valid.
 */
ssize_t st_connection_send(st_connection_t *connection, const void *data,
	size_t size, int64_t timeout_ms);
ssize_t st_connection_send_raw(st_connection_t *connection, const void *data,
	size_t size, int64_t timeout_ms);
ssize_t st_connection_receive(st_connection_t *connection, void *data,
	size_t size, int64_t timeout_ms);
ssize_t st_connection_receive_raw(st_connection_t *connection, void *data,
	size_t size, int64_t timeout_ms);
ssize_t st_connection_peek_raw(st_connection_t *connection, void *data,
	size_t size);
size_t st_connection_pending_raw(st_connection_t *connection);
size_t st_connection_queued(st_connection_t *connection);
bool st_connection_is_connected(st_connection_t *connection);
uint64_t st_connection_elapsed_ms(st_connection_t *connection);
int st_connection_set_binary_mode(st_connection_t *connection, bool enabled);
/* Geometry here is the usable terminal area after any status-row change. */
int st_connection_update_terminal_size(st_connection_t *connection,
	const struct st_screen_geometry *geometry);

/*
 * Stops and joins both workers, releases the transport, and frees connection.
 * The owner must prevent close from racing any other public operation.
 */
int st_connection_close(st_connection_t *connection);

#endif
