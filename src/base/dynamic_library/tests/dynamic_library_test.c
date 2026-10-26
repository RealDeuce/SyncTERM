/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "dynamic_library.h"

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

typedef int (*binary_function_t)(int, int);
typedef int (*value_function_t)(void);

static binary_function_t retained_add;
static int *retained_value;

static binary_function_t
binary_function_from_symbol(void *symbol)
{
	return (binary_function_t)symbol;
}

#if defined(SYNCTERM_DYNAMIC_LIBRARY_TEST_POSIX)
static value_function_t
value_function_from_symbol(void *symbol)
{
	return (value_function_t)symbol;
}
#endif

static int
test_arguments(void)
{
	dynamic_library_t *sentinel = (dynamic_library_t *)(uintptr_t)1;
	const char *valid[] = {"dynamic_library_fixture"};
	const char *empty[] = {""};
	const char *null_name[] = {NULL};
	const char *path_name[] = {"directory/library"};
	const char *backslash_name[] = {"directory\\library"};
	const char *non_ascii_name[] = {"library\xc3\xa9"};

	CHECK(dynamic_library_open(NULL, valid, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, NULL, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, valid, 0, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, empty, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, null_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, path_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, backslash_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, non_ascii_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(dynamic_library_open(&sentinel, valid, 1, 0,
	    (enum dynamic_library_visibility)99) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(sentinel == (dynamic_library_t *)(uintptr_t)1);
	CHECK(dynamic_library_symbol(NULL, "symbol") == NULL);
	CHECK(dynamic_library_close(NULL) == 0);
	return 0;
}

static int
test_symbols_and_names(void)
{
	const char *names[] = {
		"dynamic_library_missing_alias",
		"dynamic_library_fixture",
	};
	dynamic_library_t *library = NULL;
	binary_function_t add;
	int *value;

	CHECK(dynamic_library_open(&library, names, 2, 0,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(library != NULL);
	add = binary_function_from_symbol(dynamic_library_symbol(library,
	    "dynamic_library_fixture_add"));
	CHECK(add != NULL);
	CHECK(add(19, 23) == 42);
	value = dynamic_library_symbol(library,
	    "dynamic_library_fixture_value");
	CHECK(value != NULL);
	CHECK(*value == 73);
	CHECK(dynamic_library_symbol(library, "missing_symbol") == NULL);
	CHECK(dynamic_library_symbol(library, "") == NULL);
	CHECK(dynamic_library_symbol(library, NULL) == NULL);
	retained_add = add;
	retained_value = value;
	*retained_value = 74;
	return 0;
}

static int
test_retained_loads(void)
{
	const char *names[] = {"dynamic_library_fixture"};

	/* Successful callers discard their local handle without unloading symbols
	 * or leaving a heap-allocated handle wrapper behind (checked by LSan). */
	CHECK(retained_add(19, 23) == 42);
	CHECK(*retained_value == 74);
	for (unsigned int i = 0; i < 256; ++i) {
		dynamic_library_t *library = NULL;

		CHECK(dynamic_library_open(&library, names, 1, 0,
		    DYNAMIC_LIBRARY_LOCAL) == 0);
		CHECK(binary_function_from_symbol(dynamic_library_symbol(library,
		    "dynamic_library_fixture_add")) == retained_add);
		CHECK(dynamic_library_symbol(library,
		    "dynamic_library_fixture_value") == retained_value);
		CHECK(*retained_value == 74);
	}
	return 0;
}

static int
test_rejected_resolution(void)
{
	const char *names[] = {"dynamic_library_bare_fixture"};
	const char *retained_names[] = {"dynamic_library_fixture"};
	dynamic_library_t *library = NULL;

	CHECK(dynamic_library_open(&library, names, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(dynamic_library_symbol(library, "missing_required_symbol") == NULL);
	CHECK(dynamic_library_close(library) == 0);
	library = NULL;
	/* Rejecting a separate load reference must not unload a retained feature. */
	CHECK(dynamic_library_open(&library, retained_names, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(dynamic_library_symbol(library, "missing_required_symbol") == NULL);
	CHECK(dynamic_library_close(library) == 0);
	CHECK(retained_add(19, 23) == 42);
	CHECK(*retained_value == 74);
	return 0;
}

static int
test_filename_fallbacks(void)
{
	const char *versioned[] = {"dynamic_library_versioned_fixture"};
	const char *bare[] = {"dynamic_library_bare_fixture"};
	dynamic_library_t *library = NULL;

	CHECK(dynamic_library_open(&library, versioned, 1, 2,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(dynamic_library_symbol(library, "dynamic_library_fixture_add") != NULL);
	library = NULL;
	CHECK(dynamic_library_open(&library, versioned, 1, 4,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(dynamic_library_symbol(library, "dynamic_library_fixture_add") != NULL);
	library = NULL;
	CHECK(dynamic_library_open(&library, bare, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(dynamic_library_symbol(library, "dynamic_library_fixture_add") != NULL);
	return 0;
}

static int
test_missing_library(void)
{
	const char *names[] = {"syncterm_library_that_does_not_exist"};
	dynamic_library_t *sentinel = (dynamic_library_t *)(uintptr_t)1;

	CHECK(dynamic_library_open(&sentinel, names, 1, 3,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_NOT_FOUND);
	CHECK(sentinel == (dynamic_library_t *)(uintptr_t)1);
	return 0;
}

#if defined(SYNCTERM_DYNAMIC_LIBRARY_TEST_POSIX)
static int
test_visibility_and_eager_loading(void)
{
	const char *provider_name[] = {"dynamic_library_provider_fixture"};
	const char *consumer_name[] = {"dynamic_library_consumer_fixture"};
	dynamic_library_t *provider = NULL;
	dynamic_library_t *consumer = NULL;
	value_function_t value;

	CHECK(dynamic_library_open(&consumer, consumer_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_NOT_FOUND);
	CHECK(dynamic_library_open(&provider, provider_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	CHECK(dynamic_library_open(&consumer, consumer_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == IO_ERROR_NOT_FOUND);
	provider = NULL;
	CHECK(dynamic_library_open(&provider, provider_name, 1, 0,
	    DYNAMIC_LIBRARY_GLOBAL) == 0);
	CHECK(dynamic_library_open(&consumer, consumer_name, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL) == 0);
	value = value_function_from_symbol(dynamic_library_symbol(consumer,
	    "dynamic_library_consumer_value"));
	CHECK(value != NULL);
	CHECK(value() == 91);
	return 0;
}
#endif

int
main(void)
{
	CHECK(test_arguments() == 0);
	CHECK(test_symbols_and_names() == 0);
	CHECK(test_retained_loads() == 0);
	CHECK(test_rejected_resolution() == 0);
	CHECK(test_filename_fallbacks() == 0);
	CHECK(test_missing_library() == 0);
#if defined(SYNCTERM_DYNAMIC_LIBRARY_TEST_POSIX)
	CHECK(test_visibility_and_eager_loading() == 0);
#endif
	return 0;
}
