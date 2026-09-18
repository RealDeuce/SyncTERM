/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* POSIX/XDG audit regression: run with an absolute HOME and
 * XDG_CONFIG_HOME=relative XDG_CACHE_HOME=relative XDG_DATA_HOME=relative.
 * No directories or files are created. Intentionally fails until relative
 * XDG base directories are ignored as required by the specification.
 */
#include "st_app_paths.h"
#include "path.h"

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	struct st_app_paths paths = {0};
	const char *home = getenv("HOME");
	if (home == NULL || home[0] != '/') {
		fprintf(stderr, "test requires an absolute HOME\n");
		return 2;
	}
	if (st_app_paths_init(&paths, NULL) != 0)
		return 2;
	int failed = !path_is_rooted(paths.settings_file)
	    || !path_is_rooted(paths.cache_directory)
	    || !path_is_rooted(paths.keys_file);
	if (failed)
		fprintf(stderr, "relative XDG paths accepted: %s; %s; %s\n",
		    paths.settings_file, paths.cache_directory, paths.keys_file);
	st_app_paths_dispose(&paths);
	return failed;
}
