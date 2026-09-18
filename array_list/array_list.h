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

/*
 * NULL is an empty list. Every non-NULL handle must come from this library;
 * an ordinary NULL-terminated C array is not a list. The core owns only the
 * pointer-array storage, never its items. Items must be non-NULL object
 * pointers, and list[count] is a maintained NULL sentinel. Do not resize or
 * free the array directly. Replacing an existing item slot with a non-NULL
 * pointer is allowed; callers retain responsibility for item ownership and
 * any sorted-order invariant.
 *
 * reserve/insert/append can relocate the array: copied handles and pointers
 * to slots must not be retained across these calls. Item objects are not
 * moved. There is no internal synchronization; callers coordinate mutation
 * with every access to the same list.
 */
size_t array_list_count(array_list_t list);
/* Reserve at least count item slots, without changing the item count.
 * Reserving zero succeeds without allocating. A NULL handle address fails.
 * All bool mutations leave the list unchanged on failure. */
bool array_list_reserve(array_list_t *list, size_t count);
/* Insert before index (0 through count inclusive); append inserts at count.
 * NULL items, NULL handle addresses and out-of-range indices fail. */
bool array_list_insert(array_list_t *list, size_t index, void *item);
bool array_list_append(array_list_t *list, void *item);
/* Remove and return the indexed item, or NULL for an invalid index.
 * Compacts slots but does not reallocate storage or free the item. */
void *array_list_take(array_list_t list, size_t index);
/* Drop all items without freeing them; retain allocated storage for reuse. */
void array_list_clear(array_list_t list);
/* Free only the storage and set the handle to NULL. NULL arguments are OK. */
void array_list_free(array_list_t *list);

/*
 * Sorted operations require ascending order under compare. The comparator
 * receives item/key pointers themselves, not addresses of pointer slots.
 * It must return negative/zero/positive for less/equal/greater, consistently,
 * and must not mutate the list. Search writes the first equal item's index
 * or the insertion position before the first greater item when not found.
 * NULL key/compare returns false and writes zero. index may be NULL.
 */
bool array_list_sorted_find(array_list_t list, const void *key,
    array_list_compare_fn compare, size_t *index);
/* Insert after existing equal items, preserving their order. */
bool array_list_sorted_insert(array_list_t *list, void *item,
    array_list_compare_fn compare);
/* A duplicate leaves the list untouched and reports the first equal index;
 * success reports the inserted index. Errors leave list and index unchanged.
 * This never frees item, including on duplicate/error. index may be NULL. */
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
