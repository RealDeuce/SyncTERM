/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
/* Inject only the overlap snapshot allocation failure, with no production hook. */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

bool st_rip3_copy_test_fail_malloc;

static void *
test_malloc(size_t size)
{
	if (st_rip3_copy_test_fail_malloc) {
		st_rip3_copy_test_fail_malloc = false;
		return NULL;
	}
	return malloc(size);
}

#define malloc test_malloc
#include "../st_rip3_surface.c"
