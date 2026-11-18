/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdlib.h>
unsigned st_rip3_bezier_test_malloc_countdown;
static void *test_malloc(size_t size)
{
	if (st_rip3_bezier_test_malloc_countdown!=0 && --st_rip3_bezier_test_malloc_countdown==0) return NULL;
	return malloc(size);
}
#define malloc test_malloc
#include "../st_rip3_bezier.c"
