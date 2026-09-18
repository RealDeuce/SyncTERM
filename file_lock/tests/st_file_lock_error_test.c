/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Audit reproduction: cleanup must not overwrite the failed flock's errno.
 * Standalone POSIX test, intentionally failing until the defect is remedied:
 * cc -std=c17 -Wall -Wextra -Werror -Ifile_lock -Iio_error
 *   file_lock/tests/st_file_lock_error_test.c io_error/io_error.c
 *   -o /tmp/st_file_lock_error_test
 */
#include "st_file_lock_internal.h"
#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>

static int close_count;

static int
audit_open(const char *path, int flags, ...)
{
	(void)path;
	(void)flags;
	return 42;
}

static int
audit_flock(int descriptor, int operation)
{
	(void)descriptor;
	(void)operation;
	errno = EACCES;
	return -1;
}

static int
audit_close(int descriptor)
{
	(void)descriptor;
	++close_count;
	errno = EIO;
	return -1;
}

#define open audit_open
#define flock audit_flock
#define close audit_close
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
