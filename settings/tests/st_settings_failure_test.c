/* Settings-store failure-atomicity regression tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_settings.h"
#include "st_settings_test_copy.h"

#include "filesystem.h"
#include "io_error.h"
#include "st_app_paths.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_SETTINGS_FAILURE_TEST_DIRECTORY
#error "ST_SETTINGS_FAILURE_TEST_DIRECTORY is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
write_text(const char *path, const char *text)
{
	FILE *stream;
	int status = fs_open(&stream, path, "wb");

	if (status != 0)
		return status;
	if (fwrite(text, 1, strlen(text), stream) != strlen(text))
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}

static char *
read_text(const char *path, size_t *result_size)
{
	FILE *stream;
	char *result;
	int64_t length;

	if (fs_open(&stream, path, "rb") != 0)
		return NULL;
	length = fs_stream_size(stream);
	if (length < 0 || (uint64_t)length >= SIZE_MAX) {
		(void)fclose(stream);
		return NULL;
	}
	result = malloc((size_t)length + 1);
	if (result == NULL) {
		(void)fclose(stream);
		return NULL;
	}
	if (fread(result, 1, (size_t)length, stream) != (size_t)length
	    || ferror(stream)) {
		free(result);
		result = NULL;
	}
	(void)fclose(stream);
	if (result != NULL) {
		result[(size_t)length] = '\0';
		*result_size = (size_t)length;
	}
	return result;
}

static bool
file_matches(const char *path, const char *expected, size_t expected_size)
{
	size_t actual_size = 0;
	char *actual = read_text(path, &actual_size);
	bool matches = actual != NULL && actual_size == expected_size
	    && memcmp(actual, expected, actual_size) == 0;

	free(actual);
	return matches;
}

int
main(void)
{
	static const char initial[] =
	    "[SyncTERM]\nScrollBackLines=100\nConfirmClose=true\n";
	static const char external[] =
	    "[SyncTERM]\nScrollBackLines=300\nConfirmClose=true\n";
	struct st_app_paths paths = {0};
	struct st_settings candidate;
	char settings_path[sizeof(ST_SETTINGS_FAILURE_TEST_DIRECTORY) + 32];
	char list_path[sizeof(ST_SETTINGS_FAILURE_TEST_DIRECTORY) + 32];
	char cache_path[sizeof(ST_SETTINGS_FAILURE_TEST_DIRECTORY) + 32];
	char system_path[sizeof(ST_SETTINGS_FAILURE_TEST_DIRECTORY) + 64];
	const char *installed_list_path;
	char *saved_file;
	size_t saved_size = 0;

	(void)snprintf(settings_path, sizeof(settings_path), "%s/settings.ini",
	    ST_SETTINGS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(list_path, sizeof(list_path), "%s/personal.lst",
	    ST_SETTINGS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(cache_path, sizeof(cache_path), "%s/cache",
	    ST_SETTINGS_FAILURE_TEST_DIRECTORY);
	(void)snprintf(system_path, sizeof(system_path), "%s/cache/System List.lst",
	    ST_SETTINGS_FAILURE_TEST_DIRECTORY);
	CHECK(fs_create_directories(ST_SETTINGS_FAILURE_TEST_DIRECTORY) == 0);
	CHECK(write_text(settings_path, initial) == 0);
	paths.settings_file = settings_path;
	paths.personal_list = list_path;
	paths.cache_directory = cache_path;
	paths.system_cache_directory = cache_path;
	paths.cached_system_list = system_path;
	paths.download_directory = ST_SETTINGS_FAILURE_TEST_DIRECTORY;

	/* Initial-load failure is reported and leaves no partial store. */
	st_settings_test_fail_copy_after(0);
	CHECK(st_settings_load(&paths) == IO_ERROR_OUT_OF_MEMORY);
	CHECK(st_settings_save() == IO_ERROR_INVALID_ARGUMENT);
	st_settings_test_disable_copy_failure();
	CHECK(st_settings_load(&paths) == 0);

	/* Reload failure preserves both live settings and the old baseline. */
	st_settings.scrollback_lines = 200;
	installed_list_path = st_settings.list_path;
	CHECK(write_text(settings_path, external) == 0);
	st_settings_test_fail_copy_after(0);
	CHECK(st_settings_reload() == IO_ERROR_OUT_OF_MEMORY);
	st_settings_test_disable_copy_failure();
	CHECK(st_settings.scrollback_lines == 200);
	CHECK(st_settings.list_path == installed_list_path);
	CHECK(file_matches(settings_path, external, sizeof(external) - 1));
	CHECK(st_settings_save() == IO_ERROR_CONFLICT);
	CHECK(file_matches(settings_path, external, sizeof(external) - 1));
	CHECK(st_settings_reload() == 0);
	CHECK(st_settings.scrollback_lines == 300);

	/* Every save copy fails before disk or installed state changes. */
	st_settings_init(&candidate);
	CHECK(st_settings_copy(&candidate, &st_settings));
	candidate.confirm_close = !st_settings.confirm_close;
	saved_file = read_text(settings_path, &saved_size);
	CHECK(saved_file != NULL);
	installed_list_path = st_settings.list_path;
	for (size_t failure = 0; failure < 2; ++failure) {
		st_settings_test_fail_copy_after(failure);
		CHECK(st_settings_save_value(&candidate) == IO_ERROR_OUT_OF_MEMORY);
		CHECK(st_settings.confirm_close != candidate.confirm_close);
		CHECK(st_settings.list_path == installed_list_path);
		CHECK(file_matches(settings_path, saved_file, saved_size));
	}
	st_settings_test_disable_copy_failure();
	CHECK(st_settings_save_value(&candidate) == 0);
	CHECK(st_settings.confirm_close == candidate.confirm_close);
	CHECK(!file_matches(settings_path, saved_file, saved_size));
	free(saved_file);
	st_settings_dispose(&candidate);
	st_settings_shutdown();

	CHECK(fs_remove_file(settings_path) == 0);
	{
		char lock_path[sizeof(settings_path) + 8];

		(void)snprintf(lock_path, sizeof(lock_path), "%s.lock",
		    settings_path);
		(void)fs_remove_file(lock_path);
	}
	(void)fs_remove_file(list_path);
	CHECK(fs_remove_directory(ST_SETTINGS_FAILURE_TEST_DIRECTORY) == 0);
	return 0;
}
