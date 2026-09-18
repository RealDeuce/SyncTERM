/* POSIX BBS cache-directory maintenance. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static char *
join_path(const char *directory, const char *name)
{
	char *result = NULL;

	return path_join_alloc(&result, directory, name) == 0 ? result : NULL;
}

static int
remove_tree(const char *path)
{
	struct fs_info info;
	DIR *directory;
	struct dirent *entry;
	int status = fs_get_link_info(&info, path);

	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status < 0)
		return status;
	if (info.kind != FS_ENTRY_DIRECTORY)
		return fs_remove_file(path);
	directory = opendir(path);
	if (directory == NULL)
		return IO_ERROR_IO;
	while ((entry = readdir(directory)) != NULL) {
		char *child;

		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0)
			continue;
		child = join_path(path, entry->d_name);
		if (child == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			break;
		}
		status = remove_tree(child);
		free(child);
		if (status < 0)
			break;
	}
	if (closedir(directory) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status == 0)
		status = fs_remove_directory(path);
	return status;
}

int
st_bbs_cache_rename(st_bbs_list_t *list, const char *old_name,
    const char *new_name)
{
	char *old_path = join_path(list->cache_path, old_name);
	char *new_path = join_path(list->cache_path, new_name);
	struct fs_info info;
	int status;

	if (old_path == NULL || new_path == NULL) {
		free(old_path);
		free(new_path);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = fs_get_link_info(&info, old_path);
	if (status == IO_ERROR_NOT_FOUND)
		status = 0;
	else if (status == 0 && info.kind != FS_ENTRY_DIRECTORY)
		status = IO_ERROR_NOT_DIRECTORY;
	else if (status == 0)
		status = fs_rename(old_path, new_path);
	free(old_path);
	free(new_path);
	return status;
}

int
st_bbs_cache_remove(st_bbs_list_t *list, const char *name)
{
	char *path = join_path(list->cache_path, name);
	int status;

	if (path == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = remove_tree(path);
	free(path);
	return status;
}

static bool
cache_is_current(const st_bbs_list_t *list, const char *name)
{
	for (size_t i = 0; i < st_bbs_list_count(list); ++i) {
		const struct st_bbs_entry *entry = st_bbs_list_at(list, i);

		if (st_bbs_name_equal(entry->profile->name, name))
			return true;
	}
	return false;
}

static bool
cache_is_system(const st_bbs_list_t *list, const char *name)
{
	const char *base = strrchr(list->system_cache_path, '/');
	const char *windows_base = strrchr(list->system_cache_path, '\\');

	if (windows_base != NULL && (base == NULL || windows_base > base))
		base = windows_base;
	return st_bbs_name_equal(base == NULL ? list->system_cache_path : base + 1,
	    name);
}

int
st_bbs_cache_sweep(st_bbs_list_t *list)
{
	DIR *directory = opendir(list->cache_path);
	struct dirent *entry;
	int status = 0;

	if (directory == NULL) {
		struct fs_info info;

		return fs_get_link_info(&info, list->cache_path) == IO_ERROR_NOT_FOUND
		    ? 0 : IO_ERROR_IO;
	}
	while ((entry = readdir(directory)) != NULL) {
		char *path;
		struct fs_info info;

		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0
		    || cache_is_system(list, entry->d_name)
		    || cache_is_current(list, entry->d_name))
			continue;
		path = join_path(list->cache_path, entry->d_name);
		if (path == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			break;
		}
		status = fs_get_link_info(&info, path);
		if (status == 0 && info.kind == FS_ENTRY_DIRECTORY)
			status = remove_tree(path);
		else if (status == 0)
			status = 0;
		free(path);
		if (status < 0)
			break;
	}
	if (closedir(directory) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

int
st_bbs_list_sweep_orphan_caches(st_bbs_list_t *list)
{
	return list == NULL ? IO_ERROR_INVALID_ARGUMENT : st_bbs_cache_sweep(list);
}
