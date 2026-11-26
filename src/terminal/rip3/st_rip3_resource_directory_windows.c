/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "io_error.h"
#include "path_windows.h"
#include "st_rip3_resources_internal.h"
#include "st_windows_utf.h"

int
st_rip3_resource_directory_scan(struct st_rip3_resource_match *match)
{
	wchar_t         *extended = NULL, *pattern = NULL;
	WIN32_FIND_DATAW data;
	HANDLE           search;
	size_t           size;
	int              status = path_windows_extended(&extended, match->directory);

	if (status != 0)
		return status;
	size = wcslen(extended);
	if (size > SIZE_MAX / sizeof(*pattern) - 3) {
		free(extended);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	pattern = malloc((size + 3) * sizeof(*pattern));
	if (pattern == NULL) {
		free(extended);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	memcpy(pattern, extended, size * sizeof(*pattern));
	free(extended);
	if (size != 0 && pattern[size - 1] != L'\\')
		pattern[size++] = L'\\';
	pattern[size++] = L'*';
	pattern[size]   = 0;
	search          = FindFirstFileW(pattern, &data);
	free(pattern);
	if (search == INVALID_HANDLE_VALUE)
		return io_error_from_windows(GetLastError());
	do {
		char *name = NULL;

		status = st_windows_utf16_to_utf8(&name, data.cFileName);
		/* Names with unpaired surrogates are outside the UTF-8 namespace. */
		if (status == IO_ERROR_INVALID_ENCODING) {
			status = 0;
			continue;
		}
		if (status == 0)
			status = st_rip3_resource_consider(match, name);
		free(name);
		if (status != 0)
			break;
	} while (FindNextFileW(search, &data));
	if (status == 0 && GetLastError() != ERROR_NO_MORE_FILES)
		status = io_error_from_windows(GetLastError());
	if (!FindClose(search) && status == 0)
		status = io_error_from_windows(GetLastError());
	return status;
}
