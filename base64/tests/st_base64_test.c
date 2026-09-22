/* Base64 contract tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_base64.h"

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
check_vector(const char *plain, size_t plain_size, const char *encoded)
{
	char output[64];
	unsigned char decoded[64];
	size_t size;

	CHECK(st_base64_encoded_size(plain_size, &size));
	CHECK(size == strlen(encoded));
	CHECK(st_base64_encode(output, sizeof(output), plain, plain_size));
	CHECK(strcmp(output, encoded) == 0);
	CHECK(st_base64_decoded_size(encoded, strlen(encoded), &size));
	CHECK(size == plain_size);
	CHECK(st_base64_decode(decoded, sizeof(decoded), encoded,
	    strlen(encoded)));
	CHECK(memcmp(decoded, plain, plain_size) == 0);
}

int
main(void)
{
	size_t size = 99;
	char empty[1];

	check_vector("", 0, "");
	check_vector("f", 1, "Zg==");
	check_vector("fo", 2, "Zm8=");
	check_vector("foo", 3, "Zm9v");
	check_vector("foobar", 6, "Zm9vYmFy");
	CHECK(st_base64_encode(empty, sizeof(empty), NULL, 0));
	CHECK(empty[0] == '\0');
	CHECK(!st_base64_decoded_size("Zg=", 3, &size));
	CHECK(!st_base64_decoded_size("Zh==", 4, &size));
	CHECK(!st_base64_decoded_size("Zg=A", 4, &size));
	CHECK(!st_base64_decoded_size("Z g=", 4, &size));
	CHECK(!st_base64_decoded_size("Zm9*", 4, &size));
	CHECK(!st_base64_encode(empty, 0, NULL, 0));
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
