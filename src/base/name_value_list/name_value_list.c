/* Owning name/value lists and non-owning sorted indexes. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "name_value_list.h"

#include "io_error.h"
#include "string_compat.h"
#include "string_util.h"
#include "st_unicode_compare.h"

#include <stdlib.h>
#include <string.h>

ARRAY_LIST_DEFINE(name_value_array, struct name_value)
SORTED_ARRAY_LIST_DEFINE(name_value_index, struct name_value,
    name_value_compare)

static int
compare_keys(const char *left, size_t left_size,
	const char *right, size_t right_size)
{
	size_t shared = left_size < right_size ? left_size : right_size;
	int compared = shared == 0 ? 0 : memcmp(left, right, shared);

	if (compared != 0)
		return compared < 0 ? -1 : 1;
	return left_size == right_size ? 0 : left_size < right_size ? -1 : 1;
}

int
name_value_compare(const struct name_value *left,
    const struct name_value *right)
{
	int compared;

	if (left->name_key != NULL && right->name_key != NULL)
		return compare_keys(left->name_key, left->name_key_size,
		    right->name_key, right->name_key_size);
	return st_unicode_caseless_compare(&compared, left->name,
	    strlen(left->name), right->name, strlen(right->name)) == 0
	    ? compared : strcmp(left->name, right->name);
}

struct name_value *
name_value_create(const char *name, const char *value)
{
	struct name_value *item;
	char *name_copy;
	char *value_copy;
	int status;

	if (name == NULL || value == NULL)
		return NULL;
	name_copy = strdup(name);
	if (name_copy == NULL)
		return NULL;
	value_copy = strdup(value);
	if (value_copy == NULL) {
		free(name_copy);
		return NULL;
	}
	item = malloc(sizeof(*item));
	if (item == NULL) {
		free(value_copy);
		free(name_copy);
		return NULL;
	}
	item->name = name_copy;
	item->value = value_copy;
	item->name_key = NULL;
	item->name_key_size = 0;
	status = st_unicode_caseless_key(&item->name_key,
	    &item->name_key_size, name_copy, strlen(name_copy));
	if (status != 0 && status != IO_ERROR_INVALID_ENCODING) {
		free(item);
		free(value_copy);
		free(name_copy);
		return NULL;
	}
	return item;
}

bool
name_value_set_value(struct name_value *item, const char *value)
{
	char *copy;

	if (item == NULL || value == NULL)
		return false;
	copy = strdup(value);
	if (copy == NULL)
		return false;
	free((char *)item->value);
	item->value = copy;
	return true;
}

void
name_value_free_item(struct name_value *item)
{
	if (item == NULL)
		return;
	free((char *)item->name);
	free((char *)item->value);
	free(item->name_key);
	free(item);
}

size_t
name_value_list_count(name_value_list_t list)
{
	return name_value_array_count(list);
}

bool
name_value_list_append(name_value_list_t *list, const char *name,
    const char *value)
{
	struct name_value *item = name_value_create(name, value);

	if (item == NULL)
		return false;
	if (!name_value_array_append(list, item)) {
		name_value_free_item(item);
		return false;
	}
	return true;
}

bool
name_value_list_add_sorted(name_value_list_t *list, const char *name,
    const char *value)
{
	struct name_value *item = name_value_create(name, value);

	if (item == NULL)
		return false;
	if (!name_value_index_insert_sorted(list, item)) {
		name_value_free_item(item);
		return false;
	}
	return true;
}

enum array_list_insert_result
name_value_list_add_unique(name_value_list_t *list, const char *name,
    const char *value, size_t *index)
{
	struct name_value *item = name_value_create(name, value);
	enum array_list_insert_result result;

	if (item == NULL)
		return ARRAY_LIST_INSERT_ERROR;
	result = name_value_index_insert_unique(list, item, index);
	if (result != ARRAY_LIST_INSERTED)
		name_value_free_item(item);
	return result;
}

struct name_value *
name_value_list_take(name_value_list_t list, size_t index)
{
	return name_value_array_take(list, index);
}

bool
name_value_list_remove(name_value_list_t list, size_t index)
{
	struct name_value *item = name_value_list_take(list, index);

	if (item == NULL)
		return false;
	name_value_free_item(item);
	return true;
}

void
name_value_list_free(name_value_list_t *list)
{
	if (list == NULL)
		return;
	for (size_t i = 0; i < name_value_list_count(*list); ++i)
		name_value_free_item((*list)[i]);
	name_value_array_free_storage(list);
}
