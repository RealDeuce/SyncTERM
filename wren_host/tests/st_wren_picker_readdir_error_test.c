/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* Deterministically inject a directory read failure, not an end-of-stream. */
static struct dirent *
failed_readdir(DIR *stream)
{
	(void)stream;
	errno = EIO;
	return NULL;
}

#define readdir failed_readdir
#include "../wren_picker_fs_posix.c"
#undef readdir

int
main(int argc, char **argv)
{
	struct st_wren_picker_entries entries = {0};
	int status;

	if (argc != 2)
		return EXIT_FAILURE;
	status = st_wren_picker_enumerate(argv[1], &entries);
	for (size_t i = 0; i < entries.count; ++i)
		free(entries.items[i].name);
	free(entries.items);
	fprintf(stderr, "directory read error reported: %s (status %d)\n",
	    status < 0 ? "PASS" : "FAIL", status);
	return status < 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
