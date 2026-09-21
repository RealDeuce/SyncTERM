/* Windows directory enumeration for the Wren hosts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_directory.h"

#include "io_error.h"
#include "path.h"
#include "path_windows.h"
#include "st_windows_utf.h"

#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int64_t
filetime_seconds(FILETIME value)
{
	const uint64_t epoch = UINT64_C(116444736000000000);
	ULARGE_INTEGER ticks;

	ticks.LowPart = value.dwLowDateTime;
	ticks.HighPart = value.dwHighDateTime;
	if (ticks.QuadPart >= epoch)
		return (int64_t)((ticks.QuadPart - epoch) / UINT64_C(10000000));
	return -(int64_t)((epoch - ticks.QuadPart + UINT64_C(9999999))
	    / UINT64_C(10000000));
}

static int
append_entry(struct st_wren_directory_entries *entries, char *name,
	const WIN32_FIND_DATAW *data)
{
	struct st_wren_directory_entry *item;
	size_t capacity;

	if (entries->count == entries->capacity) {
		capacity = entries->capacity == 0 ? 32 : entries->capacity * 2;
		if (capacity < entries->capacity
		    || capacity > SIZE_MAX / sizeof(entries->items[0]))
			return IO_ERROR_OUT_OF_MEMORY;
		item = realloc(entries->items, capacity * sizeof(entries->items[0]));
		if (item == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		entries->items = item;
		entries->capacity = capacity;
	}
	item = &entries->items[entries->count++];
	*item = (struct st_wren_directory_entry){
		.name = name,
		.directory = (data->dwFileAttributes
		    & FILE_ATTRIBUTE_DIRECTORY) != 0,
		.size = ((uint64_t)data->nFileSizeHigh << 32) | data->nFileSizeLow,
		.modified = filetime_seconds(data->ftLastWriteTime),
	};
	return 0;
}

int
st_wren_directory_enumerate(const char *directory,
	struct st_wren_directory_entries *entries)
{
	wchar_t *extended = NULL;
	wchar_t *pattern = NULL;
	WIN32_FIND_DATAW data;
	HANDLE search;
	size_t length;
	int status;

	if (directory == NULL || entries == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = path_windows_extended(&extended, directory);
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

		if (wcscmp(data.cFileName, L".") == 0
		    || wcscmp(data.cFileName, L"..") == 0)
			continue;
		status = st_windows_utf16_to_utf8(&name, data.cFileName);
		if (status != 0)
			break;
		status = append_entry(entries, name, &data);
		if (status != 0) {
			free(name);
			break;
		}
	} while (FindNextFileW(search, &data));
	if (status == 0 && GetLastError() != ERROR_NO_MORE_FILES)
		status = io_error_from_windows(GetLastError());
	if (!FindClose(search) && status == 0)
		status = io_error_from_windows(GetLastError());
	if (status != 0)
		st_wren_directory_entries_dispose(entries);
	return status;
}
