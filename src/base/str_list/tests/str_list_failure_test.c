/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "str_list.h"
#include "str_list_test_allocator.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #condition); \
		return false; \
	} \
} while (0)

static str_list_t
make_list(const char *const *items, size_t count)
{
	str_list_t list = NULL;

	str_list_test_disable_failure();
	for (size_t i = 0; i < count; i++) {
		if (!str_list_append(&list, items[i])) {
			str_list_free(&list);
			return NULL;
		}
	}
	return list;
}

static bool
list_equals(str_list_t list, const char *const *items, size_t count)
{
	if (str_list_count(list) != count)
		return false;
	for (size_t i = 0; i < count; i++) {
		if (strcmp(list[i], items[i]) != 0)
			return false;
	}
	return list[count] == NULL;
}

static bool
test_append_failures(void)
{
	for (size_t failure = 0; failure < 3; failure++) {
		str_list_t list = make_list(NULL, 0);
		bool succeeded;

		str_list_test_fail_after(failure);
		succeeded = str_list_append(&list, "one");
		if (failure < 2) {
			CHECK(!succeeded);
			CHECK(str_list_count(list) == 0 && list == NULL);
		} else {
			CHECK(succeeded);
			CHECK(str_list_count(list) == 1);
		}
		str_list_test_disable_failure();
		str_list_free(&list);
		CHECK(str_list_test_outstanding() == 0);
	}
	return true;
}

static bool
test_growth_failure(void)
{
	static const char *const items[] = {"one", "two", "three", "four"};
	str_list_t list = make_list(items, 4);
	str_list_t original = list;
	size_t outstanding = str_list_test_outstanding();

	CHECK(list != NULL);
	/* The copy succeeds; growing the pointer array fails. */
	str_list_test_fail_after(1);
	CHECK(!str_list_append(&list, list[0]));
	CHECK(list == original && list_equals(list, items, 4));
	CHECK(str_list_test_outstanding() == outstanding);
	str_list_test_disable_failure();
	CHECK(str_list_append(&list, list[0]));
	CHECK(str_list_count(list) == 5 && strcmp(list[4], "one") == 0);
	str_list_free(&list);
	CHECK(str_list_test_outstanding() == 0);
	return true;
}

static bool
test_append_list_failures(void)
{
	static const char *const destination_items[] = {
		"zero", "one", "two", "three"
	};
	static const char *const source_items[] = {"four", "five"};
	bool saw_success = false;

	for (size_t failure = 0; failure < 8; failure++) {
		str_list_t destination = make_list(destination_items, 4);
		str_list_t source = make_list(source_items, 2);
		bool succeeded;

		CHECK(destination != NULL && source != NULL);
		str_list_test_fail_after(failure);
		succeeded = str_list_append_list(&destination, source);
		if (!succeeded) {
			CHECK(list_equals(destination, destination_items, 4));
		} else {
			saw_success = true;
			CHECK(str_list_count(destination) == 6);
		}
		str_list_test_disable_failure();
		str_list_free(&source);
		str_list_free(&destination);
		CHECK(str_list_test_outstanding() == 0);
		if (succeeded)
			break;
	}
	CHECK(saw_success);
	return true;
}

static bool
test_clone_failures(void)
{
	static const char *const items[] = {"one", "two"};
	static const char *const old_items[] = {"old"};
	bool saw_success = false;

	for (size_t failure = 0; failure < 8; failure++) {
		str_list_t source = make_list(items, 2);
		str_list_t clone = make_list(old_items, 1);
		bool succeeded;

		CHECK(source != NULL && clone != NULL);
		str_list_test_fail_after(failure);
		succeeded = str_list_clone(&clone, source);
		CHECK(list_equals(source, items, 2));
		if (succeeded) {
			saw_success = true;
			CHECK(list_equals(clone, items, 2));
		} else
			CHECK(list_equals(clone, old_items, 1));
		str_list_test_disable_failure();
		str_list_free(&clone);
		str_list_free(&source);
		CHECK(str_list_test_outstanding() == 0);
		if (succeeded)
			break;
	}
	CHECK(saw_success);
	return true;
}

static bool
test_split_failures(void)
{
	static const char *const old_items[] = {"old"};
	bool saw_success = false;

	for (size_t failure = 0; failure < 10; failure++) {
		str_list_t list = make_list(old_items, 1);
		bool succeeded;

		CHECK(list != NULL);
		str_list_test_fail_after(failure);
		succeeded = str_list_split(&list, "one,two", ",");
		if (succeeded) {
			saw_success = true;
			CHECK(str_list_count(list) == 2);
		} else {
			CHECK(list_equals(list, old_items, 1));
		}
		str_list_test_disable_failure();
		str_list_free(&list);
		CHECK(str_list_test_outstanding() == 0);
		if (saw_success)
			break;
	}
	CHECK(saw_success);
	return true;
}

static bool
test_join_failures(void)
{
	static const char *const items[] = {"one", "two"};
	str_list_t list = make_list(items, 2);
	char *joined;

	CHECK(list != NULL);
	str_list_test_fail_after(0);
	CHECK(str_list_join(list, ",") == NULL);
	str_list_test_disable_failure();
	joined = str_list_join(list, ",");
	CHECK(joined != NULL && strcmp(joined, "one,two") == 0);
	str_list_free_item(joined);
	str_list_free(&list);
	CHECK(str_list_test_outstanding() == 0);
	return true;
}

int
main(void)
{
	if (!test_append_failures()
	    || !test_growth_failure()
	    || !test_append_list_failures()
	    || !test_clone_failures()
	    || !test_split_failures()
	    || !test_join_failures())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
