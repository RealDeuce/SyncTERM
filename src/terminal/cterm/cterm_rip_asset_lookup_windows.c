/* Windows resolves file names case-insensitively without an extra scan. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_asset_lookup.h"

#include "st_windows_utf.h"

#include <stddef.h>
#include <stdlib.h>
#include <windows.h>

char *
cterm_rip_asset_case_path(const char *directory, const char *name)
{
	(void)directory;
	(void)name;
	return NULL;
}

bool
cterm_rip_asset_path_readable(const char *path)
{
	wchar_t *wide = NULL;
	HANDLE handle;

	if (st_windows_utf8_to_utf16(&wide, path) != 0)
		return false;
	handle = CreateFileW(wide, FILE_READ_DATA,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(wide);
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(handle);
	return true;
}
