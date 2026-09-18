/* POSIX directory enumeration for the isolated Wren file picker. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_utf8.h"
#include "string_compat.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

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

static int
append_entry(struct st_wren_picker_entries *entries, const char *name,
	const struct fs_info *info)
{
	struct st_wren_picker_entry *item;
	size_t capacity;

	if (entries->count == entries->capacity) {
		capacity = entries->capacity == 0 ? 32 : entries->capacity * 2;
		if (capacity < entries->capacity
		    || capacity > SIZE_MAX / sizeof(entries->items[0]))
			return IO_ERROR_OUT_OF_MEMORY;
		item = realloc(entries->items, capacity * sizeof(entries->items[0]));
		if (item == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		entries->items = item;
		entries->capacity = capacity;
	}
	item = &entries->items[entries->count];
	*item = (struct st_wren_picker_entry){
		.name = strdup(name),
		.directory = info->kind == FS_ENTRY_DIRECTORY,
		.size = info->size,
		.modified = info->modified_time,
	};
	if (item->name == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	entries->count++;
	return 0;
}

int
st_wren_picker_enumerate(const char *directory,
	struct st_wren_picker_entries *entries)
{
	DIR *stream;
	struct dirent *entry;
	int status = 0;

	if (directory == NULL || entries == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	stream = opendir(directory);
	if (stream == NULL)
		return IO_ERROR_IO;
	while ((entry = readdir(stream)) != NULL) {
		char *full = NULL;
		struct fs_info info;

		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0
		    || !valid_utf8(entry->d_name))
			continue;
		status = path_join_alloc(&full, directory, entry->d_name);
		if (status != 0)
			break;
		status = fs_get_info(&info, full);
		free(full);
		if (status != 0)
			continue;
		if (info.kind != FS_ENTRY_FILE && info.kind != FS_ENTRY_DIRECTORY)
			continue;
		status = append_entry(entries, entry->d_name, &info);
		if (status != 0)
			break;
	}
	if (closedir(stream) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}
