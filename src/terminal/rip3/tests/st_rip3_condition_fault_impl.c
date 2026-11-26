/* Deterministic allocation failure below the condition evaluator boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

bool   st_rip3_condition_test_fail;
size_t st_rip3_condition_test_allocations;

static void *
condition_allocate(size_t size)
{
	st_rip3_condition_test_allocations++;
	return st_rip3_condition_test_fail ? NULL : malloc(size);
}

#define malloc condition_allocate
#include "../st_rip3_condition.c"
#undef malloc
