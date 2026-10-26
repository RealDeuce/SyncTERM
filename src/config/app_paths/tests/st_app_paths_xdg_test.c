/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Relative XDG base directories are ignored as required by the specification. */
#include "st_app_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ST_APP_PATHS_XDG_TEST_HOME
#error "ST_APP_PATHS_XDG_TEST_HOME is required"
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
	struct st_app_paths paths = {0};
	struct st_app_path_overrides overrides = {
		.settings_file = "relative/settings.ini",
	};
	const char *home = getenv("HOME");

	CHECK(home != NULL);
	CHECK(strcmp(home, ST_APP_PATHS_XDG_TEST_HOME) == 0);
	CHECK(st_app_paths_init(&paths, NULL) == 0);
	CHECK(strcmp(paths.settings_file,
	    ST_APP_PATHS_XDG_TEST_HOME "/.config/syncterm/syncterm.ini") == 0);
	CHECK(strcmp(paths.cache_directory,
	    ST_APP_PATHS_XDG_TEST_HOME "/.cache/syncterm") == 0);
	CHECK(strcmp(paths.keys_file,
	    ST_APP_PATHS_XDG_TEST_HOME "/.local/share/syncterm/syncterm.ssh")
	    == 0);
	CHECK(strcmp(paths.download_directory, "relative-download") == 0);
	st_app_paths_dispose(&paths);

	CHECK(st_app_paths_init(&paths, &overrides) == 0);
	CHECK(strcmp(paths.settings_file, "relative/settings.ini") == 0);
	st_app_paths_dispose(&paths);
	return 0;
}
