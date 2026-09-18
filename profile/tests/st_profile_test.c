/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_profile.h"

#include "string_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
	fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #e); \
	return 1; \
} } while (0)

int
main(void)
{
	struct st_profile source;
	struct st_profile copy;

	st_profile_init(&source);
	st_profile_init(&copy);
	strcpy(source.name, "Example");
	source.download_directory = strdup("downloads");
	source.tls_client_key = strdup("key.pem");
	CHECK(source.download_directory != NULL);
	CHECK(source.tls_client_key != NULL);
	CHECK(st_profile_copy(&copy, &source));
	CHECK(strcmp(copy.name, "Example") == 0);
	CHECK(strcmp(copy.download_directory, "downloads") == 0);
	CHECK(copy.download_directory != source.download_directory);
	CHECK(copy.upload_directory == NULL);
	CHECK(strcmp(copy.tls_client_key, "key.pem") == 0);
	st_profile_dispose(&copy);
	st_profile_dispose(&source);
	return 0;
}
