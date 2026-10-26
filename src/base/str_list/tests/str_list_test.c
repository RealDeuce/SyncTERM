/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "str_list.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #condition); \
		return false; \
	} \
} while (0)

static bool
test_append_and_clone(void)
{
	char source[] = "first";
	str_list_t list = NULL;
	str_list_t clone = NULL;

	CHECK(str_list_count(NULL) == 0);
	CHECK(str_list_append(&list, source));
	source[0] = 'x';
	CHECK(strcmp(list[0], "first") == 0);
	for (size_t i = 1; i < 20; i++)
		CHECK(str_list_append(&list, "item"));
	CHECK(str_list_count(list) == 20);
	CHECK(list[20] == NULL);
	CHECK(str_list_contains(list, "first"));
	CHECK(!str_list_contains(list, "First"));
	CHECK(!str_list_contains(list, NULL));

	CHECK(str_list_clone(&clone, list));
	CHECK(str_list_count(clone) == str_list_count(list));
	CHECK(clone[0] != list[0]);
	CHECK(strcmp(clone[0], list[0]) == 0);
	list[0][0] = 'F';
	CHECK(strcmp(clone[0], "first") == 0);
	CHECK(str_list_clone(&clone, NULL));
	CHECK(clone == NULL);
	CHECK(str_list_clone(&list, list));
	CHECK(str_list_count(list) == 20);
	CHECK(strcmp(list[0], "First") == 0 && list[20] == NULL);

	str_list_free(&clone);
	str_list_free(&list);
	CHECK(clone == NULL);
	CHECK(list == NULL);
	str_list_free_item(NULL);
	str_list_free(&list);
	return true;
}

static bool
test_append_list(void)
{
	str_list_t destination = NULL;
	str_list_t source = NULL;

	CHECK(str_list_append_list(&destination, source));
	CHECK(destination == NULL);
	CHECK(str_list_append(&destination, "zero"));
	CHECK(str_list_append(&source, "one"));
	CHECK(str_list_append(&source, "two"));
	CHECK(str_list_append_list(&destination, source));
	CHECK(str_list_count(destination) == 3);
	CHECK(strcmp(destination[0], "zero") == 0);
	CHECK(strcmp(destination[1], "one") == 0);
	CHECK(strcmp(destination[2], "two") == 0);
	CHECK(destination[1] != source[0]);

	CHECK(str_list_append_list(&source, source));
	CHECK(str_list_count(source) == 4);
	CHECK(strcmp(source[0], "one") == 0);
	CHECK(strcmp(source[1], "two") == 0);
	CHECK(strcmp(source[2], "one") == 0);
	CHECK(strcmp(source[3], "two") == 0);

	str_list_free(&source);
	str_list_free(&destination);
	return true;
}

static bool
test_take(void)
{
	str_list_t list = NULL;
	char *item;

	CHECK(str_list_append(&list, "one"));
	CHECK(str_list_append(&list, "two"));
	CHECK(str_list_append(&list, "three"));
	CHECK(str_list_take(list, 3) == NULL);
	item = str_list_take(list, 1);
	CHECK(item != NULL && strcmp(item, "two") == 0);
	str_list_free_item(item);
	CHECK(str_list_count(list) == 2);
	CHECK(strcmp(list[0], "one") == 0);
	CHECK(strcmp(list[1], "three") == 0);
	CHECK(list[2] == NULL);
	item = str_list_take_last(list);
	CHECK(item != NULL && strcmp(item, "three") == 0);
	str_list_free_item(item);
	item = str_list_take_last(list);
	CHECK(item != NULL && strcmp(item, "one") == 0);
	str_list_free_item(item);
	CHECK(str_list_take_last(list) == NULL);
	CHECK(str_list_take_last(NULL) == NULL);
	CHECK(str_list_append(&list, "remove"));
	CHECK(str_list_remove(list, 0));
	CHECK(!str_list_remove(list, 0));
	str_list_free(&list);
	return true;
}

static bool
test_split_and_join(void)
{
	str_list_t list = NULL;
	str_list_t empty = NULL;
	str_list_t undivided = NULL;
	char *joined;

	CHECK(str_list_split(&list, ",one,,two;three,", ",;"));
	CHECK(str_list_count(list) == 3);
	CHECK(strcmp(list[0], "one") == 0);
	CHECK(strcmp(list[1], "two") == 0);
	CHECK(strcmp(list[2], "three") == 0);
	joined = str_list_join(list, "::");
	CHECK(joined != NULL && strcmp(joined, "one::two::three") == 0);
	str_list_free_item(joined);

	CHECK(str_list_append(&empty, "old"));
	CHECK(str_list_split(&empty, "", ","));
	CHECK(empty == NULL && str_list_count(empty) == 0);
	joined = str_list_join(empty, ",");
	CHECK(joined != NULL && strcmp(joined, "") == 0);
	str_list_free_item(joined);

	CHECK(str_list_split(&undivided, "one,two", ""));
	CHECK(undivided != NULL && str_list_count(undivided) == 1);
	CHECK(strcmp(undivided[0], "one,two") == 0);
	CHECK(!str_list_split(NULL, "one", ","));
	CHECK(!str_list_split(&list, NULL, ","));
	CHECK(!str_list_split(&list, "one", NULL));
	CHECK(str_list_count(list) == 3);
	joined = str_list_join(NULL, ",");
	CHECK(joined != NULL && strcmp(joined, "") == 0);
	str_list_free_item(joined);
	CHECK(str_list_join(list, NULL) == NULL);
	/* Replacement must finish reading inputs borrowed from the old list. */
	CHECK(str_list_split(&list, list[0], "n"));
	CHECK(str_list_count(list) == 2);
	CHECK(strcmp(list[0], "o") == 0 && strcmp(list[1], "e") == 0);

	str_list_free(&undivided);
	str_list_free(&empty);
	str_list_free(&list);
	return true;
}

int
main(void)
{
	if (!test_append_and_clone()
	    || !test_append_list()
	    || !test_take()
	    || !test_split_and_join())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
