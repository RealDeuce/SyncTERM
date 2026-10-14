/* DOS-style asset filename lookup on a case-sensitive POSIX filesystem. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_asset_lookup.h"

#include "path.h"

#include <stdbool.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

static bool
ascii_equal_folded(const char *left, const char *right)
{
	for (; *left != '\0' && *right != '\0'; left++, right++) {
		unsigned char a = (unsigned char)*left;
		unsigned char b = (unsigned char)*right;

		if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
		if (a != b)
			return false;
	}
	return *left == '\0' && *right == '\0';
}

char *
cterm_rip_asset_case_path(const char *directory, const char *name)
{
	DIR *stream;
	struct dirent *entry;
	char *result = NULL;

	if (directory == NULL || name == NULL)
		return NULL;
	stream = opendir(directory);
	if (stream == NULL)
		return NULL;
	while ((entry = readdir(stream)) != NULL) {
		if (ascii_equal_folded(entry->d_name, name)) {
			(void)path_join_alloc(&result, directory, entry->d_name);
			break;
		}
	}
	closedir(stream);
	return result;
}

bool
cterm_rip_asset_path_readable(const char *path)
{
	return access(path, R_OK) == 0;
}
