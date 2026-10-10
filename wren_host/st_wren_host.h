/* Persistent Wren host for SyncTERM's menu and picker scripts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_HOST_H_
#define ST_WREN_HOST_H_

#include "st_profile.h"
#include "st_hyperlink.h"

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

enum st_wren_sftp_operation {
	ST_WREN_SFTP_REALPATH,
	ST_WREN_SFTP_STAT,
	ST_WREN_SFTP_OPENDIR,
	ST_WREN_SFTP_READDIR,
	ST_WREN_SFTP_CLOSE,
	ST_WREN_SFTP_OPEN,
	ST_WREN_SFTP_READ,
	ST_WREN_SFTP_WRITE,
	ST_WREN_SFTP_MKDIR,
	ST_WREN_SFTP_RMDIR,
	ST_WREN_SFTP_REMOVE,
	ST_WREN_SFTP_RENAME,
	ST_WREN_SFTP_SET_MTIME,
	ST_WREN_SFTP_DESCRIPTIONS,
};

struct st_wren_sftp_request {
	enum st_wren_sftp_operation operation;
	const char *path;
	const char *second_path;
	uint64_t handle;
	uint64_t offset;
	uint64_t modified_time;
	uint32_t flags;
	const void *data;
	size_t size;
};

struct st_wren_sftp_stat {
	uint64_t size;
	int64_t modified_time;
	int64_t accessed_time;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
};

struct st_wren_sftp_entry {
	const char *name;
	const char *long_name;
	struct st_wren_sftp_stat stat;
	bool directory;
	bool has_long_description;
	const void *hash;
	size_t hash_size;
};

enum st_wren_sftp_result_kind {
	ST_WREN_SFTP_RESULT_OK,
	ST_WREN_SFTP_RESULT_EOF,
	ST_WREN_SFTP_RESULT_STRING,
	ST_WREN_SFTP_RESULT_BYTES,
	ST_WREN_SFTP_RESULT_HANDLE,
	ST_WREN_SFTP_RESULT_STAT,
	ST_WREN_SFTP_RESULT_ENTRIES,
	ST_WREN_SFTP_RESULT_ERROR,
};

/* Response pointers remain borrowed only for the duration of poll(). */
struct st_wren_sftp_response {
	uint64_t request_id;
	enum st_wren_sftp_result_kind kind;
	const void *data;
	size_t size;
	uint64_t handle;
	struct st_wren_sftp_stat stat;
	const struct st_wren_sftp_entry *entries;
	size_t entry_count;
	int error_code;
	uint32_t server_status;
	const char *message;
	bool transient;
};

struct st_wren_sftp_provider {
	void *context;
	bool (*available)(void *context);
	const char *(*public_directory)(void *context);
	const char *(*long_name_extension)(void *context);
	/* submit() must copy every borrowed request pointer before returning and
	 * return an ID unique among requests that have not yet been polled. */
	int (*submit)(void *context, const struct st_wren_sftp_request *request,
		uint64_t *request_id);
	/* Returns one for a response, zero when none is ready, or an error. */
	int (*poll)(void *context, struct st_wren_sftp_response *response,
		uint32_t timeout_ms);
	void (*cancel_all)(void *context);
};

struct st_wren_connected_config {
	st_wren_host_t *host;
	st_console_t *console;
	const struct st_profile *profile;
	const struct st_wren_sftp_provider *sftp;
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

/* Refreshes configured web-list caches with the menu progress overlay. */
int st_wren_host_refresh_web_lists(st_wren_host_t *host);

/*
 * Runs the persistent main-menu VM until it selects an entry or exits.
 * selected must be initialized; it is replaced only when has_selection is
 * set true. A selected entry is returned to the future connection layer.
 */
int st_wren_host_run_menu(st_wren_host_t *host,
	struct st_profile *selected, bool *has_selection);

/*
 * Creates the same temporary profile as Menu.quickConnect(). An UNKNOWN
 * default uses the BBS-list default; an explicit URL scheme still wins.
 */
int st_wren_host_select_url(st_wren_host_t *host, const char *url,
	int default_connection_type, struct st_profile *selected);

bool st_wren_host_exit_requested(const st_wren_host_t *host);
int st_wren_host_set_screen_mode(st_wren_host_t *host,
	enum st_screen_mode selected);
int st_wren_host_get_screen_mode(const st_wren_host_t *host,
	enum st_screen_mode *result);
const char *st_wren_host_last_error(const st_wren_host_t *host);

/* Connected Wren lifetime; application connection wiring is deliberately
 * separate from this provider-neutral host. */
int st_wren_connected_create(st_wren_connected_t **result,
	const struct st_wren_connected_config *config);
void st_wren_connected_destroy(st_wren_connected_t *connected);
int st_wren_connected_pump(st_wren_connected_t *connected,
	uint32_t timeout_ms);
int st_wren_connected_interpret(st_wren_connected_t *connected,
	const char *module, const char *source);
int st_wren_connected_shell_closed(st_wren_connected_t *connected);
const char *st_wren_connected_last_error(
	const st_wren_connected_t *connected);

#endif
