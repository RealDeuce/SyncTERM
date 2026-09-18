/* Windows persistent advisory file locks. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_file_lock_internal.h"

#include "io_error.h"
#include "path_windows.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

static int
map_error(DWORD error)
{
	return io_error_from_windows(error);
}

int
st_file_lock_platform_acquire(st_file_lock_t *lock, const char *path,
    bool exclusive)
{
	OVERLAPPED overlap = {0};
	wchar_t *wide;
	char *sidecar;
	size_t length;
	HANDLE handle;
	DWORD flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
	int status;

	length = strlen(path);
	if (length > SIZE_MAX - sizeof(".lock"))
		return IO_ERROR_NAME_TOO_LONG;
	sidecar = malloc(length + sizeof(".lock"));
	if (sidecar == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(sidecar, path, length);
	memcpy(sidecar + length, ".lock", sizeof(".lock"));
	status = path_windows_extended(&wide, sidecar);
	free(sidecar);
	if (status < 0)
		return status;
	handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	free(wide);
	if (handle == INVALID_HANDLE_VALUE)
		return map_error(GetLastError());
	if (!LockFileEx(handle, flags, 0, MAXDWORD, MAXDWORD, &overlap)) {
		status = map_error(GetLastError());
		(void)CloseHandle(handle);
		return status;
	}
	lock->native = (intptr_t)handle;
	return 0;
}

void
st_file_lock_platform_release(st_file_lock_t *lock)
{
	OVERLAPPED overlap = {0};
	HANDLE handle = (HANDLE)lock->native;

	(void)UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlap);
	(void)CloseHandle(handle);
}
