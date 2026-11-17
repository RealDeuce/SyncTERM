/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdlib.h>

unsigned st_rip3_define_test_fail_calloc;
unsigned st_rip3_define_test_allocations;

static void *
test_calloc(size_t count, size_t size)
{
	st_rip3_define_test_allocations++;
	if (st_rip3_define_test_fail_calloc != 0
	    && --st_rip3_define_test_fail_calloc == 0)
		return NULL;
	return calloc(count,size);
}

#define calloc test_calloc
#include "../st_rip3_surface.c"
