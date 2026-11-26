/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>

#include "filesystem.h"
#include "io_error.h"

bool st_rip3_resource_test_fail_replace;

static int
replace_file(const char *path, const void *bytes, size_t size)
{
	if (st_rip3_resource_test_fail_replace) {
		st_rip3_resource_test_fail_replace = false;
		return IO_ERROR_IO;
	}
	return fs_replace_file(path, bytes, size);
}

#define fs_replace_file replace_file
#include "../st_rip3_resources.c"
