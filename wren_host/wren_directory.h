/* Internal cross-platform directory inventory for Wren hosts. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_WREN_DIRECTORY_H_
#define ST_WREN_DIRECTORY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_wren_directory_entry {
	char *name;
	bool directory;
	uint64_t size;
	int64_t modified;
};

struct st_wren_directory_entries {
	struct st_wren_directory_entry *items;
	size_t count;
	size_t capacity;
};

int st_wren_directory_enumerate(const char *directory,
	struct st_wren_directory_entries *entries);
void st_wren_directory_entries_dispose(
	struct st_wren_directory_entries *entries);

#endif
