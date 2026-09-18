/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "array_list.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #condition); \
		return false; \
	} \
} while (0)

struct number {
	int value;
	int order;
};

static int
compare_number(const struct number *left, const struct number *right)
{
	return (left->value > right->value) - (left->value < right->value);
}

SORTED_ARRAY_LIST_DECLARE(number_list, struct number, compare_number)
SORTED_ARRAY_LIST_DEFINE(number_list, struct number, compare_number)

static bool
test_array_list(void)
{
	struct number one = {1, 0};
	struct number two_first = {2, 1};
	struct number two_second = {2, 2};
	struct number three = {3, 0};
	struct number key = {2, 0};
	number_list_t list = NULL;
	size_t index;

	CHECK(number_list_count(NULL) == 0);
	CHECK(!number_list_reserve(&list, SIZE_MAX));
	CHECK(list == NULL);
	CHECK(number_list_insert_sorted(&list, &two_first));
	CHECK(number_list_insert_sorted(&list, &three));
	CHECK(number_list_insert_sorted(&list, &one));
	CHECK(number_list_insert_sorted(&list, &two_second));
	CHECK(number_list_count(list) == 4 && list[4] == NULL);
	CHECK(list[1] == &two_first && list[2] == &two_second);
	CHECK(number_list_find(list, &key, &index) && index == 1);
	CHECK(number_list_insert_unique(&list, &key, &index)
	    == ARRAY_LIST_INSERT_DUPLICATE && index == 1);
	CHECK(number_list_take(list, 1) == &two_first);
	CHECK(list[1] == &two_second && number_list_count(list) == 3);
	number_list_clear(list);
	CHECK(number_list_count(list) == 0 && list[0] == NULL);
	number_list_free_storage(&list);
	CHECK(list == NULL);
	return true;
}

static bool
test_growth(void)
{
	struct number numbers[257];
	number_list_t list = NULL;

	CHECK(number_list_reserve(&list, 0));
	CHECK(list == NULL);
	for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
		numbers[i] = (struct number){(int)i, 0};
		CHECK(number_list_append(&list, &numbers[i]));
		CHECK(number_list_count(list) == i + 1);
		CHECK(list[i + 1] == NULL);
		for (size_t j = 0; j <= i; ++j)
			CHECK(list[j] == &numbers[j]);
	}
	CHECK(number_list_reserve(&list, 1024));
	CHECK(number_list_count(list) == 257 && list[257] == NULL);
	number_list_t saved = list;
	CHECK(!number_list_reserve(&list, SIZE_MAX));
	CHECK(list == saved);
	CHECK(number_list_count(list) == 257 && list[257] == NULL);
	for (size_t i = 0; i < 257; ++i)
		CHECK(list[i] == &numbers[i]);
	number_list_clear(list);
	CHECK(list == saved && list[0] == NULL);
	CHECK(number_list_reserve(&list, 2048));
	CHECK(number_list_count(list) == 0 && list[0] == NULL);
	CHECK(number_list_append(&list, &numbers[0]));
	CHECK(number_list_count(list) == 1 && list[0] == &numbers[0]);
	CHECK(list[1] == NULL);
	number_list_free_storage(&list);
	CHECK(list == NULL);
	return true;
}

int
main(void)
{
	return test_array_list() && test_growth() ? EXIT_SUCCESS : EXIT_FAILURE;
}
