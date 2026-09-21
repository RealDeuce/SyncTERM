/* Persistent Wren host for SyncTERM's menu and picker scripts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_HOST_H_
#define ST_WREN_HOST_H_

#include "st_profile.h"

#include <stdbool.h>

typedef struct st_wren_host st_wren_host_t;
typedef struct st_console st_console_t;
typedef struct st_console_display st_console_display_t;

struct st_app_paths;
struct st_audio_runtime;
struct st_profile;
struct st_session_history;

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
int st_wren_host_get_screen_mode(const st_wren_host_t *host,
	enum st_screen_mode *result);
const char *st_wren_host_last_error(const st_wren_host_t *host);

#endif
