/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "file_map.h"

#include "filesystem.h"
#include "io_error.h"

#include <errno.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

struct file_map {
	const void *data;
	size_t size;
	HANDLE mapping;
	unsigned char empty;
};

static int
file_map_errno_error(int error)
{
	return io_error_from_errno(error);
}

static int
file_map_windows_error(DWORD error)
{
	return io_error_from_windows(error);
}

int
file_map_open_stream(file_map_t **result, FILE *stream)
{
	file_map_t *map;
	BY_HANDLE_FILE_INFORMATION information;
	LARGE_INTEGER length;
	int descriptor;
	intptr_t native_descriptor;
	DWORD type;
	HANDLE handle;

	if (result == NULL || stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	descriptor = _fileno(stream);
	if (descriptor < 0)
		return file_map_errno_error(errno);
	native_descriptor = _get_osfhandle(descriptor);
	if (native_descriptor == -1)
		return file_map_errno_error(errno);
	handle = (HANDLE)native_descriptor;
	SetLastError(NO_ERROR);
	type = GetFileType(handle);
	if (type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR)
		return file_map_windows_error(GetLastError());
	if (type != FILE_TYPE_DISK)
		return IO_ERROR_NOT_SUPPORTED;
	if (!GetFileInformationByHandle(handle, &information))
		return file_map_windows_error(GetLastError());
	if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return IO_ERROR_IS_DIRECTORY;
	if (!GetFileSizeEx(handle, &length))
		return file_map_windows_error(GetLastError());
	if (length.QuadPart < 0 || (uint64_t)length.QuadPart > SIZE_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	map = malloc(sizeof(*map));
	if (map == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	map->size = (size_t)length.QuadPart;
	map->mapping = NULL;
	map->empty = 0;
	if (map->size == 0) {
		map->data = &map->empty;
		*result = map;
		return 0;
	}
	map->mapping = CreateFileMappingW(handle, NULL, PAGE_READONLY, 0, 0,
	    NULL);
	if (map->mapping == NULL) {
		int error = file_map_windows_error(GetLastError());

		free(map);
		return error;
	}
	map->data = MapViewOfFile(map->mapping, FILE_MAP_READ, 0, 0,
	    map->size);
	if (map->data == NULL) {
		int error = file_map_windows_error(GetLastError());

		(void)CloseHandle(map->mapping);
		free(map);
		return error;
	}
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
	if (map->size != 0 && !UnmapViewOfFile(map->data))
		error = file_map_windows_error(GetLastError());
	if (map->mapping != NULL && !CloseHandle(map->mapping) && error == 0)
		error = file_map_windows_error(GetLastError());
	free(map);
	return error;
}
