/* POSIX theme-directory enumeration. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "theme_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "string_compat.h"
#include "st_utf8.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void
st_theme_directory_dispose(struct st_theme_directory *directory)
{
	if (directory == NULL)
		return;
	for (size_t i = 0; i < directory->count; ++i)
		free(directory->entries[i].name);
	free(directory->entries);
	*directory = (struct st_theme_directory){0};
}

static int
append_entry(struct st_theme_directory *result, const char *name)
{
	if (result->count == SIZE_MAX / sizeof(result->entries[0]))
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_theme_directory_entry *entries = realloc(result->entries,
	    (result->count + 1) * sizeof(result->entries[0]));
	if (entries == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	result->entries = entries;
	entries[result->count].name = strdup(name);
	if (entries[result->count].name == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	++result->count;
	return 0;
}

static bool
valid_utf8(const char *text)
{
	size_t length = strlen(text);
	size_t offset = 0;

	while (offset < length) {
		uint32_t codepoint;
		size_t consumed;

		if (st_utf8_decode_one(&codepoint, &consumed, text + offset,
		    length - offset) != 0)
			return false;
		offset += consumed;
	}
	return true;
}

int
st_theme_directory_read(struct st_theme_directory *result, const char *path)
{
	struct st_theme_directory value = {0};
	DIR *directory;
	int status = 0;

	if (result == NULL || path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	directory = opendir(path);
	if (directory == NULL)
		return io_error_from_errno(errno);
	for (;;) {
		struct dirent *entry;
		char *full = NULL;
		struct fs_info info;

		errno = 0;
		entry = readdir(directory);
		if (entry == NULL) {
			if (errno != 0)
				status = io_error_from_errno(errno);
			break;
		}

		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0
		    || !valid_utf8(entry->d_name))
			continue;
		status = path_join_alloc(&full, path, entry->d_name);
		if (status != 0)
			break;
		status = fs_get_info(&info, full);
		free(full);
		if (status != 0 || info.kind != FS_ENTRY_FILE) {
			status = 0;
			continue;
		}
		status = append_entry(&value, entry->d_name);
		if (status != 0)
			break;
	}
	if (closedir(directory) != 0 && status == 0)
		status = io_error_from_errno(errno);
	if (status != 0) {
		st_theme_directory_dispose(&value);
		return status;
	}
	st_theme_directory_dispose(result);
	*result = value;
	return 0;
}
