/* Generic pointer-array storage and sorted operations. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "array_list.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct array_list_storage {
	size_t count;
	size_t capacity;
	void *items[];
};

static struct array_list_storage *
array_list_storage(array_list_t list)
{
	return (struct array_list_storage *)((unsigned char *)list
	    - offsetof(struct array_list_storage, items));
}

static bool
array_list_allocation_size(size_t capacity, size_t *size)
{
	const size_t header = offsetof(struct array_list_storage, items);
	const size_t maximum = (SIZE_MAX - header) / sizeof(void *);

	if (maximum == 0 || capacity > maximum - 1)
		return false;
	*size = header + (capacity + 1) * sizeof(void *);
	return true;
}

size_t
array_list_count(array_list_t list)
{
	return list == NULL ? 0 : array_list_storage(list)->count;
}

bool
array_list_reserve(array_list_t *list, size_t required)
{
	struct array_list_storage *storage;
	size_t allocation_size;
	size_t capacity;
	size_t maximum;

	if (list == NULL)
		return false;
	if (required == 0)
		return true;
	maximum = (SIZE_MAX - offsetof(struct array_list_storage, items))
	    / sizeof(void *);
	if (maximum == 0 || required > maximum - 1)
		return false;
	maximum--;
	if (*list == NULL)
		capacity = 4;
	else {
		storage = array_list_storage(*list);
		if (required <= storage->capacity)
			return true;
		capacity = storage->capacity < 4 ? 4 : storage->capacity;
	}
	while (capacity < required) {
		if (capacity > maximum / 2) {
			capacity = required;
			break;
		}
		capacity *= 2;
	}
	if (!array_list_allocation_size(capacity, &allocation_size))
		return false;
	if (*list == NULL)
		storage = malloc(allocation_size);
	else
		storage = realloc(array_list_storage(*list), allocation_size);
	if (storage == NULL)
		return false;
	if (*list == NULL) {
		storage->count = 0;
		storage->items[0] = NULL;
	}
	storage->capacity = capacity;
	*list = storage->items;
	return true;
}

bool
array_list_insert(array_list_t *list, size_t index, void *item)
{
	struct array_list_storage *storage;
	size_t count;

	if (list == NULL || item == NULL)
		return false;
	count = array_list_count(*list);
	if (index > count || count == SIZE_MAX
	    || !array_list_reserve(list, count + 1))
		return false;
	storage = array_list_storage(*list);
	memmove(&storage->items[index + 1], &storage->items[index],
	    (count - index + 1) * sizeof(storage->items[0]));
	storage->items[index] = item;
	storage->count++;
	return true;
}

bool
array_list_append(array_list_t *list, void *item)
{
	return array_list_insert(list, array_list_count(list == NULL ? NULL : *list),
	    item);
}

void *
array_list_take(array_list_t list, size_t index)
{
	struct array_list_storage *storage;
	void *item;

	if (list == NULL)
		return NULL;
	storage = array_list_storage(list);
	if (index >= storage->count)
		return NULL;
	item = storage->items[index];
	memmove(&storage->items[index], &storage->items[index + 1],
	    (storage->count - index) * sizeof(storage->items[0]));
	storage->count--;
	return item;
}

void
array_list_clear(array_list_t list)
{
	struct array_list_storage *storage;

	if (list == NULL)
		return;
	storage = array_list_storage(list);
	storage->count = 0;
	storage->items[0] = NULL;
}

void
array_list_free(array_list_t *list)
{
	if (list == NULL || *list == NULL)
		return;
	free(array_list_storage(*list));
	*list = NULL;
}

static size_t
array_list_bound(array_list_t list, const void *key,
    array_list_compare_fn compare, bool upper)
{
	size_t low = 0;
	size_t high = array_list_count(list);

	while (low < high) {
		size_t middle = low + (high - low) / 2;
		int relation = compare(list[middle], key);

		if (relation < 0 || (upper && relation == 0))
			low = middle + 1;
		else
			high = middle;
	}
	return low;
}

bool
array_list_sorted_find(array_list_t list, const void *key,
    array_list_compare_fn compare, size_t *index)
{
	size_t position;

	if (key == NULL || compare == NULL) {
		if (index != NULL)
			*index = 0;
		return false;
	}
	position = array_list_bound(list, key, compare, false);
	if (index != NULL)
		*index = position;
	return position < array_list_count(list)
	    && compare(list[position], key) == 0;
}

bool
array_list_sorted_insert(array_list_t *list, void *item,
    array_list_compare_fn compare)
{
	size_t position;

	if (list == NULL || item == NULL || compare == NULL)
		return false;
	position = array_list_bound(*list, item, compare, true);
	return array_list_insert(list, position, item);
}

enum array_list_insert_result
array_list_sorted_insert_unique(array_list_t *list, void *item,
    array_list_compare_fn compare, size_t *index)
{
	size_t position;

	if (list == NULL || item == NULL || compare == NULL)
		return ARRAY_LIST_INSERT_ERROR;
	if (array_list_sorted_find(*list, item, compare, &position)) {
		if (index != NULL)
			*index = position;
		return ARRAY_LIST_INSERT_DUPLICATE;
	}
	if (!array_list_insert(list, position, item))
		return ARRAY_LIST_INSERT_ERROR;
	if (index != NULL)
		*index = position;
	return ARRAY_LIST_INSERTED;
}
