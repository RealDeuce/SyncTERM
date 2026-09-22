/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "filesystem.h"

#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

static int
fs_errno_error(int error)
{
	return io_error_from_errno(error);
}

static enum fs_entry_kind
fs_mode_kind(mode_t mode)
{
	if (S_ISREG(mode))
		return FS_ENTRY_FILE;
	if (S_ISDIR(mode))
		return FS_ENTRY_DIRECTORY;
	if (S_ISLNK(mode))
		return FS_ENTRY_LINK;
	return FS_ENTRY_OTHER;
}

static int
fs_stat_info(struct fs_info *result, const char *path, bool follow)
{
	struct stat status;
	struct fs_info info;

	if (result == NULL || path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if ((follow ? stat(path, &status) : lstat(path, &status)) != 0)
		return fs_errno_error(errno);
	info.kind = fs_mode_kind(status.st_mode);
	if (info.kind == FS_ENTRY_FILE) {
		if (status.st_size < 0)
			return IO_ERROR_IO;
		info.size = (uint64_t)status.st_size;
	}
	else
		info.size = 0;
	info.modified_time = (int64_t)status.st_mtime;
	*result = info;
	return 0;
}

int
fs_get_info(struct fs_info *result, const char *path)
{
	return fs_stat_info(result, path, true);
}

int
fs_get_link_info(struct fs_info *result, const char *path)
{
	return fs_stat_info(result, path, false);
}

int
fs_open(FILE **result, const char *path, const char *mode)
{
	FILE *stream;

	if (result == NULL || path == NULL || *path == '\0' || mode == NULL ||
	    *mode == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	stream = fopen(path, mode);
	if (stream == NULL)
		return fs_errno_error(errno);
	*result = stream;
	return 0;
}

int64_t
fs_stream_size(FILE *stream)
{
	struct stat status;
	int descriptor;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	descriptor = fileno(stream);
	if (descriptor < 0)
		return fs_errno_error(errno);
	if (fstat(descriptor, &status) != 0)
		return fs_errno_error(errno);
	if (status.st_size < 0)
		return IO_ERROR_IO;
	if ((uintmax_t)status.st_size > INT64_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return (int64_t)status.st_size;
}

int64_t
fs_stream_position(FILE *stream)
{
	off_t position;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	position = ftello(stream);
	if (position < 0)
		return fs_errno_error(errno);
	if ((uintmax_t)position > INT64_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return (int64_t)position;
}

int
fs_stream_seek(FILE *stream, int64_t offset)
{
	off_t native_offset = (off_t)offset;

	if (stream == NULL || offset < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((int64_t)native_offset != offset)
		return IO_ERROR_OUT_OF_RANGE;
	if (fseeko(stream, native_offset, SEEK_SET) != 0)
		return fs_errno_error(errno);
	return 0;
}

int
fs_stream_truncate(FILE *stream, uint64_t size)
{
	int descriptor;
	off_t native_size;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	native_size = (off_t)size;
	if (native_size < 0 || (uint64_t)native_size != size)
		return IO_ERROR_OUT_OF_RANGE;
	if (fflush(stream) != 0)
		return fs_errno_error(errno);
	descriptor = fileno(stream);
	if (descriptor < 0)
		return fs_errno_error(errno);
	if (ftruncate(descriptor, native_size) != 0)
		return fs_errno_error(errno);
	return 0;
}

static int
fs_time_value(int64_t value, time_t *result)
{
	time_t converted = (time_t)value;
	if ((int64_t)converted != value)
		return IO_ERROR_OUT_OF_RANGE;
	*result = converted;
	return 0;
}

int
fs_set_modified_time(const char *path, int64_t modified_time)
{
	struct stat status;
	struct utimbuf times;
	int error;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if (stat(path, &status) != 0)
		return fs_errno_error(errno);
	error = fs_time_value(modified_time, &times.modtime);
	if (error < 0)
		return error;
	times.actime = status.st_atime;
	if (utime(path, &times) != 0)
		return fs_errno_error(errno);
	return 0;
}

int
fs_remove_file(const char *path)
{
	struct stat status;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if (lstat(path, &status) != 0)
		return fs_errno_error(errno);
	if (S_ISDIR(status.st_mode))
		return IO_ERROR_IS_DIRECTORY;
	if (unlink(path) != 0)
		return fs_errno_error(errno);
	return 0;
}

int
fs_remove_directory(const char *path)
{
	struct stat status;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if (lstat(path, &status) != 0)
		return fs_errno_error(errno);
	if (!S_ISDIR(status.st_mode))
		return IO_ERROR_NOT_DIRECTORY;
	if (rmdir(path) != 0)
		return fs_errno_error(errno);
	return 0;
}

int
fs_rename(const char *source, const char *destination)
{
	if (source == NULL || *source == '\0' || destination == NULL ||
	    *destination == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if (rename(source, destination) != 0)
		return fs_errno_error(errno);
	return 0;
}

static int
fs_same_file(const struct stat *source, const char *destination)
{
	struct stat status;

	if (stat(destination, &status) != 0) {
		if (errno == ENOENT)
			return 0;
		return fs_errno_error(errno);
	}
	return source->st_dev == status.st_dev && source->st_ino == status.st_ino;
}

int
fs_copy_file(const char *source, const char *destination)
{
	unsigned char buffer[64 * 1024];
	struct stat source_status;
	FILE *input = NULL;
	FILE *output = NULL;
	int error = 0;

	if (source == NULL || *source == '\0' || destination == NULL ||
	    *destination == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if (stat(source, &source_status) != 0)
		return fs_errno_error(errno);
	if (!S_ISREG(source_status.st_mode))
		return S_ISDIR(source_status.st_mode) ? IO_ERROR_IS_DIRECTORY :
		    IO_ERROR_NOT_SUPPORTED;
	int same = fs_same_file(&source_status, destination);
	if (same < 0)
		return same;
	if (same != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((input = fopen(source, "rb")) == NULL)
		return fs_errno_error(errno);
	if ((output = fopen(destination, "wb")) == NULL) {
		error = fs_errno_error(errno);
		fclose(input);
		return error;
	}
	for (;;) {
		size_t count = fread(buffer, 1, sizeof(buffer), input);
		if (count != 0 && fwrite(buffer, 1, count, output) != count) {
			error = fs_errno_error(errno == 0 ? EIO : errno);
			break;
		}
		if (count < sizeof(buffer)) {
			if (ferror(input))
				error = fs_errno_error(errno == 0 ? EIO : errno);
			break;
		}
	}
	if (error == 0 && fflush(output) != 0)
		error = fs_errno_error(errno);
	if (error == 0 && fchmod(fileno(output), source_status.st_mode & 0777) != 0)
		error = fs_errno_error(errno);
	if (fclose(output) != 0 && error == 0)
		error = fs_errno_error(errno);
	(void)fclose(input);
	if (error == 0)
		error = fs_set_modified_time(destination,
		    (int64_t)source_status.st_mtime);
	return error;
}

int
fs_create_directory(const char *path)
{
	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	if (mkdir(path, 0777) != 0)
		return fs_errno_error(errno);
	return 0;
}

static int
fs_create_or_confirm_directory(const char *path)
{
	struct stat status;

	if (mkdir(path, 0777) == 0)
		return 0;
	if (errno != EEXIST)
		return fs_errno_error(errno);
	if (stat(path, &status) != 0)
		return fs_errno_error(errno);
	return S_ISDIR(status.st_mode) ? 0 : IO_ERROR_NOT_DIRECTORY;
}

int
fs_create_directories(const char *path)
{
	char *copy;
	size_t length;
	int error = 0;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	length = strlen(path);
	copy = malloc(length + 1);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy, path, length + 1);
	while (length > 1 && copy[length - 1] == '/')
		copy[--length] = '\0';
	for (char *current = copy + (copy[0] == '/' ? 1 : 0);
	    *current != '\0'; current++) {
		if (*current != '/')
			continue;
		*current = '\0';
		if (*copy != '\0')
			error = fs_create_or_confirm_directory(copy);
		*current = '/';
		if (error < 0)
			break;
		while (current[1] == '/')
			current++;
	}
	if (error == 0 && strcmp(copy, "/") != 0)
		error = fs_create_or_confirm_directory(copy);
	free(copy);
	return error;
}
