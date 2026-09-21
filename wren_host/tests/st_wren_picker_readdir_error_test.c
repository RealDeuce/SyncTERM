/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Return one entry before a read failure to exercise partial-result cleanup. */
static struct dirent partial_entry;
static bool returned_entry;

static struct dirent *
failed_readdir(DIR *stream)
{
	(void)stream;
	if (!returned_entry) {
		returned_entry = true;
		return &partial_entry;
	}
	errno = EIO;
	return NULL;
}

#define readdir failed_readdir
#include "../wren_picker_fs_posix.c"
#undef readdir

int
main(int argc, char **argv)
{
	struct st_wren_directory_entries entries = {0};
	char path[1024];
	FILE *stream;
	int length;
	int status;
	bool passed;

	if (argc != 2)
		return EXIT_FAILURE;
	length = snprintf(path, sizeof(path), "%s/partial", argv[1]);
	if (length < 0 || (size_t)length >= sizeof(path))
		return EXIT_FAILURE;
	stream = fopen(path, "wb");
	if (stream == NULL || fclose(stream) != 0)
		return EXIT_FAILURE;
	memcpy(partial_entry.d_name, "partial", sizeof("partial"));
	status = st_wren_directory_enumerate(argv[1], &entries);
	passed = status < 0 && entries.items == NULL && entries.count == 0;
	fprintf(stderr, "directory read error discards partial result: %s "
	    "(status %d)\n", passed ? "PASS" : "FAIL", status);
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
