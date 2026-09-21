/* Windows path-query buffer growth tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "path.h"
#include "path_windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static unsigned int current_directory_calls;
static unsigned int full_path_calls;

static DWORD WINAPI
test_get_current_directory(DWORD size, LPWSTR buffer)
{
	static const wchar_t value[] = L"C:\\long";

	current_directory_calls++;
	if (size == 0)
		return 4;
	if (size < sizeof(value) / sizeof(value[0]))
		return (DWORD)(sizeof(value) / sizeof(value[0]));
	memcpy(buffer, value, sizeof(value));
	return (DWORD)(sizeof(value) / sizeof(value[0]) - 1);
}

static DWORD WINAPI
test_get_full_path_name(LPCWSTR path, DWORD size, LPWSTR buffer,
    LPWSTR *file_part)
{
	static const wchar_t value[] = L"D:\\base";

	(void)path;
	full_path_calls++;
	if (file_part != NULL)
		*file_part = NULL;
	if (size == 0)
		return 4;
	if (size < sizeof(value) / sizeof(value[0]))
		return (DWORD)(sizeof(value) / sizeof(value[0]));
	memcpy(buffer, value, sizeof(value));
	return (DWORD)(sizeof(value) / sizeof(value[0]) - 1);
}

#define GetCurrentDirectoryW test_get_current_directory
#define GetFullPathNameW test_get_full_path_name
#include "../path_windows.c"
#undef GetFullPathNameW
#undef GetCurrentDirectoryW

int
main(void)
{
	char absolute[64];
	wchar_t *extended = NULL;

	CHECK(path_make_absolute(absolute, sizeof(absolute), "child") == 13);
	CHECK(strcmp(absolute, "C:\\long\\child") == 0);
	CHECK(current_directory_calls == 3);

	CHECK(path_make_absolute(absolute, sizeof(absolute), "D:child") == 13);
	CHECK(strcmp(absolute, "D:\\base\\child") == 0);
	CHECK(full_path_calls == 3);

	current_directory_calls = 0;
	CHECK(path_windows_extended(&extended, "child") == 0);
	CHECK(wcscmp(extended, L"\\\\?\\C:\\long\\child") == 0);
	CHECK(current_directory_calls == 3);
	free(extended);
	return 0;
}
