/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>

bool st_rip3_bitmap_encode_test_fail_allocate;

static void *
test_calloc(size_t count, size_t size)
{
	if (st_rip3_bitmap_encode_test_fail_allocate) {
		st_rip3_bitmap_encode_test_fail_allocate = false;
		return NULL;
	}
	return calloc(count, size);
}

#define calloc test_calloc
#include "../st_rip3_bitmap.c"
