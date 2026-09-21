/* XDG application paths for POSIX systems. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths_internal.h"

#include "io_error.h"
#include "path.h"

#include <stdlib.h>

static char *
environment_or_home(const char *environment, const char *home_suffix,
    bool require_absolute)
{
	const char *value = getenv(environment);
	const char *home;

	if (value != NULL && value[0] != '\0'
	    && (!require_absolute || path_is_rooted(value)))
		return st_app_path_copy(value);
	home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		home = ".";
	return st_app_path_join(home, home_suffix);
}

int
st_app_paths_platform_init(struct st_app_paths *paths)
{
	char *config = NULL;
	char *cache = NULL;
	char *download = NULL;
	char *data = NULL;

	config = environment_or_home("XDG_CONFIG_HOME", ".config", true);
	cache = environment_or_home("XDG_CACHE_HOME", ".cache", true);
	download = environment_or_home("XDG_DOWNLOAD_DIR", "Downloads", false);
	data = environment_or_home("XDG_DATA_HOME", ".local/share", true);
	if (config != NULL) {
		char *base = st_app_path_join(config, "syncterm");
		free(config);
		config = base;
	}
	if (cache != NULL) {
		char *base = st_app_path_join(cache, "syncterm");
		free(cache);
		cache = base;
	}
	if (data != NULL) {
		char *base = st_app_path_join(data, "syncterm");
		free(data);
		data = base;
	}
	if (config == NULL || cache == NULL || download == NULL || data == NULL)
		goto no_memory;
	paths->settings_file = st_app_path_join(config, "syncterm.ini");
	paths->personal_list = st_app_path_join(config, "syncterm.lst");
	paths->shared_list = st_app_path_copy("/usr/local/etc/syncterm.lst");
	paths->cache_directory = cache;
	paths->download_directory = download;
	cache = NULL;
	download = NULL;
	paths->keys_file = st_app_path_join(data, "syncterm.ssh");
	paths->scripts_directory = st_app_path_join(data, "scripts");
	paths->themes_directory = st_app_path_join(data, "themes");
	free(config);
	config = NULL;
	free(data);
	data = NULL;
	if (paths->settings_file == NULL || paths->personal_list == NULL
	    || paths->shared_list == NULL || paths->keys_file == NULL
	    || paths->scripts_directory == NULL || paths->themes_directory == NULL)
		goto no_memory;
	return 0;

no_memory:
	free(config);
	free(cache);
	free(download);
	free(data);
	return IO_ERROR_OUT_OF_MEMORY;
}
