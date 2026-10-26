/* Native macOS application paths. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths_internal.h"

#include "io_error.h"

#include <stdlib.h>

int
st_app_paths_platform_init(struct st_app_paths *paths)
{
	const char *home = getenv("HOME");
	char *support;

	if (home == NULL || home[0] == '\0')
		home = ".";
	support = st_app_path_join(home, "Library/Application Support/SyncTERM");
	paths->cache_directory = st_app_path_join(home,
	    "Library/Caches/SyncTERM");
	paths->download_directory = st_app_path_join(home, "Downloads");
	paths->shared_list = st_app_path_copy(
	    "/Library/Application Support/SyncTERM/syncterm.lst");
	if (support == NULL || paths->cache_directory == NULL
	    || paths->download_directory == NULL
	    || paths->shared_list == NULL) {
		free(support);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	paths->settings_file = st_app_path_join(support, "syncterm.ini");
	paths->personal_list = st_app_path_join(support, "syncterm.lst");
	paths->keys_file = st_app_path_join(support, "syncterm.ssh");
	paths->scripts_directory = st_app_path_join(support, "scripts");
	paths->themes_directory = st_app_path_join(support, "themes");
	free(support);
	if (paths->settings_file == NULL || paths->personal_list == NULL
	    || paths->keys_file == NULL || paths->scripts_directory == NULL
	    || paths->themes_directory == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	return 0;
}
