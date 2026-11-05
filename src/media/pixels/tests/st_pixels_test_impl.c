/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the real implementation with allocation failures, only in tests. */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

size_t st_pixels_test_fail_allocation;
size_t st_pixels_test_allocations;

static bool
fail_allocation(void)
{
	++st_pixels_test_allocations;
	return st_pixels_test_fail_allocation != 0
	    && st_pixels_test_allocations == st_pixels_test_fail_allocation;
}

static void *
test_pixels_malloc(size_t size)
{
	return fail_allocation() ? NULL : malloc(size);
}

static void *
test_pixels_calloc(size_t count, size_t size)
{
	return fail_allocation() ? NULL : calloc(count, size);
}

#define malloc test_pixels_malloc
#define calloc test_pixels_calloc
#include "../st_pixels.c"
