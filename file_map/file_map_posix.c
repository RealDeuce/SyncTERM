/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "file_map.h"

#include "filesystem.h"
#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

struct file_map {
	const void *data;
	size_t size;
	unsigned char empty;
};

static int
file_map_errno_error(int error)
{
#ifdef ENODEV
	if (error == ENODEV)
		return IO_ERROR_NOT_SUPPORTED;
#endif
	return io_error_from_errno(error);
}

int
file_map_open_stream(file_map_t **result, FILE *stream)
{
	file_map_t *map;
	struct stat status;
	int descriptor;
	int flags;
	void *address;

	if (result == NULL || stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	descriptor = fileno(stream);
	if (descriptor < 0)
		return file_map_errno_error(errno);
	flags = fcntl(descriptor, F_GETFL);
	if (flags < 0)
		return file_map_errno_error(errno);
	if ((flags & O_ACCMODE) == O_WRONLY)
		return IO_ERROR_PERMISSION_DENIED;
	if (fstat(descriptor, &status) != 0)
		return file_map_errno_error(errno);
	if (S_ISDIR(status.st_mode))
		return IO_ERROR_IS_DIRECTORY;
	if (!S_ISREG(status.st_mode))
		return IO_ERROR_NOT_SUPPORTED;
	if (status.st_size < 0)
		return IO_ERROR_IO;
	if ((uintmax_t)status.st_size > SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	map = malloc(sizeof(*map));
	if (map == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	map->size = (size_t)status.st_size;
	map->empty = 0;
	if (map->size == 0) {
		map->data = &map->empty;
		*result = map;
		return 0;
	}
	address = mmap(NULL, map->size, PROT_READ, MAP_PRIVATE, descriptor, 0);
	if (address == MAP_FAILED) {
		int error = file_map_errno_error(errno);

		free(map);
		return error;
	}
	map->data = address;
	*result = map;
	return 0;
}

int
file_map_open(file_map_t **result, const char *path)
{
	FILE *stream = NULL;
	file_map_t *map = NULL;
	struct fs_info info;
	int close_error;
	int error;

	if (result == NULL || path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	error = fs_get_info(&info, path);
	if (error != 0)
		return error;
	if (info.kind == FS_ENTRY_DIRECTORY)
		return IO_ERROR_IS_DIRECTORY;
	if (info.kind != FS_ENTRY_FILE)
		return IO_ERROR_NOT_SUPPORTED;
	error = fs_open(&stream, path, "rb");
	if (error != 0)
		return error;
	error = file_map_open_stream(&map, stream);
	close_error = fclose(stream) == 0 ? 0 : file_map_errno_error(errno);
	if (error != 0)
		return error;
	if (close_error != 0) {
		(void)file_map_close(map);
		return close_error;
	}
	*result = map;
	return 0;
}

const void *
file_map_data(const file_map_t *map)
{
	return map == NULL ? NULL : map->data;
}

size_t
file_map_size(const file_map_t *map)
{
	return map == NULL ? 0 : map->size;
}

int
file_map_close(file_map_t *map)
{
	int error = 0;

	if (map == NULL)
		return 0;
	if (map->size != 0 && munmap((void *)map->data, map->size) != 0)
		error = file_map_errno_error(errno);
	free(map);
	return error;
}
