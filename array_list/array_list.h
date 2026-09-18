/* Generic pointer-array storage and typed declaration helpers. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_ARRAY_LIST_H_
#define SYNCTERM_ARRAY_LIST_H_

#include <stdbool.h>
#include <stddef.h>

typedef void **array_list_t;
typedef int (*array_list_compare_fn)(const void *left, const void *right);

enum array_list_insert_result {
	ARRAY_LIST_INSERT_ERROR = -1,
	ARRAY_LIST_INSERT_DUPLICATE = 0,
	ARRAY_LIST_INSERTED = 1,
};

/* NULL is an empty list. The core owns only its pointer-array storage. */
size_t array_list_count(array_list_t list);
bool array_list_reserve(array_list_t *list, size_t count);
bool array_list_insert(array_list_t *list, size_t index, void *item);
bool array_list_append(array_list_t *list, void *item);
void *array_list_take(array_list_t list, size_t index);
void array_list_clear(array_list_t list);
void array_list_free(array_list_t *list);

/* Search results use the first equal item and its insertion position. */
bool array_list_sorted_find(array_list_t list, const void *key,
    array_list_compare_fn compare, size_t *index);
bool array_list_sorted_insert(array_list_t *list, void *item,
    array_list_compare_fn compare);
enum array_list_insert_result array_list_sorted_insert_unique(
    array_list_t *list, void *item, array_list_compare_fn compare,
    size_t *index);

/*
 * These macros deliberately use casts: the compiled implementation operates
 * on void pointers while the generated wrappers retain C type checking at the
 * call site.
 */
#define ARRAY_LIST_DECLARE(prefix, item_type) \
	typedef item_type **prefix##_t; \
	size_t prefix##_count(prefix##_t list); \
	bool prefix##_reserve(prefix##_t *list, size_t count); \
	bool prefix##_insert(prefix##_t *list, size_t index, item_type *item); \
	bool prefix##_append(prefix##_t *list, item_type *item); \
	item_type *prefix##_take(prefix##_t list, size_t index); \
	void prefix##_clear(prefix##_t list); \
	void prefix##_free_storage(prefix##_t *list);

#define ARRAY_LIST_DEFINE(prefix, item_type) \
	size_t prefix##_count(prefix##_t list) \
	{ return array_list_count((array_list_t)list); } \
	bool prefix##_reserve(prefix##_t *list, size_t count) \
	{ return array_list_reserve((array_list_t *)list, count); } \
	bool prefix##_insert(prefix##_t *list, size_t index, item_type *item) \
	{ return array_list_insert((array_list_t *)list, index, item); } \
	bool prefix##_append(prefix##_t *list, item_type *item) \
	{ return array_list_append((array_list_t *)list, item); } \
	item_type *prefix##_take(prefix##_t list, size_t index) \
	{ return (item_type *)array_list_take((array_list_t)list, index); } \
	void prefix##_clear(prefix##_t list) \
	{ array_list_clear((array_list_t)list); } \
	void prefix##_free_storage(prefix##_t *list) \
	{ array_list_free((array_list_t *)list); }

#define SORTED_ARRAY_LIST_DECLARE(prefix, item_type, comparator) \
	ARRAY_LIST_DECLARE(prefix, item_type) \
	bool prefix##_find(prefix##_t list, const item_type *key, size_t *index); \
	bool prefix##_insert_sorted(prefix##_t *list, item_type *item); \
	enum array_list_insert_result prefix##_insert_unique(prefix##_t *list, \
	    item_type *item, size_t *index);

#define SORTED_ARRAY_LIST_DEFINE(prefix, item_type, comparator) \
	ARRAY_LIST_DEFINE(prefix, item_type) \
	static int prefix##_compare_adapter(const void *left, const void *right) \
	{ return comparator((const item_type *)left, (const item_type *)right); } \
	bool prefix##_find(prefix##_t list, const item_type *key, size_t *index) \
	{ return array_list_sorted_find((array_list_t)list, key, \
	    prefix##_compare_adapter, index); } \
	bool prefix##_insert_sorted(prefix##_t *list, item_type *item) \
	{ return array_list_sorted_insert((array_list_t *)list, item, \
	    prefix##_compare_adapter); } \
	enum array_list_insert_result prefix##_insert_unique(prefix##_t *list, \
	    item_type *item, size_t *index) \
	{ return array_list_sorted_insert_unique((array_list_t *)list, item, \
	    prefix##_compare_adapter, index); }

#endif
