/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "path.h"

#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void
check_output(ptrdiff_t (*function)(char *, size_t, const char *),
    const char *input, const char *expected)
{
	char output[512];
	char small[4] = "old";
	ptrdiff_t required = function(NULL, 0, input);
	CHECK(required == (ptrdiff_t)strlen(expected));
	CHECK(function(output, sizeof(output), input) == required);
	CHECK(strcmp(output, expected) == 0);
	CHECK(function(small, sizeof(small), input) == required);
	if ((size_t)required >= sizeof(small))
		CHECK(strcmp(small, "old") == 0);
	/* Required-size queries exclude NUL; exactly that capacity cannot fit. */
	memset(output, 0x5a, sizeof(output));
	CHECK(function(output, (size_t)required, input) == required);
	for (size_t i = 0; i < sizeof(output); ++i)
		CHECK(output[i] == 0x5a);
	if (strlen(input) < sizeof(output)) {
		memcpy(output, input, strlen(input) + 1);
		CHECK(function(output, sizeof(output), output) == required);
		CHECK(strcmp(output, expected) == 0);
	}
}

int
main(void)
{
	char *joined = NULL;

	CHECK(strcmp(path_untrusted_basename("one/two\\three"), "three") == 0);
	CHECK(path_untrusted_component_valid("name"));
	CHECK(!path_untrusted_component_valid("."));
	CHECK(!path_untrusted_component_valid(".."));
	CHECK(!path_untrusted_component_valid("one/two"));
	CHECK(!path_untrusted_component_valid("one\\two"));
	CHECK(path_join_alloc(&joined, "one", "two") == 0);
#ifdef _WIN32
	CHECK(strcmp(joined, "one\\two") == 0);
#else
	CHECK(strcmp(joined, "one/two") == 0);
#endif
	free(joined);
	joined = NULL;
	CHECK(path_join_alloc(&joined, "", "two") == 0);
	CHECK(strcmp(joined, "two") == 0);
	free(joined);
	CHECK(path_basename(NULL) == NULL);
	CHECK(path_parent(NULL, 0, "") == IO_ERROR_INVALID_ARGUMENT);

#ifdef _WIN32
	CHECK(path_is_separator('/'));
	CHECK(path_is_separator('\\'));
	CHECK(path_is_rooted("C:\\one"));
	CHECK(path_is_rooted("\\one"));
	CHECK(path_is_rooted("//server/share/one"));
	CHECK(!path_is_rooted("C:one"));
	CHECK(strcmp(path_basename("one/two\\three"), "three") == 0);
	check_output(path_parent, "C:\\one\\two", "C:\\one");
	check_output(path_parent, "C:\\one", "C:\\");
	check_output(path_append_separator, "one/two", "one/two\\");
	check_output(path_expand_user, "~/one", "~/one");
	char absolute[1024];
	CHECK(path_make_absolute(absolute, sizeof(absolute), ".\\one\\..\\two") > 0);
	CHECK(strstr(absolute, "\\two") != NULL);
	CHECK(path_make_absolute(absolute, sizeof(absolute),
	    "//server/share/one/../two") == 18);
	CHECK(strcmp(absolute, "\\\\server\\share\\two") == 0);
	CHECK(path_make_absolute(absolute, sizeof(absolute), "C:.") > 0);
	CHECK(absolute[1] == ':' && absolute[2] == '\\');
	const char invalid[] = "\xc0\xaf";
	CHECK(path_make_absolute(NULL, 0, invalid) == IO_ERROR_INVALID_ENCODING);
#else
	CHECK(path_is_separator('/'));
	CHECK(!path_is_separator('\\'));
	CHECK(path_is_rooted("/one"));
	CHECK(!path_is_rooted("one"));
	CHECK(strcmp(path_basename("one/two\\three"), "two\\three") == 0);
	check_output(path_parent, "/one/two", "/one");
	check_output(path_parent, "/one", "/");
	check_output(path_parent, "one", ".");
	check_output(path_parent, "one//two", "one");
	check_output(path_append_separator, "one\\", "one\\/");
	char in_place[32] = "one";
	CHECK(path_append_separator(in_place, sizeof(in_place), in_place) == 4);
	CHECK(strcmp(in_place, "one/") == 0);
	char absolute[1024];
	CHECK(path_make_absolute(absolute, sizeof(absolute), "one/../two") > 0);
	CHECK(absolute[0] == '/');
	CHECK(strcmp(path_basename(absolute), "two") == 0);
	check_output(path_make_absolute, "/one/..", "/");
	check_output(path_make_absolute, "/../../one/./two/../three//", "/one/three");
	check_output(path_parent, "/one///", "/");
	check_output(path_parent, "/", "/");
	const char *home = getenv("HOME");
	if (home == NULL || *home == '\0')
		CHECK(path_expand_user(NULL, 0, "~/one") == IO_ERROR_NOT_FOUND);
	else {
		char expected[512];
		size_t length = strlen(home);
		const char *suffix = home[length - 1] == '/' ? "one" : "/one";
		int written = snprintf(expected, sizeof(expected), "%s%s", home,
		    suffix);

		CHECK(written >= 0 && (size_t)written < sizeof(expected));
		if (written >= 0 && (size_t)written < sizeof(expected))
			check_output(path_expand_user, "~/one", expected);
	}
	check_output(path_expand_user, "~someone/one", "~someone/one");
#endif

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
