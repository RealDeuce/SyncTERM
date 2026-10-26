/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "string_compat.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #condition); \
		return false; \
	} \
} while (0)

static bool
test_duplication(void)
{
	char source[] = "string";
	char *copy = strdup(source);
	char *short_copy = strndup(source, 3);
	char *long_copy = strndup(source, 20);
	char *empty_copy = strndup(source, 0);

	CHECK(copy != NULL && short_copy != NULL && long_copy != NULL
	    && empty_copy != NULL);
	source[0] = 'S';
	CHECK(strcmp(copy, "string") == 0);
	CHECK(strcmp(short_copy, "str") == 0);
	CHECK(strcmp(long_copy, "string") == 0);
	CHECK(strcmp(empty_copy, "") == 0);
	free(empty_copy);
	free(long_copy);
	free(short_copy);
	free(copy);
	const char bounded[] = {'a', 'b', 'c'};
	/* Exercise the actual bounded facility: Clang 19 incorrectly folds a
	 * direct call on this unterminated constant array into strdup(). */
	char *(*volatile bounded_duplicate)(const char *, size_t) = strndup;
	copy = bounded_duplicate(bounded, sizeof(bounded));
	if (copy == NULL || strcmp(copy, "abc") != 0)
		fprintf(stderr, "bounded strndup: source-size=%zu result=%s length=%zu\n",
		    sizeof(bounded), copy == NULL ? "(null)" : copy,
		    copy == NULL ? 0 : strlen(copy));
	CHECK(copy != NULL && strcmp(copy, "abc") == 0);
	free(copy);
	return true;
}

static bool
test_copy(void)
{
	char destination[5];
	char unchanged = 'x';

	CHECK(strlcpy(destination, "string", sizeof(destination)) == 6);
	CHECK(strcmp(destination, "stri") == 0);
	CHECK(strlcpy(destination, "one", sizeof(destination)) == 3);
	CHECK(strcmp(destination, "one") == 0);
	CHECK(strlcpy(&unchanged, "value", 0) == 5);
	CHECK(unchanged == 'x');
	CHECK(strlcpy(&unchanged, "value", 1) == 5);
	CHECK(unchanged == '\0');
	return true;
}

static bool
test_append(void)
{
	char destination[8] = "one";
	char truncated[6] = "one";
	char unterminated[3] = {'a', 'b', 'c'};

	CHECK(strlcat(destination, "two", sizeof(destination)) == 6);
	CHECK(strcmp(destination, "onetwo") == 0);
	CHECK(strlcat(truncated, "three", sizeof(truncated)) == 8);
	CHECK(strcmp(truncated, "oneth") == 0);
	CHECK(strlcat(unterminated, "x", sizeof(unterminated)) == 4);
	CHECK(memcmp(unterminated, "abc", sizeof(unterminated)) == 0);
	CHECK(strlcat(unterminated, "xy", 0) == 2);
	CHECK(memcmp(unterminated, "abc", sizeof(unterminated)) == 0);
	return true;
}

static bool
test_tokenize(void)
{
	char outer[] = ";;a/b;;c/d;";
	char *outer_state = NULL, *inner_state = NULL;
	char *first = strtok_r(outer, ";", &outer_state);
	CHECK(first != NULL && strcmp(first, "a/b") == 0);
	CHECK(strcmp(strtok_r(first, "/", &inner_state), "a") == 0);
	CHECK(strcmp(strtok_r(NULL, "/", &inner_state), "b") == 0);
	CHECK(strtok_r(NULL, "/", &inner_state) == NULL);
	CHECK(strtok_r(NULL, "/", &inner_state) == NULL);
	CHECK(strcmp(strtok_r(NULL, ";", &outer_state), "c/d") == 0);
	CHECK(strtok_r(NULL, ";", &outer_state) == NULL);
	char text[] = "a:b/c";
	CHECK(strcmp(strtok_r(text, ":", &outer_state), "a") == 0);
	CHECK(strcmp(strtok_r(NULL, "/", &outer_state), "b") == 0);
	CHECK(strcmp(strtok_r(NULL, "", &outer_state), "c") == 0);
	CHECK(strtok_r(NULL, "", &outer_state) == NULL);
	char empty[] = "";
	CHECK(strtok_r(empty, ":", &outer_state) == NULL);
	return true;
}

int
main(void)
{
	if (!test_duplication() || !test_copy() || !test_append() || !test_tokenize())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
