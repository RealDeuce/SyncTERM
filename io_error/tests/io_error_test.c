/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "io_error.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", \
    __FILE__, __LINE__); return EXIT_FAILURE; } } while (0)

int
main(void)
{
	CHECK(io_error_from_errno(0) == 0);
	CHECK(io_error_from_errno(EINVAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(io_error_from_errno(ENOENT) == IO_ERROR_NOT_FOUND);
	CHECK(io_error_from_errno(EACCES) == IO_ERROR_PERMISSION_DENIED);
	CHECK(io_error_from_errno(ENOMEM) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(io_error_from_errno(EINTR) == IO_ERROR_INTERRUPTED);
	CHECK(io_error_from_errno(-12345) == IO_ERROR_IO);
	return EXIT_SUCCESS;
}
