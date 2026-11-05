/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "amiga_directory_internal.h"
#include "io_error.h"
#include "path.h"
#include "path_windows.h"
#include "st_windows_utf.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
int amiga_directory_names(struct amiga_names *result, const char *path)
{
	struct amiga_names names = {0};
	char *pattern = NULL;
	wchar_t *wide = NULL;
	int status = path_join_alloc(&pattern, path, "*");
	if (!status)
		status = path_windows_extended(&wide, pattern);
	free(pattern);
	if (status)
		return status;
	WIN32_FIND_DATAW data;
	HANDLE find = FindFirstFileW(wide, &data);
	free(wide);
	if (find == INVALID_HANDLE_VALUE)
		return io_error_from_windows(GetLastError());
	do {
		char *name = NULL;
		status = st_windows_utf16_to_utf8(&name, data.cFileName);
		if (!status && strcmp(name, ".") && strcmp(name, ".."))
			status = amiga_names_append(&names, name);
		free(name);
		if (status)
			break;
	} while (FindNextFileW(find, &data));
	if (!status && GetLastError() != ERROR_NO_MORE_FILES)
		status = io_error_from_windows(GetLastError());
	if (!FindClose(find) && !status)
		status = io_error_from_windows(GetLastError());
	if (status)
		amiga_names_dispose(&names);
	else
		*result = names;
	return status;
}
