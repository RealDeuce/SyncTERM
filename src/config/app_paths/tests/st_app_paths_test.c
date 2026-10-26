/* Application path ownership and lazy-creation tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths.h"

#include "filesystem.h"

#include <stdio.h>
#include <string.h>

#if SYNCTERM_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

static void
diagnose_known_folders(void)
{
	static const struct {
		const char *name;
		const KNOWNFOLDERID *identifier;
	} folders[] = {
		{"RoamingAppData", &FOLDERID_RoamingAppData},
		{"ProgramData", &FOLDERID_ProgramData},
		{"InternetCache", &FOLDERID_InternetCache},
		{"Downloads", &FOLDERID_Downloads},
	};
	for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i) {
		PWSTR path = NULL;
		HRESULT result = SHGetKnownFolderPath(folders[i].identifier,
		    KF_FLAG_DONT_VERIFY, NULL, &path);
		fprintf(stderr, "known folder=%s HRESULT=%08lx\n", folders[i].name,
		    (unsigned long)result);
		CoTaskMemFree(path);
	}
}
#endif

#ifndef ST_APP_PATHS_TEST_DIRECTORY
#error "ST_APP_PATHS_TEST_DIRECTORY is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	char settings[sizeof(ST_APP_PATHS_TEST_DIRECTORY) + 32];
	char personal[sizeof(ST_APP_PATHS_TEST_DIRECTORY) + 32];
	char shared[sizeof(ST_APP_PATHS_TEST_DIRECTORY) + 32];
	char cache[sizeof(ST_APP_PATHS_TEST_DIRECTORY) + 16];
	char download[sizeof(ST_APP_PATHS_TEST_DIRECTORY) + 16];
	char directory[sizeof(ST_APP_PATHS_TEST_DIRECTORY) + 32];
	struct st_app_path_overrides overrides = {0};
	struct st_app_paths paths = {0};
	struct fs_info info;

	(void)snprintf(settings, sizeof(settings), "%s/a/b/settings.ini",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)snprintf(personal, sizeof(personal), "%s/c/d/personal.lst",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)snprintf(shared, sizeof(shared), "%s/shared.lst",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)snprintf(cache, sizeof(cache), "%s/cache",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)snprintf(download, sizeof(download), "%s/download",
	    ST_APP_PATHS_TEST_DIRECTORY);
	overrides.settings_file = settings;
	overrides.personal_list = personal;
	overrides.shared_list = shared;
	overrides.cache_directory = cache;
	overrides.download_directory = download;
	int status = st_app_paths_init(&paths, &overrides);
	if (status != 0) {
		fprintf(stderr, "app paths init returned %d\n", status);
#if SYNCTERM_PLATFORM_WINDOWS
		diagnose_known_folders();
#endif
	}
	CHECK(status == 0);
	CHECK(strcmp(paths.settings_file, settings) == 0);
	CHECK(strcmp(paths.personal_list, personal) == 0);
	CHECK(strcmp(paths.shared_list, shared) == 0);
	CHECK(strcmp(paths.cache_directory, cache) == 0);
	CHECK(strcmp(paths.download_directory, download) == 0);
	CHECK(strstr(paths.cached_system_list, "System List.lst") != NULL);
	CHECK(st_app_paths_prepare_settings(&paths) == 0);
	CHECK(st_app_paths_prepare_personal_list(&paths) == 0);
	CHECK(st_app_paths_prepare_cache(&paths) == 0);
	(void)snprintf(directory, sizeof(directory), "%s/a/b",
	    ST_APP_PATHS_TEST_DIRECTORY);
	CHECK(fs_get_info(&info, directory) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	(void)snprintf(directory, sizeof(directory), "%s/c/d",
	    ST_APP_PATHS_TEST_DIRECTORY);
	CHECK(fs_get_info(&info, directory) == 0
	    && info.kind == FS_ENTRY_DIRECTORY);
	CHECK(fs_get_info(&info, cache) == 0 && info.kind == FS_ENTRY_DIRECTORY);
	st_app_paths_dispose(&paths);

	(void)snprintf(directory, sizeof(directory), "%s/a/b",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)fs_remove_directory(directory);
	(void)snprintf(directory, sizeof(directory), "%s/a",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)fs_remove_directory(directory);
	(void)snprintf(directory, sizeof(directory), "%s/c/d",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)fs_remove_directory(directory);
	(void)snprintf(directory, sizeof(directory), "%s/c",
	    ST_APP_PATHS_TEST_DIRECTORY);
	(void)fs_remove_directory(directory);
	(void)fs_remove_directory(cache);
	(void)fs_remove_directory(ST_APP_PATHS_TEST_DIRECTORY);
	return 0;
}
