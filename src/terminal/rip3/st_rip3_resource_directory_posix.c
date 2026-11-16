/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_resources_internal.h"
#include "io_error.h"

#include <dirent.h>
#include <errno.h>

int
st_rip3_resource_directory_scan(struct st_rip3_resource_match *match)
{
	DIR *directory = opendir(match->directory);
	int status = 0;

	if (directory == NULL)
		return io_error_from_errno(errno);
	for (;;) {
		struct dirent *entry;

		errno = 0;
		entry = readdir(directory);
		if (entry == NULL) {
			if (errno != 0)
				status = io_error_from_errno(errno);
			break;
		}
		status = st_rip3_resource_consider(match, entry->d_name);
		if (status != 0)
			break;
	}
	if (closedir(directory) != 0 && status == 0)
		status = io_error_from_errno(errno);
	return status;
}
