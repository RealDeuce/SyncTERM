/* Platform-neutral filesystem composition. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"

#include "io_error.h"
#include "path.h"

#include <stdint.h>
#include <stdlib.h>

int
fs_create_parent_directories(const char *path)
{
	char *parent;
	ptrdiff_t required;
	int status;

	if (path == NULL || path[0] == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	required = path_parent(NULL, 0, path);
	if (required < 0)
		return (int)required;
	if ((uintmax_t)required >= SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	parent = malloc((size_t)required + 1);
	if (parent == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = (int)path_parent(parent, (size_t)required + 1, path);
	if (status >= 0)
		status = fs_create_directories(parent);
	free(parent);
	return status < 0 ? status : 0;
}
