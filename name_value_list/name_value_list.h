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

struct name_value {
	const char *name;
	const char *value;
};

int name_value_compare(const struct name_value *left,
    const struct name_value *right);

ARRAY_LIST_DECLARE(name_value_array, struct name_value)
SORTED_ARRAY_LIST_DECLARE(name_value_index, struct name_value,
    name_value_compare)

typedef name_value_array_t name_value_list_t;

struct name_value *name_value_create(const char *name, const char *value);
bool name_value_set_value(struct name_value *item, const char *value);
void name_value_free_item(struct name_value *item);

size_t name_value_list_count(name_value_list_t list);
bool name_value_list_append(name_value_list_t *list, const char *name,
    const char *value);
bool name_value_list_add_sorted(name_value_list_t *list, const char *name,
    const char *value);
enum array_list_insert_result name_value_list_add_unique(
    name_value_list_t *list, const char *name, const char *value,
    size_t *index);
struct name_value *name_value_list_take(name_value_list_t list, size_t index);
bool name_value_list_remove(name_value_list_t list, size_t index);
void name_value_list_free(name_value_list_t *list);

#endif
