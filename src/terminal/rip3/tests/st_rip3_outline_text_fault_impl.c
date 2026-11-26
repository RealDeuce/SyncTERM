/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdint.h>
#include <stdlib.h>

size_t st_rip3_outline_test_allocations;
size_t st_rip3_outline_test_fail_at = SIZE_MAX;

static void *
test_malloc(size_t size)
{
	return st_rip3_outline_test_allocations++ == st_rip3_outline_test_fail_at ? NULL : malloc(size);
}

static void *
test_calloc(size_t count, size_t size)
{
	return st_rip3_outline_test_allocations++ == st_rip3_outline_test_fail_at ? NULL : calloc(count, size);
}

static void *
test_realloc(void *value, size_t size)
{
	return st_rip3_outline_test_allocations++ == st_rip3_outline_test_fail_at ? NULL : realloc(value, size);
}

#define malloc  test_malloc
#define calloc  test_calloc
#define realloc test_realloc
#include "../st_rip3_outline_text.c"
