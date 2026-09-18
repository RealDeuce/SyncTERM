/* SyncTERM application path selection. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_APP_PATHS_H_
#define ST_APP_PATHS_H_

#include <stddef.h>

struct st_app_path_overrides {
	const char *settings_file;
	const char *personal_list;
	const char *shared_list;
	const char *cache_directory;
	const char *download_directory;
	const char *keys_file;
	const char *scripts_directory;
	const char *themes_directory;
};

struct st_app_paths {
	char *settings_file;
	char *personal_list;
	char *shared_list;
	char *cache_directory;
	char *system_cache_directory;
	char *cached_system_list;
	char *download_directory;
	char *keys_file;
	char *scripts_directory;
	char *themes_directory;
};

int st_app_paths_init(struct st_app_paths *paths,
    const struct st_app_path_overrides *overrides);
void st_app_paths_dispose(struct st_app_paths *paths);

/* Creates only the parent required by the selected output path. */
int st_app_paths_prepare_settings(const struct st_app_paths *paths);
int st_app_paths_prepare_personal_list(const struct st_app_paths *paths);
int st_app_paths_prepare_cache(const struct st_app_paths *paths);
int st_app_paths_prepare_keys(const struct st_app_paths *paths);
int st_app_paths_prepare_scripts(const struct st_app_paths *paths);
int st_app_paths_prepare_themes(const struct st_app_paths *paths);

#endif
