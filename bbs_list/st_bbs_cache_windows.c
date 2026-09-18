/* Windows BBS cache-directory maintenance. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "path_windows.h"
#include "st_windows_utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static char *
join_path(const char *directory, const char *name)
{
	char *result = NULL;

	return path_join_alloc(&result, directory, name) == 0 ? result : NULL;
}

static int
windows_error(DWORD error)
{
	return io_error_from_windows(error);
}

static int
wide_path(const char *path, wchar_t **result)
{
	return path_windows_extended(result, path);
}

static wchar_t *
wide_join(const wchar_t *directory, const wchar_t *name)
{
	size_t a = wcslen(directory);
	size_t b = wcslen(name);
	wchar_t *result;

	if (a > SIZE_MAX / sizeof(*result) - b - 2)
		return NULL;
	result = malloc((a + b + 2) * sizeof(*result));
	if (result == NULL)
		return NULL;
	memcpy(result, directory, a * sizeof(*result));
	result[a++] = L'\\';
	memcpy(result + a, name, (b + 1) * sizeof(*result));
	return result;
}

static int
remove_tree_wide(const wchar_t *path)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes;
	WIN32_FIND_DATAW entry;
	wchar_t *pattern;
	HANDLE search;
	int status = 0;

	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
		status = windows_error(GetLastError());
		return status == IO_ERROR_NOT_FOUND ? 0 : status;
	}
	if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		return DeleteFileW(path) ? 0 : windows_error(GetLastError());
	if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		return RemoveDirectoryW(path) ? 0 : windows_error(GetLastError());
	pattern = wide_join(path, L"*");
	if (pattern == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	search = FindFirstFileW(pattern, &entry);
	free(pattern);
	if (search == INVALID_HANDLE_VALUE) {
		status = windows_error(GetLastError());
		if (status != IO_ERROR_NOT_FOUND)
			return status;
	} else {
		do {
			wchar_t *child;

			if (wcscmp(entry.cFileName, L".") == 0
			    || wcscmp(entry.cFileName, L"..") == 0)
				continue;
			child = wide_join(path, entry.cFileName);
			if (child == NULL) {
				status = IO_ERROR_OUT_OF_MEMORY;
				break;
			}
			status = remove_tree_wide(child);
			free(child);
		} while (status == 0 && FindNextFileW(search, &entry));
		if (status == 0 && GetLastError() != ERROR_NO_MORE_FILES)
			status = windows_error(GetLastError());
		if (!FindClose(search) && status == 0)
			status = windows_error(GetLastError());
	}
	if (status == 0 && !RemoveDirectoryW(path))
		status = windows_error(GetLastError());
	return status;
}

static int
remove_tree(const char *path)
{
	wchar_t *wide;
	int status = wide_path(path, &wide);

	if (status == 0) {
		status = remove_tree_wide(wide);
		free(wide);
	}
	return status;
}

static char *
utf8_name(const wchar_t *name)
{
	char *result = NULL;

	return st_windows_utf16_to_utf8(&result, name) == 0 ? result : NULL;
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

int
st_bbs_cache_sweep(st_bbs_list_t *list)
{
	wchar_t *root;
	wchar_t *pattern;
	WIN32_FIND_DATAW entry;
	HANDLE search;
	int status = wide_path(list->cache_path, &root);

	if (status < 0)
		return status;
	pattern = wide_join(root, L"*");
	if (pattern == NULL) {
		free(root);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	search = FindFirstFileW(pattern, &entry);
	free(pattern);
	if (search == INVALID_HANDLE_VALUE) {
		status = windows_error(GetLastError());
		free(root);
		return status == IO_ERROR_NOT_FOUND ? 0 : status;
	}
	do {
		char *name;
		wchar_t *child;

		if (wcscmp(entry.cFileName, L".") == 0
		    || wcscmp(entry.cFileName, L"..") == 0
		    || (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
		    || (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			continue;
		name = utf8_name(entry.cFileName);
		if (name == NULL) {
			status = IO_ERROR_INVALID_ENCODING;
			break;
		}
		if (cache_is_system(list, name)
		    || cache_is_current(list, name)) {
			free(name);
			continue;
		}
		free(name);
		child = wide_join(root, entry.cFileName);
		if (child == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			break;
		}
		status = remove_tree_wide(child);
		free(child);
	} while (status == 0 && FindNextFileW(search, &entry));
	if (status == 0 && GetLastError() != ERROR_NO_MORE_FILES)
		status = windows_error(GetLastError());
	if (!FindClose(search) && status == 0)
		status = windows_error(GetLastError());
	free(root);
	return status;
}

int
st_bbs_list_sweep_orphan_caches(st_bbs_list_t *list)
{
	return list == NULL ? IO_ERROR_INVALID_ARGUMENT : st_bbs_cache_sweep(list);
}
