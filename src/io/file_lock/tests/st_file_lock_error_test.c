/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Cleanup must not overwrite the failed flock's errno. */
#include "st_file_lock_internal.h"
#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>

static int close_count;

static int
test_open(const char *path, int flags, ...)
{
	(void)path;
	(void)flags;
	return 42;
}

static int
test_flock(int descriptor, int operation)
{
	(void)descriptor;
	(void)operation;
	errno = EACCES;
	return -1;
}

static int
test_close(int descriptor)
{
	(void)descriptor;
	++close_count;
	errno = EIO;
	return -1;
}

#define open test_open
#define flock test_flock
#define close test_close
#include "../st_file_lock_posix.c"

int
main(void)
{
	st_file_lock_t lock = {0};
	int result = st_file_lock_platform_acquire(&lock, "unused", true);
	int expected = io_error_from_errno(EACCES);

	if (result != expected || close_count != 1) {
		fprintf(stderr, "expected lock error %d and one close; got %d and %d\n",
		    expected, result, close_count);
		return 1;
	}
	return 0;
}
