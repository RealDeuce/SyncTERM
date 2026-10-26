/* Regression test for transient directory-entry metadata failures. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
injected_fs_get_info(struct fs_info *result, const char *path)
{
	if (strstr(path, "missing") != NULL)
		return IO_ERROR_NOT_FOUND;
	*result = (struct fs_info){
		.kind = FS_ENTRY_FILE,
		.size = 4,
		.modified_time = 1,
	};
	return 0;
}

#define fs_get_info injected_fs_get_info
#include "../wren_picker_fs_posix.c"
#undef fs_get_info

static int
write_file(const char *directory, const char *name)
{
	char path[1024];
	FILE *stream;
	int length = snprintf(path, sizeof(path), "%s/%s", directory, name);

	if (length < 0 || (size_t)length >= sizeof(path))
		return -1;
	stream = fopen(path, "wb");
	if (stream == NULL)
		return -1;
	return fclose(stream);
}

int
main(int argc, char **argv)
{
	struct st_wren_directory_entries entries = {0};
	int status;
	bool passed;

	if (argc != 2 || write_file(argv[1], "missing") != 0
	    || write_file(argv[1], "retained") != 0)
		return EXIT_FAILURE;
	status = st_wren_directory_enumerate(argv[1], &entries);
	passed = status == 0 && entries.count == 1
	    && strcmp(entries.items[0].name, "retained") == 0;
	st_wren_directory_entries_dispose(&entries);
	fprintf(stderr, "metadata failure skips one entry: %s\n",
	    passed ? "PASS" : "FAIL");
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
