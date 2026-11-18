/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

bool st_rip3_path_test_fail_malloc;
unsigned st_rip3_path_test_malloc_countdown;

static void *
test_malloc(size_t size)
{
	if (st_rip3_path_test_fail_malloc) {
		st_rip3_path_test_fail_malloc = false;
		return NULL;
	}
	if (st_rip3_path_test_malloc_countdown != 0 && --st_rip3_path_test_malloc_countdown == 0) return NULL;
	return malloc(size);
}

/* Owned decoded vertices fail locally, without a production allocation hook. */
#define malloc test_malloc
#include "../st_rip3_path_commands.c"
