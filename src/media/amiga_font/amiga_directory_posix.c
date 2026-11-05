/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "amiga_directory_internal.h"
#include "io_error.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
int amiga_directory_names(struct amiga_names *result, const char *path)
{
	struct amiga_names names = {0};
	DIR *directory = opendir(path);
	if (!directory)
		return io_error_from_errno(errno);
	int status = 0;
	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(directory);
		if (!entry) {
			if (errno)
				status = io_error_from_errno(errno);
			break;
		}
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		status = amiga_names_append(&names, entry->d_name);
		if (status)
			break;
	}
	if (closedir(directory) != 0 && !status)
		status = io_error_from_errno(errno);
	if (status)
		amiga_names_dispose(&names);
	else
		*result = names;
	return status;
}
