/* Windows BBS cache-directory maintenance. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path_windows.h"
#include "st_windows_utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

int
st_bbs_cache_remove_tree(const char *path)
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

int
st_bbs_cache_visit(const char *path,
    int (*visit)(const char *key, void *context), void *context)
{
	wchar_t *root = NULL, *pattern;
	WIN32_FIND_DATAW entry;
	HANDLE search;
	int status = wide_path(path, &root);

	if (status < 0)
		return status;
	pattern = wide_join(root, L"*");
	free(root);
	if (pattern == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	search = FindFirstFileW(pattern, &entry);
	free(pattern);
	if (search == INVALID_HANDLE_VALUE) {
		status = windows_error(GetLastError());
		return status == IO_ERROR_NOT_FOUND ? 0 : status;
	}
	for (;;) {
		char *name = utf8_name(entry.cFileName);
		status = name == NULL ? IO_ERROR_INVALID_ENCODING : visit(name, context);
		free(name);
		if (status < 0) break;
		if (!FindNextFileW(search, &entry)) {
			DWORD error = GetLastError();
			if (error != ERROR_NO_MORE_FILES) status = windows_error(error);
			break;
		}
	}
	if (!FindClose(search) && status == 0)
		status = windows_error(GetLastError());
	return status;
}
