/* Test-only allocation failure control for the BBS list implementation. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_test_allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t attempt;
static size_t failure = SIZE_MAX;

static bool
should_fail(void)
{
	return attempt++ == failure;
}

void
st_bbs_test_fail_after(size_t allocation)
{
	attempt = 0;
	failure = allocation;
}

void
st_bbs_test_disable_failure(void)
{
	attempt = 0;
	failure = SIZE_MAX;
}

void *
st_bbs_test_calloc(size_t count, size_t size)
{
	return should_fail() ? NULL : calloc(count, size);
}

char *
st_bbs_test_strdup(const char *string)
{
	size_t length = strlen(string);
	char *copy;

	if (should_fail() || length == SIZE_MAX)
		return NULL;
	copy = malloc(length + 1);
	if (copy != NULL)
		memcpy(copy, string, length + 1);
	return copy;
}
