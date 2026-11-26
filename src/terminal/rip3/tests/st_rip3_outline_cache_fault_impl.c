/* Factory allocation failure without changing metric/measurement behavior. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

bool   st_rip3_outline_cache_test_fail;
size_t st_rip3_outline_cache_test_allocations;

static void *
cache_allocate(size_t count, size_t size)
{
	st_rip3_outline_cache_test_allocations++;
	return st_rip3_outline_cache_test_fail ? NULL : calloc(count, size);
}

#define calloc cache_allocate
#include "../st_rip3_outline_cache.c"
#undef calloc
