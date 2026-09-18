/* Functions for owning NULL-terminated string lists. */

/****************************************************************************
 * Copyright Rob Swindell - http://www.synchro.net/copyright.html           *
 * Copyright (c) 2026 Stephen Hurd                                          *
 *                                                                          *
 * This library is free software; you can redistribute it and/or            *
 * modify it under the terms of the GNU Lesser General Public License       *
 * as published by the Free Software Foundation; either version 2          *
 * of the License, or (at your option) any later version.                    *
 ****************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array_list.h"
#include "string_compat.h"
#include "str_list.h"

#if defined(STR_LIST_TEST_ALLOCATOR)
	#include "str_list_test_allocator.h"
	#define STR_LIST_MALLOC(size) str_list_test_malloc(size)
	#define STR_LIST_FREE(pointer) str_list_test_free(pointer)
#else
	#define STR_LIST_MALLOC(size) malloc(size)
	#define STR_LIST_FREE(pointer) free(pointer)
#endif

static char *
str_list_duplicate(const char *string)
{
#if defined(STR_LIST_TEST_ALLOCATOR)
	return str_list_test_strdup(string);
#else
	return strdup(string);
#endif
}

static char *
str_list_duplicate_n(const char *string, size_t length)
{
#if defined(STR_LIST_TEST_ALLOCATOR)
	return str_list_test_strndup(string, length);
#else
	return strndup(string, length);
#endif
}

static bool
str_list_append_owned(str_list_t *list, char *string)
{
	if (list == NULL || string == NULL)
		return false;
	return array_list_append((array_list_t *)list, string);
}

void
str_list_free_item(char *item)
{
	STR_LIST_FREE(item);
}

void
str_list_free(str_list_t *list)
{
	if (list == NULL || *list == NULL)
		return;
	for (size_t i = 0; i < str_list_count(*list); i++)
		str_list_free_item((*list)[i]);
	array_list_free((array_list_t *)list);
}

size_t
str_list_count(str_list_t list)
{
	return array_list_count((array_list_t)list);
}

bool
str_list_append(str_list_t *list, const char *string)
{
	char *copy;

	if (list == NULL || string == NULL)
		return false;
	copy = str_list_duplicate(string);
	if (copy == NULL)
		return false;
	if (!str_list_append_owned(list, copy)) {
		STR_LIST_FREE(copy);
		return false;
	}
	return true;
}

bool
str_list_append_list(str_list_t *list, str_list_t source)
{
	size_t destination_count;
	char **copies;
	size_t source_count;
	size_t copy_size;
	size_t i;

	if (list == NULL)
		return false;
	source_count = str_list_count(source);
	if (source_count == 0)
		return true;
	destination_count = str_list_count(*list);
	if (source_count > SIZE_MAX / sizeof(*copies))
		return false;
	copy_size = source_count * sizeof(*copies);
	copies = STR_LIST_MALLOC(copy_size);
	if (copies == NULL)
		return false;
	for (i = 0; i < source_count; i++) {
		copies[i] = str_list_duplicate(source[i]);
		if (copies[i] == NULL)
			break;
	}
	if (i != source_count) {
		while (i > 0)
			STR_LIST_FREE(copies[--i]);
		STR_LIST_FREE(copies);
		return false;
	}
	if (source_count > SIZE_MAX - destination_count
	    || !array_list_reserve((array_list_t *)list,
	    destination_count + source_count)) {
		for (i = 0; i < source_count; i++)
			STR_LIST_FREE(copies[i]);
		STR_LIST_FREE(copies);
		return false;
	}
	for (i = 0; i < source_count; i++)
		(void)array_list_append((array_list_t *)list, copies[i]);
	STR_LIST_FREE(copies);
	return true;
}

bool
str_list_clone(str_list_t *result, str_list_t source)
{
	str_list_t copy = NULL;

	if (result == NULL)
		return false;
	if (!str_list_append_list(&copy, source)) {
		str_list_free(&copy);
		return false;
	}
	str_list_free(result);
	*result = copy;
	return true;
}

bool
str_list_contains(str_list_t list, const char *string)
{
	if (list == NULL || string == NULL)
		return false;
	for (size_t i = 0; i < str_list_count(list); i++) {
		if (strcmp(list[i], string) == 0)
			return true;
	}
	return false;
}

char *
str_list_take(str_list_t list, size_t index)
{
	return array_list_take((array_list_t)list, index);
}

bool
str_list_remove(str_list_t list, size_t index)
{
	char *item = str_list_take(list, index);

	if (item == NULL)
		return false;
	str_list_free_item(item);
	return true;
}

char *
str_list_take_last(str_list_t list)
{
	if (str_list_count(list) == 0)
		return NULL;
	return str_list_take(list, str_list_count(list) - 1);
}

bool
str_list_split(str_list_t *result, const char *string,
    const char *delimiters)
{
	const char *cursor;
	str_list_t list = NULL;

	if (result == NULL || string == NULL || delimiters == NULL)
		return false;
	cursor = string;
	while (*cursor != '\0') {
		size_t length;
		char *item;

		cursor += strspn(cursor, delimiters);
		if (*cursor == '\0')
			break;
		length = strcspn(cursor, delimiters);
		item = str_list_duplicate_n(cursor, length);
		if (item == NULL || !str_list_append_owned(&list, item)) {
			STR_LIST_FREE(item);
			str_list_free(&list);
			return false;
		}
		cursor += length;
	}
	str_list_free(result);
	*result = list;
	return true;
}

char *
str_list_join(str_list_t list, const char *separator)
{
	size_t count;
	size_t separator_length;
	size_t length = 0;
	char *joined;
	char *cursor;

	if (separator == NULL)
		return NULL;
	count = str_list_count(list);
	if (count == 0)
		return str_list_duplicate("");
	separator_length = strlen(separator);
	for (size_t i = 0; i < count; i++) {
		size_t item_length = strlen(list[i]);

		if (item_length > SIZE_MAX - length)
			return NULL;
		length += item_length;
		if (i > 0) {
			if (separator_length > SIZE_MAX - length)
				return NULL;
			length += separator_length;
		}
	}
	if (length == SIZE_MAX)
		return NULL;
	joined = STR_LIST_MALLOC(length + 1);
	if (joined == NULL)
		return NULL;
	cursor = joined;
	for (size_t i = 0; i < count; i++) {
		size_t item_length;

		if (i > 0) {
			memcpy(cursor, separator, separator_length);
			cursor += separator_length;
		}
		item_length = strlen(list[i]);
		memcpy(cursor, list[i], item_length);
		cursor += item_length;
	}
	*cursor = '\0';
	return joined;
}
