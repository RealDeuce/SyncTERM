/* POSIX persistent advisory file locks. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_file_lock_internal.h"

#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
map_error(int error)
{
	return io_error_from_errno(error);
}

int
st_file_lock_platform_acquire(st_file_lock_t *lock, const char *path,
    bool exclusive)
{
	size_t length;
	char *sidecar;
	int descriptor;

	length = strlen(path);
	if (length > SIZE_MAX - sizeof(".lock"))
		return IO_ERROR_NAME_TOO_LONG;
	sidecar = malloc(length + sizeof(".lock"));
	if (sidecar == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(sidecar, path, length);
	memcpy(sidecar + length, ".lock", sizeof(".lock"));
	descriptor = open(sidecar, O_RDWR | O_CREAT, 0600);
	free(sidecar);
	if (descriptor < 0)
		return map_error(errno);
	while (flock(descriptor, exclusive ? LOCK_EX : LOCK_SH) < 0) {
		if (errno == EINTR)
			continue;
		int error = errno;
		(void)close(descriptor);
		return map_error(error);
	}
	lock->native = descriptor;
	return 0;
}

void
st_file_lock_platform_release(st_file_lock_t *lock)
{
	int descriptor = (int)lock->native;

	(void)flock(descriptor, LOCK_UN);
	(void)close(descriptor);
}
