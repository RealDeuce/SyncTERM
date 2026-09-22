/* Hyperlink registry contract tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_hyperlink.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (false)

struct thread_context {
	st_hyperlink_registry_t *registry;
	uint16_t identifier;
};

static int
add_from_thread(void *opaque)
{
	struct thread_context *context = opaque;
	const char uri[] = "https://thread.invalid/";

	return st_hyperlink_registry_add(context->registry, uri,
	    sizeof(uri) - 1, NULL, 0, &context->identifier);
}

int
main(void)
{
	st_hyperlink_registry_t *registry = NULL;
	struct st_hyperlink link = {0};
	char uri_sentinel;
	char parameter_sentinel;
	struct st_hyperlink unchanged = {&uri_sentinel, &parameter_sentinel};
	struct thread_context left = {0}, right = {0};
	st_thread_t left_thread;
	st_thread_t right_thread;
	uint16_t first = 0;
	uint16_t duplicate = 0;
	uint16_t distinct = 0;
	uint16_t identifier = 0;
	bool contains = false;
	int thread_result;
	const char uri[] = "https://example.invalid/path";
	const char parameter[] = "group";
	const char embedded_nul[] = {'a', '\0', 'b'};

	CHECK(st_hyperlink_registry_create(&registry) == 0);
	CHECK(st_hyperlink_registry_add(registry, uri, sizeof(uri) - 1,
	    parameter, sizeof(parameter) - 1, &first) == 0 && first == 1);
	CHECK(st_hyperlink_registry_add(registry, uri, sizeof(uri) - 1,
	    parameter, sizeof(parameter) - 1, &duplicate) == 0
	    && duplicate == first);
	CHECK(st_hyperlink_registry_add(registry, uri, sizeof(uri) - 1,
	    NULL, 0, &distinct) == 0 && distinct != first);
	CHECK(st_hyperlink_registry_add(registry, uri, sizeof(uri) - 1,
	    "", 0, &identifier) == 0 && identifier != distinct);
	CHECK(st_hyperlink_registry_copy(registry, identifier, &link) == 0
	    && link.parameter == NULL);
	st_hyperlink_dispose(&link);
	CHECK(st_hyperlink_registry_contains(registry, first, &contains) == 0
	    && contains);
	CHECK(st_hyperlink_registry_contains(registry, 0, &contains) == 0
	    && !contains);
	CHECK(st_hyperlink_registry_copy(registry, first, &link) == 0);
	CHECK(strcmp(link.uri, uri) == 0
	    && strcmp(link.parameter, parameter) == 0);
	st_hyperlink_dispose(&link);
	CHECK(st_hyperlink_registry_copy(registry, distinct, &link) == 0
	    && strcmp(link.uri, uri) == 0 && link.parameter == NULL);
	st_hyperlink_dispose(&link);
	CHECK(st_hyperlink_registry_copy(registry, UINT16_MAX, &unchanged)
	    == IO_ERROR_NOT_FOUND && unchanged.uri == &uri_sentinel
	    && unchanged.parameter == &parameter_sentinel);
	CHECK(st_hyperlink_registry_add(registry, "", 0, NULL, 0, &identifier)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_hyperlink_registry_add(registry, embedded_nul,
	    sizeof(embedded_nul), NULL, 0, &identifier)
	    == IO_ERROR_INVALID_ARGUMENT);

	left.registry = registry;
	right.registry = registry;
	CHECK(st_thread_create(&left_thread, add_from_thread, &left)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&right_thread, add_from_thread, &right)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(left_thread, &thread_result) == ST_THREAD_SUCCESS
	    && thread_result == 0);
	CHECK(st_thread_join(right_thread, &thread_result) == ST_THREAD_SUCCESS
	    && thread_result == 0 && left.identifier != right.identifier);

	for (size_t count = 6; count <= UINT16_MAX; ++count) {
		CHECK(st_hyperlink_registry_add(registry, "x", 1, NULL, 0,
		    &identifier) == 0 && identifier == count);
	}
	identifier = 27;
	CHECK(st_hyperlink_registry_add(registry, "x", 1, NULL, 0,
	    &identifier) == IO_ERROR_OUT_OF_RANGE && identifier == 27);
	CHECK(st_hyperlink_registry_copy(registry, first, &link) == 0
	    && strcmp(link.uri, uri) == 0);
	st_hyperlink_dispose(&link);
	st_hyperlink_registry_destroy(registry);
	return 0;
}
