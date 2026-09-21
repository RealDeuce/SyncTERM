/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "path.h"
#include "path_windows.h"

#include "io_error.h"
#include "path_internal.h"
#include "st_windows_utf.h"

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static bool
ascii_drive_letter(char value)
{
	return (value >= 'A' && value <= 'Z')
	    || (value >= 'a' && value <= 'z');
}

bool
path_is_separator(char character)
{
	return character == '/' || character == '\\';
}

bool
path_is_rooted(const char *path)
{
	if (path == NULL || *path == '\0')
		return false;
	if (path_is_separator(path[0]))
		return true;
	return ascii_drive_letter(path[0]) && path[1] == ':' &&
	    path_is_separator(path[2]);
}

static bool
wide_is_separator(wchar_t character)
{
	return character == L'/' || character == L'\\';
}

static size_t
unc_root_length_utf8(const char *path)
{
	size_t position = 2;

	while (path[position] != '\0' && !path_is_separator(path[position]))
		position++;
	if (path[position] == '\0')
		return position;
	while (path_is_separator(path[position]))
		position++;
	while (path[position] != '\0' && !path_is_separator(path[position]))
		position++;
	if (path_is_separator(path[position]))
		position++;
	return position;
}

size_t
syncterm_path_root_length(const char *path)
{
	if (path_is_separator(path[0]) && path_is_separator(path[1]))
		return unc_root_length_utf8(path);
	if (ascii_drive_letter(path[0]) && path[1] == ':') {
		if (path_is_separator(path[2]))
			return 3;
		return 2;
	}
	return path_is_separator(path[0]) ? 1 : 0;
}

char
syncterm_path_separator(void)
{
	return '\\';
}

