/* POSIX BBS cache-directory maintenance. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *
join_path(const char *directory, const char *name)
{
	char *result = NULL;

	return path_join_alloc(&result, directory, name) == 0 ? result : NULL;
}

int
st_bbs_cache_remove_tree(const char *path)
{
	struct fs_info info;
	DIR *directory;
	struct dirent *entry;
	int status = fs_get_link_info(&info, path);

	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status < 0)
		return status;
	if (info.kind != FS_ENTRY_DIRECTORY)
		return fs_remove_file(path);
	directory = opendir(path);
	if (directory == NULL)
		return IO_ERROR_IO;
	while ((entry = readdir(directory)) != NULL) {
		char *child;

		if (strcmp(entry->d_name, ".") == 0
		    || strcmp(entry->d_name, "..") == 0)
			continue;
		child = join_path(path, entry->d_name);
		if (child == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			break;
		}
		status = st_bbs_cache_remove_tree(child);
		free(child);
		if (status < 0)
			break;
	}
	if (closedir(directory) != 0 && status == 0)
		status = IO_ERROR_IO;
	if (status == 0)
		status = fs_remove_directory(path);
	return status;
}

int
st_bbs_cache_visit(const char *path,
    int (*visit)(const char *key, void *context), void *context)
{
	DIR *directory = opendir(path);
	struct dirent *entry;
	int status = 0;

	if (directory == NULL)
		return io_error_from_errno(errno);
	for (;;) {
		errno = 0;
		entry = readdir(directory);
		if (entry == NULL) {
			if (errno != 0) status = io_error_from_errno(errno);
			break;
		}
		status = visit(entry->d_name, context);
		if (status < 0) break;
	}
	if (closedir(directory) != 0 && status == 0)
		status = IO_ERROR_IO;
	return status;
}
