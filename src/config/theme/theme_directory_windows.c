/* Windows theme-directory enumeration. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "theme_internal.h"

#include "io_error.h"
#include "path_windows.h"
#include "st_windows_utf.h"

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void
st_theme_directory_dispose(struct st_theme_directory *directory)
{
	if (directory == NULL)
		return;
	for (size_t i = 0; i < directory->count; ++i)
		free(directory->entries[i].name);
	free(directory->entries);
	*directory = (struct st_theme_directory){0};
}

static int
append_entry(struct st_theme_directory *result, char *name)
{
	if (result->count == SIZE_MAX / sizeof(result->entries[0]))
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_theme_directory_entry *entries = realloc(result->entries,
	    (result->count + 1) * sizeof(result->entries[0]));
	if (entries == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	result->entries = entries;
	entries[result->count++].name = name;
	return 0;
}

int
st_theme_directory_read(struct st_theme_directory *result, const char *path)
{
	struct st_theme_directory value = {0};
	wchar_t *extended = NULL;
	wchar_t *pattern = NULL;
	WIN32_FIND_DATAW data;
	HANDLE search;
	size_t length;
	int status;

	if (result == NULL || path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = path_windows_extended(&extended, path);
	if (status != 0)
		return status;
	length = wcslen(extended);
	if (length > SIZE_MAX / sizeof(*pattern) - 3) {
		free(extended);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	pattern = malloc((length + 3) * sizeof(*pattern));
	if (pattern == NULL) {
		free(extended);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	memcpy(pattern, extended, length * sizeof(*pattern));
	if (length != 0 && pattern[length - 1] != L'\\')
		pattern[length++] = L'\\';
	pattern[length++] = L'*';
	pattern[length] = L'\0';
	free(extended);
	search = FindFirstFileW(pattern, &data);
	free(pattern);
	if (search == INVALID_HANDLE_VALUE)
		return io_error_from_windows(GetLastError());
	status = 0;
	do {
		char *name = NULL;

		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;
		status = st_windows_utf16_to_utf8(&name, data.cFileName);
		if (status != 0)
			break;
		status = append_entry(&value, name);
		if (status != 0) {
			free(name);
			break;
		}
	} while (FindNextFileW(search, &data));
	if (status == 0 && GetLastError() != ERROR_NO_MORE_FILES)
		status = io_error_from_windows(GetLastError());
	FindClose(search);
	if (status != 0) {
		st_theme_directory_dispose(&value);
		return status;
	}
	st_theme_directory_dispose(result);
	*result = value;
	return 0;
}
