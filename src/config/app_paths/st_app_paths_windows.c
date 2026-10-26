/* Native Windows application paths. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_app_paths_internal.h"

#include "io_error.h"
#include "st_windows_utf.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <stdlib.h>

static char *
known_folder(REFKNOWNFOLDERID identifier)
{
	PWSTR wide = NULL;
	char *result = NULL;

	/* Path discovery must not require lazy-created folders to exist (for
	 * example Downloads in a service account's new profile). */
	if (SHGetKnownFolderPath(identifier, KF_FLAG_DONT_VERIFY, NULL, &wide) != S_OK) {
		CoTaskMemFree(wide);
		return NULL;
	}
	(void)st_windows_utf16_to_utf8(&result, wide);
	CoTaskMemFree(wide);
	return result;
}

int
st_app_paths_platform_init(struct st_app_paths *paths)
{
	char *roaming = known_folder(&FOLDERID_RoamingAppData);
	char *program = known_folder(&FOLDERID_ProgramData);
	char *cache = known_folder(&FOLDERID_InternetCache);
	char *download = known_folder(&FOLDERID_Downloads);
	char *personal = NULL;
	char *shared = NULL;

	if (roaming != NULL)
		personal = st_app_path_join(roaming, "SyncTERM");
	if (program != NULL)
		shared = st_app_path_join(program, "SyncTERM");
	if (cache != NULL)
		paths->cache_directory = st_app_path_join(cache, "SyncTERM");
	paths->download_directory = download;
	if (personal != NULL) {
		paths->settings_file = st_app_path_join(personal, "syncterm.ini");
		paths->personal_list = st_app_path_join(personal, "syncterm.lst");
		paths->keys_file = st_app_path_join(personal, "syncterm.ssh");
		paths->scripts_directory = st_app_path_join(personal, "scripts");
		paths->themes_directory = st_app_path_join(personal, "themes");
	}
	if (shared != NULL)
		paths->shared_list = st_app_path_join(shared, "syncterm.lst");
	free(roaming);
	free(program);
	free(cache);
	free(personal);
	free(shared);
	if (paths->settings_file == NULL || paths->personal_list == NULL
	    || paths->shared_list == NULL || paths->cache_directory == NULL
	    || paths->download_directory == NULL || paths->keys_file == NULL
	    || paths->scripts_directory == NULL || paths->themes_directory == NULL)
		return IO_ERROR_NOT_FOUND;
	return 0;
}
