/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
bool   st_rip3_resident_test_fail;
size_t st_rip3_resident_test_attempts, st_rip3_resident_test_live, st_rip3_resident_test_fail_at = SIZE_MAX;

static void *
test_malloc(size_t size)
{
	if (st_rip3_resident_test_attempts++ == st_rip3_resident_test_fail_at || st_rip3_resident_test_fail)
		return NULL;
	void *result = malloc(size);
	if (result != NULL)
		st_rip3_resident_test_live++;
	return result;
}

static void
test_free(void *object)
{
	if (object != NULL)
		st_rip3_resident_test_live--;
	free(object);
}

#define malloc test_malloc
#define free   test_free
#include "../st_rip3_resident_query.c"
