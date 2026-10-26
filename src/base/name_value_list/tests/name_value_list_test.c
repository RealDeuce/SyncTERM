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
	struct name_value key = {.name = "key", .value = ""};
	struct name_value *taken;
	size_t index;
	name_value_index_t borrowed = NULL;
	char name[] = "owned name";
	char value[] = "owned value";

	CHECK(name_value_list_count(NULL) == 0);
	CHECK(name_value_create(NULL, "value") == NULL);
	CHECK(name_value_create("name", NULL) == NULL);
	CHECK(!name_value_list_append(NULL, "name", "value"));
	CHECK(!name_value_list_append(&list, NULL, "value"));
	CHECK(list == NULL);
	CHECK(!name_value_list_remove(NULL, 0));
	CHECK(name_value_list_take(NULL, 0) == NULL);
	index = 123;
	CHECK(name_value_list_add_unique(NULL, "name", "value", &index)
	    == ARRAY_LIST_INSERT_ERROR && index == 123);

	CHECK(name_value_list_append(&list, name, value));
	name[0] = 'x';
	value[0] = 'x';
	CHECK(strcmp(list[0]->name, "owned name") == 0);
	CHECK(strcmp(list[0]->value, "owned value") == 0);
	CHECK(name_value_index_insert_sorted(&borrowed, list[0]));
	CHECK(name_value_set_value(list[0], list[0]->value + 6));
	CHECK(strcmp(borrowed[0]->value, "value") == 0);
	CHECK(name_value_set_value(list[0], list[0]->name));
	CHECK(strcmp(borrowed[0]->value, "owned name") == 0);
	CHECK(!name_value_set_value(list[0], NULL));
	CHECK(strcmp(list[0]->value, "owned name") == 0);
	name_value_index_free_storage(&borrowed);
	CHECK(borrowed == NULL);
	CHECK(strcmp(list[0]->name, "owned name") == 0);
	name_value_list_free(&list);

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
	CHECK(index == 0 && strcmp(list[0]->value, "one") == 0);
	CHECK(name_value_list_count(list) == 2);
	CHECK(name_value_set_value(list[1], "changed"));
	taken = name_value_list_take(list, 0);
	CHECK(taken != NULL && name_value_list_count(list) == 1);
	name_value_free_item(taken);
	CHECK(name_value_list_remove(list, 0));
	CHECK(!name_value_list_remove(list, 0));
	name_value_list_free(&list);
	CHECK(name_value_list_add_unique(&list, "Stra\xc3\x9f" "e", "four",
	    &index) == ARRAY_LIST_INSERTED);
	CHECK(list[0]->name_key_size == 7
	    && memcmp(list[0]->name_key, "strasse", 7) == 0);
	CHECK(name_value_list_add_unique(&list, "STRASSE", "five", &index)
	    == ARRAY_LIST_INSERT_DUPLICATE);
	CHECK(name_value_list_count(list) == 1);
	name_value_list_free(&list);
	/* A large owning list must be freed without repeatedly shifting its tail. */
	for (size_t i = 0; i < 8192; ++i)
		CHECK(name_value_list_append(&list, "item", "value"));
	CHECK(name_value_list_count(list) == 8192);
	name_value_list_free(&list);
	CHECK(list == NULL);
	name_value_list_free(&list);
	name_value_list_free(NULL);
	name_value_free_item(NULL);
	return EXIT_SUCCESS;
}