static int
grow_wide_buffer(wchar_t **buffer, DWORD *capacity, DWORD required)
{
	DWORD next = required;
	wchar_t *resized;
	size_t bytes;

	if (next <= *capacity) {
		if (*capacity > UINT32_MAX / 2)
			return IO_ERROR_OUT_OF_RANGE;
		next = *capacity * 2;
	}
	bytes = (size_t)next * sizeof(**buffer);
	if (bytes / sizeof(**buffer) != (size_t)next)
		return IO_ERROR_OUT_OF_RANGE;
	resized = realloc(*buffer, bytes);
	if (resized == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*buffer = resized;
	*capacity = next;
	return 0;
}

static int
windows_current_directory(wchar_t **result)
{
	wchar_t *directory = NULL;
	DWORD capacity = 0;
	DWORD required = GetCurrentDirectoryW(0, NULL);

	for (;;) {
		if (required == 0) {
			DWORD native_error = GetLastError();
			free(directory);
			return io_error_from_windows(native_error);
		}
		int error = grow_wide_buffer(&directory, &capacity, required);
		if (error < 0) {
			free(directory);
			return error;
		}
		required = GetCurrentDirectoryW(capacity, directory);
		if (required != 0 && required < capacity) {
			*result = directory;
			return 0;
		}
	}
}

static int
windows_drive_directory(wchar_t drive, wchar_t **result)
{
	wchar_t probe[] = {drive, L':', L'.', L'\0'};
	wchar_t *directory = NULL;
	DWORD capacity = 0;
	DWORD required = GetFullPathNameW(probe, 0, NULL, NULL);

	for (;;) {
		if (required == 0) {
			DWORD native_error = GetLastError();
			free(directory);
			return io_error_from_windows(native_error);
		}
		int error = grow_wide_buffer(&directory, &capacity, required);
		if (error < 0) {
			free(directory);
			return error;
		}
		required = GetFullPathNameW(probe, capacity, directory, NULL);
		if (required != 0 && required < capacity) {
			*result = directory;
			return 0;
		}
	}
}

static size_t
wide_unc_root_length(const wchar_t *path)
{
	size_t position = 2;

	while (path[position] != L'\0' && !wide_is_separator(path[position]))
		position++;
	if (path[position] == L'\0')
		return 0;
	while (wide_is_separator(path[position]))
		position++;
	if (path[position] == L'\0')
		return 0;
	while (path[position] != L'\0' && !wide_is_separator(path[position]))
		position++;
	return position;
}

static int
join_wide(const wchar_t *base, const wchar_t *suffix, bool suffix_rooted,
    wchar_t **result)
{
	size_t base_length = wcslen(base);
	size_t suffix_length = wcslen(suffix);
	bool add_separator = !suffix_rooted && base_length != 0 &&
	    !wide_is_separator(base[base_length - 1]);
	if (base_length > SIZE_MAX - suffix_length - (add_separator ? 2 : 1))
		return IO_ERROR_OUT_OF_RANGE;
	wchar_t *joined = malloc((base_length + suffix_length +
	    (add_separator ? 2 : 1)) * sizeof(*joined));
	if (joined == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(joined, base, base_length * sizeof(*joined));
	size_t used = base_length;
	if (add_separator)
		joined[used++] = L'\\';
	memcpy(joined + used, suffix, (suffix_length + 1) * sizeof(*joined));
	*result = joined;
	return 0;
}

static int
make_raw_absolute(wchar_t *path, wchar_t **result)
{
	for (wchar_t *current = path; *current != L'\0'; current++) {
		if (*current == L'/')
			*current = L'\\';
	}
	if ((wcsncmp(path, L"\\\\?\\", 4) == 0) ||
	    (wcsncmp(path, L"\\\\.\\", 4) == 0) ||
	    (wcsncmp(path, L"\\??\\", 4) == 0))
		return IO_ERROR_INVALID_ARGUMENT;

	if (wide_is_separator(path[0]) && wide_is_separator(path[1])) {
		*result = path;
		return 0;
	}
	if (((path[0] >= L'A' && path[0] <= L'Z') ||
	    (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
	    wide_is_separator(path[2])) {
		*result = path;
		return 0;
	}

	wchar_t *base;
	const wchar_t *suffix = path;
	bool suffix_rooted = false;
	int error;
	if (((path[0] >= L'A' && path[0] <= L'Z') ||
	    (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':') {
		error = windows_drive_directory(path[0], &base);
		suffix = path + 2;
	}
	else {
		error = windows_current_directory(&base);
		if (wide_is_separator(path[0])) {
			suffix_rooted = true;
			if (error == 0 && wide_is_separator(base[0]) &&
			    wide_is_separator(base[1])) {
				size_t root = wide_unc_root_length(base);
				if (root == 0) {
					free(base);
					return IO_ERROR_INVALID_ARGUMENT;
				}
				base[root] = L'\0';
			}
			else if (error == 0 && base[1] == L':')
				base[2] = L'\0';
		}
	}
	if (error < 0)
		return error;
	wchar_t *joined;
	error = join_wide(base, suffix, suffix_rooted, &joined);
	free(base);
	if (error < 0)
		return error;
	free(path);
	*result = joined;
	return 0;
}

static int
normalize_wide_absolute(const wchar_t *path, wchar_t **result)
{
	size_t input_length = wcslen(path);
	if (input_length > SIZE_MAX / sizeof(wchar_t) - 2)
		return IO_ERROR_OUT_OF_RANGE;
	wchar_t *normalized = malloc((input_length + 2) * sizeof(*normalized));
	if (normalized == NULL)
		return IO_ERROR_OUT_OF_MEMORY;

	size_t root_length;
	size_t source;
	if (wide_is_separator(path[0]) && wide_is_separator(path[1])) {
		root_length = wide_unc_root_length(path);
		if (root_length == 0) {
			free(normalized);
			return IO_ERROR_INVALID_ARGUMENT;
		}
		normalized[0] = L'\\';
		normalized[1] = L'\\';
		size_t used = 2;
		for (size_t i = 2; i < root_length; i++) {
			if (wide_is_separator(path[i])) {
				if (used == 0 || normalized[used - 1] != L'\\')
					normalized[used++] = L'\\';
			}
			else
				normalized[used++] = path[i];
		}
		if (used == 0 || normalized[used - 1] != L'\\')
			normalized[used++] = L'\\';
		root_length = used;
		source = wide_unc_root_length(path);
	}
	else if (path[1] == L':' && wide_is_separator(path[2])) {
		normalized[0] = path[0];
		normalized[1] = L':';
		normalized[2] = L'\\';
		root_length = 3;
		source = 3;
	}
	else {
		free(normalized);
		return IO_ERROR_INVALID_ARGUMENT;
	}

	size_t used = root_length;
	while (wide_is_separator(path[source]))
		source++;
	while (path[source] != L'\0') {
		size_t component = source;
		while (path[source] != L'\0' &&
		    !wide_is_separator(path[source]))
			source++;
		size_t length = source - component;
		if (length == 1 && path[component] == L'.') {
			/* Nothing to add. */
		}
		else if (length == 2 && path[component] == L'.' &&
		    path[component + 1] == L'.') {
			if (used > root_length) {
				if (normalized[used - 1] == L'\\')
					used--;
				while (used > root_length &&
				    normalized[used - 1] != L'\\')
					used--;
				if (used > root_length)
					used--;
			}
		}
		else if (length != 0) {
			if (used != 0 && normalized[used - 1] != L'\\')
				normalized[used++] = L'\\';
			memcpy(normalized + used, path + component,
			    length * sizeof(*normalized));
			used += length;
		}
		while (wide_is_separator(path[source]))
			source++;
	}
	normalized[used] = L'\0';
	*result = normalized;
	return 0;
}

static int
path_make_absolute_alloc(char **result, const char *path)
{
	wchar_t *wide;
	wchar_t *absolute;
	wchar_t *normalized;
	char *utf8;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	int error = st_windows_utf8_to_utf16(&wide, path);
	if (error < 0)
		return error;
	error = make_raw_absolute(wide, &absolute);
	if (error < 0) {
		free(wide);
		return error;
	}
	error = normalize_wide_absolute(absolute, &normalized);
	free(absolute);
	if (error < 0)
		return error;
	error = st_windows_utf16_to_utf8(&utf8, normalized);
	free(normalized);
	if (error < 0)
		return error;
	*result = utf8;
	return 0;
}

ptrdiff_t
path_make_absolute(char *result, size_t result_size, const char *path)
{
	char *absolute;
	int error = path_make_absolute_alloc(&absolute, path);

	if (error < 0)
		return error;
	ptrdiff_t stored = syncterm_path_store(result, result_size, absolute,
	    strlen(absolute));
	free(absolute);
	return stored;
}

int
path_windows_extended(wchar_t **result, const char *path)
{
	char *absolute;
	wchar_t *wide;
	wchar_t *extended;
	size_t length;
	size_t prefix_length;
	size_t skip;
	int status;

	if (result == NULL || path == NULL || path[0] == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	status = path_make_absolute_alloc(&absolute, path);
	if (status < 0)
		return status;
	status = st_windows_utf8_to_utf16(&wide, absolute);
	free(absolute);
	if (status < 0)
		return status;
	length = wcslen(wide);
	if (length >= 2 && wide[0] == L'\\' && wide[1] == L'\\') {
		prefix_length = 8;
		skip = 2;
	}
	else {
		prefix_length = 4;
		skip = 0;
	}
	if (length - skip > 32766U - prefix_length) {
		free(wide);
		return IO_ERROR_NAME_TOO_LONG;
	}
	extended = malloc((prefix_length + length - skip + 1)
	    * sizeof(*extended));
	if (extended == NULL) {
		free(wide);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (skip != 0)
		memcpy(extended, L"\\\\?\\UNC\\", prefix_length * sizeof(*extended));
	else
		memcpy(extended, L"\\\\?\\", prefix_length * sizeof(*extended));
	memcpy(extended + prefix_length, wide + skip,
	    (length - skip + 1) * sizeof(*extended));
	free(wide);
	*result = extended;
	return 0;
}

ptrdiff_t
path_expand_user(char *result, size_t result_size, const char *path)
{
	if (path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return syncterm_path_store(result, result_size, path, strlen(path));
}
