/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "str_list_test_allocator.h"

static size_t attempt;
static size_t failure = SIZE_MAX;
static size_t outstanding;

static bool
str_list_test_should_fail(void)
{
	return attempt++ == failure;
}

void
str_list_test_fail_after(size_t allocation)
{
	attempt = 0;
	failure = allocation;
}

void
str_list_test_disable_failure(void)
{
	attempt = 0;
	failure = SIZE_MAX;
}

size_t
str_list_test_outstanding(void)
{
	return outstanding;
}

void *
str_list_test_malloc(size_t size)
{
	void *pointer;

	if (str_list_test_should_fail())
		return NULL;
	pointer = malloc(size);
	if (pointer != NULL)
		outstanding++;
	return pointer;
}

void *
str_list_test_realloc(void *pointer, size_t size)
{
	bool new_allocation = pointer == NULL;
	void *resized;

	if (str_list_test_should_fail())
		return NULL;
	resized = realloc(pointer, size);
	if (resized != NULL && new_allocation)
		outstanding++;
	return resized;
}

void
str_list_test_free(void *pointer)
{
	if (pointer != NULL) {
		outstanding--;
		free(pointer);
	}
}

char *
str_list_test_strdup(const char *string)
{
	size_t length = strlen(string);
	char *copy;

	if (length == SIZE_MAX)
		return NULL;
	copy = str_list_test_malloc(length + 1);
	if (copy != NULL)
		memcpy(copy, string, length + 1);
	return copy;
}

char *
str_list_test_strndup(const char *string, size_t length)
{
	char *copy;

	if (length == SIZE_MAX)
		return NULL;
	copy = str_list_test_malloc(length + 1);
	if (copy != NULL) {
		memcpy(copy, string, length);
		copy[length] = '\0';
	}
	return copy;
}
