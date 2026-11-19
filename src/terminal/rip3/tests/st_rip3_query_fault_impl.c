/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdlib.h>

unsigned st_rip3_query_test_allocation_countdown;

static void *
test_malloc(size_t size)
{
	if (st_rip3_query_test_allocation_countdown != 0
	    && --st_rip3_query_test_allocation_countdown == 0) return NULL;
	return malloc(size);
}

static void *
test_calloc(size_t count, size_t size)
{
	if (st_rip3_query_test_allocation_countdown != 0
	    && --st_rip3_query_test_allocation_countdown == 0) return NULL;
	return calloc(count,size);
}

static void *
test_realloc(void *old, size_t size)
{
	if (st_rip3_query_test_allocation_countdown!=0
	    && --st_rip3_query_test_allocation_countdown==0) return NULL;
	return realloc(old,size);
}

#define realloc test_realloc
#define malloc test_malloc
#define calloc test_calloc
#include "../st_rip3_query.c"
