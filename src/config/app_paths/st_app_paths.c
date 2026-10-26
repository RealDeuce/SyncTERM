/* SyncTERM application path ownership and lazy directory creation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "string_compat.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

char *
st_app_path_copy(const char *path)
{
	return path == NULL ? NULL : strdup(path);
}

char *
st_app_path_join(const char *directory, const char *name)
{
	char *result = NULL;

	return path_join_alloc(&result, directory, name) == 0 ? result : NULL;
}

void
st_app_paths_dispose(struct st_app_paths *paths)
{
	if (paths == NULL)
		return;
	free(paths->settings_file);
	free(paths->personal_list);
	free(paths->shared_list);
	free(paths->cache_directory);
	free(paths->system_cache_directory);
	free(paths->cached_system_list);
	free(paths->download_directory);
	free(paths->keys_file);
	free(paths->scripts_directory);
	free(paths->themes_directory);
	memset(paths, 0, sizeof(*paths));
}

static int
replace_path(char **path, const char *replacement)
{
	char *copy;

	if (replacement == NULL)
		return 0;
	copy = strdup(replacement);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(*path);
	*path = copy;
	return 0;
}

int
st_app_paths_init(struct st_app_paths *paths,
    const struct st_app_path_overrides *overrides)
{
	struct st_app_paths value = {0};
	int status;

	if (paths == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_app_paths_platform_init(&value);
	if (status < 0)
		goto fail;
	if (overrides != NULL) {
		status = replace_path(&value.settings_file,
		    overrides->settings_file);
		if (status == 0)
			status = replace_path(&value.personal_list,
			    overrides->personal_list);
		if (status == 0)
			status = replace_path(&value.shared_list,
			    overrides->shared_list);
		if (status == 0)
			status = replace_path(&value.cache_directory,
			    overrides->cache_directory);
		if (status == 0)
			status = replace_path(&value.download_directory,
			    overrides->download_directory);
		if (status == 0)
			status = replace_path(&value.keys_file,
			    overrides->keys_file);
		if (status == 0)
			status = replace_path(&value.scripts_directory,
			    overrides->scripts_directory);
		if (status == 0)
			status = replace_path(&value.themes_directory,
			    overrides->themes_directory);
		if (status < 0)
			goto fail;
	}
	free(value.system_cache_directory);
	free(value.cached_system_list);
	value.system_cache_directory = st_app_path_join(value.cache_directory,
	    "syncterm-system-cache");
	value.cached_system_list = st_app_path_join(value.system_cache_directory,
	    "System List.lst");
	if (value.settings_file == NULL || value.personal_list == NULL
	    || value.cache_directory == NULL
	    || value.system_cache_directory == NULL
	    || value.cached_system_list == NULL
	    || value.download_directory == NULL || value.keys_file == NULL
	    || value.scripts_directory == NULL || value.themes_directory == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto fail;
	}
	*paths = value;
	return 0;

fail:
	st_app_paths_dispose(&value);
	return status;
}

int
st_app_paths_prepare_settings(const struct st_app_paths *paths)
{
	if (paths == NULL || paths->settings_file == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return fs_create_parent_directories(paths->settings_file);
}

int
st_app_paths_prepare_personal_list(const struct st_app_paths *paths)
{
	if (paths == NULL || paths->personal_list == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return fs_create_parent_directories(paths->personal_list);
}

int
st_app_paths_prepare_cache(const struct st_app_paths *paths)
{
	if (paths == NULL || paths->cache_directory == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return fs_create_directories(paths->cache_directory);
}

int
st_app_paths_prepare_keys(const struct st_app_paths *paths)
{
	if (paths == NULL || paths->keys_file == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return fs_create_parent_directories(paths->keys_file);
}

int
st_app_paths_prepare_scripts(const struct st_app_paths *paths)
{
	if (paths == NULL || paths->scripts_directory == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return fs_create_directories(paths->scripts_directory);
}

int
st_app_paths_prepare_themes(const struct st_app_paths *paths)
{
	if (paths == NULL || paths->themes_directory == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return fs_create_directories(paths->themes_directory);
}
