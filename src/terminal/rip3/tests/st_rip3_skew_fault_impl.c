/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
bool st_rip3_skew_test_fail_malloc;

static void *
test_malloc(size_t size)
{
	if (st_rip3_skew_test_fail_malloc) {
		st_rip3_skew_test_fail_malloc = false;
		return NULL;
	}
	return malloc(size);
}

#define malloc test_malloc
#include "../st_rip3_skew.c"
