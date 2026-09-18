/* Owning name/value lists and non-owning sorted indexes. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_NAME_VALUE_LIST_H_
#define SYNCTERM_NAME_VALUE_LIST_H_

#include "array_list.h"

#include <stdbool.h>
#include <stddef.h>

/* Library-created items own both strings. Read the fields directly, but do
 * not replace/free them or edit their characters. Use set_value to replace
 * a value. Names remain stable while an item is present in a sorted index. */
struct name_value {
	const char *name;
	const char *value;
};

/* ASCII-only case-insensitive name ordering, independent of locale. Values
 * do not participate. Both items and their names must be non-NULL. A stack
 * name_value containing a borrowed name may be used as a search key. */
int name_value_compare(const struct name_value *left,
    const struct name_value *right);

/* These low-level arrays/indexes own only storage, not the pointed-to items.
 * In particular, free_storage must be used to dispose of a borrowed index.
 * All array_list contracts apply, including handle invalidation on growth. */
ARRAY_LIST_DECLARE(name_value_array, struct name_value)
SORTED_ARRAY_LIST_DECLARE(name_value_index, struct name_value,
    name_value_compare)

/* Owning list interface: NULL is empty; each item must be owned exactly once.
 * Borrowed indexes must be removed/updated before owned items are freed. */
typedef name_value_array_t name_value_list_t;

/* Copy both strings; NULL input or allocation failure returns NULL. */
struct name_value *name_value_create(const char *name, const char *value);
/* Copy before replacing, so value may alias the current value or name.
 * NULL arguments/allocation failure return false and leave item unchanged. */
bool name_value_set_value(struct name_value *item, const char *value);
/* Release a library-created item and its strings. NULL is permitted. */
void name_value_free_item(struct name_value *item);

size_t name_value_list_count(name_value_list_t list);
bool name_value_list_append(name_value_list_t *list, const char *name,
    const char *value);
/* Both sorted additions require an already sorted list under name_value_compare.
 * add_sorted inserts after equal names. add_unique preserves the existing
 * item/value on duplicate and reports its first index. On error the list
 * and index are unchanged; on success index identifies the new item.
 * All additions copy name/value and preserve the list on failure. */
bool name_value_list_add_sorted(name_value_list_t *list, const char *name,
    const char *value);
enum array_list_insert_result name_value_list_add_unique(
    name_value_list_t *list, const char *name, const char *value,
    size_t *index);
/* take transfers item ownership to the caller (free with free_item); remove
 * frees it. Invalid indices yield NULL/false without changing the list.
 * Neither operation reallocates the array. */
struct name_value *name_value_list_take(name_value_list_t list, size_t index);
bool name_value_list_remove(name_value_list_t list, size_t index);
/* Free all owned items and storage in linear time, then set the handle NULL.
 * NULL arguments are permitted. Do not pass a borrowed index to this call. */
void name_value_list_free(name_value_list_t *list);

#endif
