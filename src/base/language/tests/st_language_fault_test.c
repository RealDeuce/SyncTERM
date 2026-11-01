/* Allocation failures must not leave a partially owned discovery result. */
/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_language_internal.h"
#include "io_error.h"
#include "string_compat.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t fail_at, allocations, outstanding;
static bool empty_preferences;

static void *
test_malloc(size_t size)
{
	void *result;

	if (allocations++ == fail_at)
		return NULL;
	result = malloc(size);
	if (result != NULL)
		outstanding++;
	return result;
}

static char *
test_strdup(const char *text)
{
	size_t size = strlen(text) + 1;
	char *result = test_malloc(size);

	if (result != NULL)
		memcpy(result, text, size);
	return result;
}

static void *
test_realloc(void *old, size_t size)
{
	void *result;
	bool was_null = old == NULL;

	if (allocations++ == fail_at)
		return NULL;
	result = realloc(old, size);
	if (result != NULL && was_null)
		outstanding++;
	return result;
}

static void
test_free(void *allocation)
{
	if (allocation != NULL)
		outstanding--;
	free(allocation);
}

#undef strdup
#define strdup test_strdup
#define realloc test_realloc
#define free test_free
#include "../st_language.c"
#undef strdup
#undef realloc
#undef free

int
st_language_platform(struct st_language_preferences *result)
{
	if (empty_preferences)
		return 0;
	return st_language_environment(result, "fr:de", NULL, NULL, "en_US");
}

int
main(void)
{
	for (size_t scenario = 0; scenario < 2; scenario++) {
		size_t allocation_count = scenario == 0 ? 4 : 2;

		empty_preferences = scenario != 0;
		for (fail_at = 0; fail_at <= allocation_count; fail_at++) {
			struct st_language_preferences result;
			int status;

			allocations = 0;
			status = st_language_discover(&result);
			if ((fail_at < allocation_count
			        && (status != IO_ERROR_OUT_OF_MEMORY
			            || result.count != 0 || result.tags != NULL))
			    || (fail_at == allocation_count && (status != 0
			        || result.count != (empty_preferences ? 1u : 2u)))) {
				fprintf(stderr, "allocation failure case %zu\n", fail_at);
				return 1;
			}
			st_language_free(&result);
			if (outstanding != 0)
				return 1;
		}
	}
	return 0;
}
