/* Failure-atomic BBS directory regression tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list.h"
#include "st_bbs_test_allocator.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_BBS_FAILURE_TEST_DIRECTORY
#error "ST_BBS_FAILURE_TEST_DIRECTORY is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

static int
write_text(const char *path, const char *text)
{
	FILE *stream;
	int status = fs_open(&stream, path, "wb");

	if (status < 0)
		return status;
	if (fwrite(text, 1, strlen(text), stream) != strlen(text))
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static bool
file_equals(const char *path, const char *expected)
{
	char buffer[1024];
	FILE *stream = fopen(path, "rb");
	size_t expected_size = strlen(expected);
	size_t size;
	bool okay;

	if (stream == NULL || expected_size >= sizeof(buffer)) {
		if (stream != NULL)
			(void)fclose(stream);
		return false;
	}
	size = fread(buffer, 1, sizeof(buffer), stream);
	okay = !ferror(stream) && fclose(stream) == 0;
	return okay && size == expected_size
	    && memcmp(buffer, expected, expected_size) == 0;
}

int
main(void)
{
	static const char personal_text[] =
	    "[Alpha]\nAddress=personal.example\n";
	static const char shared_text[] =
	    "[Alpha]\nAddress=shared.example\n"
	    "[Shared]\nAddress=shared-only.example\n";
	struct st_app_paths paths = {0};
	struct st_profile profile;
	st_bbs_list_t *list = NULL;
	st_bbs_list_t *failed = NULL;
	const struct st_bbs_entry *entry;
	char personal[sizeof(ST_BBS_FAILURE_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_BBS_FAILURE_TEST_DIRECTORY) + 32];
	char cache[sizeof(ST_BBS_FAILURE_TEST_DIRECTORY) + 32];
	char system_cache[sizeof(ST_BBS_FAILURE_TEST_DIRECTORY) + 48];
	char system_list[sizeof(ST_BBS_FAILURE_TEST_DIRECTORY) + 64];
	uint64_t generation;
	bool reached_success = false;

	(void)snprintf(personal, sizeof(personal), "%s/personal.lst",
	    ST_BBS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_BBS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(cache, sizeof(cache), "%s/cache",
	    ST_BBS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(system_cache, sizeof(system_cache), "%s/cache/system",
	    ST_BBS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(system_list, sizeof(system_list), "%s/System List.lst",
	    system_cache);
	CHECK(fs_create_directories(system_cache) == 0);
	CHECK(write_text(personal, personal_text) == 0);
	CHECK(write_text(shared, shared_text) == 0);
	paths.settings_file = personal;
	paths.personal_list = personal;
	paths.shared_list = shared;
	paths.cache_directory = cache;
	paths.system_cache_directory = system_cache;
	paths.cached_system_list = system_list;
	paths.download_directory = ST_BBS_FAILURE_TEST_DIRECTORY;

	/* A failed copy of a configured optional source must fail open. */
	st_bbs_test_fail_after(2);
	CHECK(st_bbs_list_open(&failed, &paths, NULL, 0)
	    == IO_ERROR_OUT_OF_MEMORY);
	CHECK(failed == NULL);
	st_bbs_test_disable_failure();

	CHECK(st_bbs_list_open(&list, &paths, NULL, 0) == 0);
	entry = st_bbs_list_find(list, "Alpha");
	CHECK(entry != NULL);
	CHECK(st_bbs_entry_shadowed(entry) != NULL);
	CHECK(st_bbs_entry_shadowed(entry)->source == ST_BBS_SOURCE_SHARED);
	generation = st_bbs_list_generation(list);

	/* A failed reload preserves every borrowed view and its generation. */
	CHECK(write_text(personal, "[Alpha]\nPalette=not-a-number\n") == 0);
	CHECK(st_bbs_list_reload(list) < 0);
	CHECK(st_bbs_list_generation(list) == generation);
	CHECK(st_bbs_list_find(list, "Alpha") == entry);
	CHECK(strcmp(entry->profile->address, "personal.example") == 0);
	CHECK(write_text(personal, personal_text) == 0);

	st_profile_init(&profile);
	(void)snprintf(profile.name, sizeof(profile.name), "%s", "Beta");
	(void)snprintf(profile.address, sizeof(profile.address), "%s",
	    "beta.example");
	for (size_t failure = 0; failure < 32; ++failure) {
		int status;

		st_bbs_test_fail_after(failure);
		status = st_bbs_list_add(list, &profile, NULL);
		if (status == 0) {
			reached_success = true;
			CHECK(st_bbs_list_generation(list) == generation + 1);
			break;
		}
		CHECK(status == IO_ERROR_OUT_OF_MEMORY);
		CHECK(st_bbs_list_generation(list) == generation);
		CHECK(st_bbs_list_find(list, "Alpha") == entry);
		CHECK(st_bbs_list_find(list, "Beta") == NULL);
		CHECK(file_equals(personal, personal_text));
	}
	st_bbs_test_disable_failure();
	CHECK(reached_success);
	CHECK(st_bbs_list_find(list, "Beta") != NULL);
	st_profile_dispose(&profile);
	st_bbs_list_close(list);
	return 0;
}
