/* Discovery policy for user-supplied Wren scripts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "path.h"

#include <stdlib.h>
#include <string.h>

static int
compare_entries(const void *left, const void *right)
{
	const struct st_wren_directory_entry *a = left;
	const struct st_wren_directory_entry *b = right;

	return strcmp(a->name, b->name);
}

int
st_wren_script_inventory(const char *directory, const char *event,
	struct st_wren_directory_entries *entries)
{
	char *automatic = NULL;
	char *event_directory = NULL;
	const char *source = directory;
	size_t retained = 0;
	int status;

	if (directory == NULL || entries == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (event != NULL) {
		status = path_join_alloc(&automatic, directory, "auto");
		if (status == 0)
			status = path_join_alloc(&event_directory, automatic, event);
		free(automatic);
		if (status != 0)
			return status;
		source = event_directory;
	}
	status = st_wren_directory_enumerate(source, entries);
	free(event_directory);
	if (status != 0) {
		st_wren_directory_entries_dispose(entries);
		return status;
	}
	for (size_t i = 0; i < entries->count; ++i) {
		struct st_wren_directory_entry entry = entries->items[i];
		size_t length = strlen(entry.name);

		if (entry.directory || length < sizeof(".wren") - 1
		    || strcmp(entry.name + length - (sizeof(".wren") - 1),
		    ".wren") != 0) {
			free(entry.name);
			continue;
		}
		entry.name[length - (sizeof(".wren") - 1)] = '\0';
		entries->items[retained++] = entry;
	}
	entries->count = retained;
	if (entries->count > 1)
		qsort(entries->items, entries->count, sizeof(entries->items[0]),
		    compare_entries);
	return 0;
}

bool
st_wren_bbs_script_name_valid(const char *name)
{
	size_t length;

	if (name == NULL)
		return false;
	length = strlen(name);
	if (length == 0 || length >= 256)
		return false;
	for (size_t i = 0; i < length; ++i) {
		unsigned char value = (unsigned char)name[i];

		if (!((value >= 'A' && value <= 'Z')
		    || (value >= 'a' && value <= 'z')
		    || (value >= '0' && value <= '9') || value == '_'))
			return false;
	}
	return strcmp(name, "syncterm_menu") != 0
	    && strcmp(name, "syncterm_picker") != 0
	    && strcmp(name, "picker_bootstrap") != 0;
}
