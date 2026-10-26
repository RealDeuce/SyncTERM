/* Native Haiku application paths. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths_internal.h"

#include "io_error.h"

#include <FindDirectory.h>
#include <fs_info.h>

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

static char *
haiku_directory(directory_which which, dev_t device)
{
	char path[B_PATH_NAME_LENGTH];

	if (find_directory(which, device, false, path, sizeof(path)) != B_OK)
		return NULL;
	return st_app_path_copy(path);
}

int
st_app_paths_platform_init(struct st_app_paths *paths)
{
	dev_t device = dev_for_path("/boot");
	char *user_settings;
	char *system_settings;
	char *user_cache;
	char *desktop;
	char *personal_base = NULL;
	char *shared_base = NULL;
	int status = IO_ERROR_OUT_OF_MEMORY;

	if (device < 0)
		return IO_ERROR_NOT_FOUND;
	user_settings = haiku_directory(B_USER_SETTINGS_DIRECTORY, device);
	system_settings = haiku_directory(B_SYSTEM_SETTINGS_DIRECTORY, device);
	user_cache = haiku_directory(B_USER_CACHE_DIRECTORY, device);
	desktop = haiku_directory(B_DESKTOP_DIRECTORY, device);
	if (user_settings == NULL || system_settings == NULL || user_cache == NULL
	    || desktop == NULL)
		goto done;
	personal_base = st_app_path_join(user_settings, "SyncTERM");
	shared_base = st_app_path_join(system_settings, "SyncTERM");
	paths->cache_directory = st_app_path_join(user_cache, "SyncTERM");
	paths->download_directory = st_app_path_copy(desktop);
	if (personal_base == NULL || shared_base == NULL
	    || paths->cache_directory == NULL || paths->download_directory == NULL)
		goto done;
	paths->settings_file = st_app_path_join(personal_base, "SyncTERM.ini");
	paths->personal_list = st_app_path_join(personal_base, "SyncTERM.lst");
	paths->keys_file = st_app_path_join(personal_base, "SyncTERM.ssh");
	paths->scripts_directory = st_app_path_join(personal_base, "scripts");
	paths->themes_directory = st_app_path_join(personal_base, "themes");
	paths->shared_list = st_app_path_join(shared_base, "SyncTERM.lst");
	if (paths->settings_file == NULL || paths->personal_list == NULL
	    || paths->shared_list == NULL || paths->keys_file == NULL
	    || paths->scripts_directory == NULL || paths->themes_directory == NULL)
		goto done;
	status = 0;

done:
	free(user_settings);
	free(system_settings);
	free(user_cache);
	free(desktop);
	free(personal_base);
	free(shared_base);
	return status;
}
