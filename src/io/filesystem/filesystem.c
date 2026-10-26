/* Platform-neutral filesystem composition. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"

#include "io_error.h"
#include "path.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static atomic_uint_fast32_t fs_temporary_sequence;

static int
temporary_path(char **result, const char *path, uint32_t sequence)
{
	static const char suffix[] = ".syncterm-tmp-";
	char number[9];
	size_t path_length;
	size_t number_length;
	char *value;
	int written;

	written = snprintf(number, sizeof(number), "%08" PRIx32, sequence);
	if (written < 0 || (size_t)written >= sizeof(number))
		return IO_ERROR_OUT_OF_RANGE;
	path_length = strlen(path);
	number_length = (size_t)written;
	if (path_length > SIZE_MAX - sizeof(suffix) - number_length)
		return IO_ERROR_OUT_OF_RANGE;
	value = malloc(path_length + sizeof(suffix) + number_length);
	if (value == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(value, path, path_length);
	memcpy(value + path_length, suffix, sizeof(suffix) - 1);
	memcpy(value + path_length + sizeof(suffix) - 1, number,
	    number_length + 1);
	*result = value;
	return 0;
}

int
fs_replace_file(const char *path, const void *data, size_t size)
{
	char *temporary = NULL;
	FILE *stream = NULL;
	int status = IO_ERROR_ALREADY_EXISTS;

	if (path == NULL || path[0] == '\0' || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	for (unsigned int attempt = 0; attempt < 256; ++attempt) {
		uint32_t sequence = (uint32_t)atomic_fetch_add_explicit(
		    &fs_temporary_sequence, 1, memory_order_relaxed);

		free(temporary);
		temporary = NULL;
		status = temporary_path(&temporary, path, sequence);
		if (status != 0)
			break;
		status = fs_open(&stream, temporary, "wbx");
		if (status != IO_ERROR_ALREADY_EXISTS)
			break;
	}
	if (status == 0 && size != 0 && fwrite(data, 1, size, stream) != size)
		status = IO_ERROR_IO;
	if (stream != NULL && fclose(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status == 0)
		status = fs_rename(temporary, path);
	if (status != 0 && temporary != NULL)
		(void)fs_remove_file(temporary);
	free(temporary);
	return status;
}

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
