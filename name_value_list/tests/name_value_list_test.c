/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "name_value_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #c); return EXIT_FAILURE; } } while (0)

int
main(void)
{
	name_value_list_t list = NULL;
	struct name_value key = {"key", ""};
	struct name_value *taken;
	size_t index;

	CHECK(name_value_list_append(&list, "z", "last"));
	CHECK(name_value_list_append(&list, "a", "first"));
	CHECK(name_value_list_count(list) == 2);
	name_value_list_free(&list);
	CHECK(list == NULL);
	CHECK(name_value_list_add_sorted(&list, "Key", "one"));
	CHECK(name_value_list_add_sorted(&list, "key", "two"));
	CHECK(name_value_index_find(list, &key, &index) && index == 0);
	CHECK(strcmp(list[0]->value, "one") == 0);
	CHECK(strcmp(list[1]->value, "two") == 0);
	CHECK(name_value_list_add_unique(&list, "KEY", "three", &index)
	    == ARRAY_LIST_INSERT_DUPLICATE);
	CHECK(name_value_set_value(list[1], "changed"));
	taken = name_value_list_take(list, 0);
	CHECK(taken != NULL && name_value_list_count(list) == 1);
	name_value_free_item(taken);
	CHECK(name_value_list_remove(list, 0));
	name_value_list_free(&list);
	return EXIT_SUCCESS;
}
