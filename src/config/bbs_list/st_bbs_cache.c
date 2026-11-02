/* Per-BBS cache ownership and digest-collision resolution. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_bbs_list_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_file_lock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
cache_lock(st_file_lock_t **lock, const char *directory)
{
	char *path = NULL;
	int status = fs_create_directories(directory);

	if (status == 0)
		status = path_join_alloc(&path, directory, "syncterm-cache");
	if (status == 0)
		status = st_file_lock_acquire(lock, path, true);
	free(path);
	return status;
}

static int
read_name(const char *directory, char name[ST_PROFILE_NAME_MAX + 1])
{
	char *path = NULL;
	struct fs_info info;
	FILE *stream = NULL;
	int status = path_join_alloc(&path, directory, ST_BBS_CACHE_NAME_FILE);

	if (status == 0)
		status = fs_get_link_info(&info, path);
	if (status == 0 && (info.kind != FS_ENTRY_FILE
	    || info.size == 0 || info.size > ST_PROFILE_NAME_MAX))
		status = IO_ERROR_INVALID_FORMAT;
	if (status == 0)
		status = fs_open(&stream, path, "rb");
	if (status == 0) {
		/* Read one extra byte to detect growth, without unbounded allocation. */
		size_t size = fread(name, 1, ST_PROFILE_NAME_MAX + 1, stream);

		if (ferror(stream))
			status = IO_ERROR_IO;
		else if (size == 0 || size > ST_PROFILE_NAME_MAX
		    || memchr(name, '\0', size) != NULL)
			status = IO_ERROR_INVALID_FORMAT;
		else {
			name[size] = '\0';
			if (!st_bbs_name_valid(name))
				status = IO_ERROR_INVALID_FORMAT;
		}
		if (fclose(stream) != 0 && status == 0)
			status = IO_ERROR_IO;
	}
	free(path);
	return status;
}

static int
write_name(const char *directory, const char *name)
{
	char *path = NULL;
	int status = path_join_alloc(&path, directory, ST_BBS_CACHE_NAME_FILE);

	if (status == 0)
		status = fs_replace_file(path, name, strlen(name));
	free(path);
	return status;
}

struct lookup {
	const char *directory;
	const char *key;
	const char *name;
	char *found;
};

static int
lookup_suffix(const char *key, void *context)
{
	struct lookup *lookup = context;
	char owner[ST_PROFILE_NAME_MAX + 1];
	char *path = NULL;
	struct fs_info info;
	int status;

	if (!st_bbs_cache_key_recognized(key)
	    || strncmp(key, lookup->key, ST_BBS_CACHE_KEY_MAX) != 0
	    || key[ST_BBS_CACHE_KEY_MAX] == '\0')
		return 0;
	status = path_join_alloc(&path, lookup->directory, key);
	if (status == 0)
		status = fs_get_link_info(&info, path);
	if (status == 0 && info.kind == FS_ENTRY_DIRECTORY) {
		status = read_name(path, owner);
		if (status == 0 && st_bbs_name_equal(owner, lookup->name)
		    && lookup->found == NULL) {
			lookup->found = path;
			path = NULL;
		}
	}
	free(path);
	/* Malformed/unmarked buckets are occupied, never adopted as suffixes. */
	return status == IO_ERROR_NOT_FOUND || status == IO_ERROR_INVALID_FORMAT
	    ? 0 : status;
}

/* Caller holds the root lock across resolution and any mutation. With create
 * false, NOT_FOUND also returns the available destination path for rename. */
static int
resolve(char **result, const char *directory, const char *name, bool create)
{
	char key[ST_BBS_CACHE_KEY_MAX + 1];
	char suffixed[ST_BBS_CACHE_DIRECTORY_KEY_MAX + 1];
	char owner[ST_PROFILE_NAME_MAX + 1];
	char *base = NULL;
	struct fs_info info;
	bool missing = false, unmarked = false;
	int status;

	if (!st_bbs_cache_key(key, sizeof(key), name))
		return IO_ERROR_INVALID_ARGUMENT;
	status = path_join_alloc(&base, directory, key);
	if (status < 0)
		return status;
	status = fs_get_link_info(&info, base);
	if (status == IO_ERROR_NOT_FOUND)
		missing = true;
	else if (status == 0 && info.kind != FS_ENTRY_DIRECTORY)
		status = IO_ERROR_NOT_DIRECTORY;
	else if (status == 0) {
		status = read_name(base, owner);
		unmarked = status == IO_ERROR_NOT_FOUND;
		if (status == 0 && st_bbs_name_equal(owner, name)) {
			*result = base;
			return 0;
		}
	}
	if (status < 0 && status != IO_ERROR_NOT_FOUND
	    && status != IO_ERROR_INVALID_FORMAT) {
		free(base);
		return status;
	}
	/* Search all suffixes before filling a gap: deleting an earlier collision
	 * bucket must not move another entry to a fresh, empty cache. */
	struct lookup lookup = {directory, key, name, NULL};
	status = st_bbs_cache_visit(directory, lookup_suffix, &lookup);
	if (status == 0 && lookup.found != NULL) {
		free(base);
		*result = lookup.found;
		return 0;
	}
	free(lookup.found);
	if (status == 0 && !missing && !unmarked) {
		for (size_t suffix = 1;; ++suffix) {
			(void)snprintf(suffixed, sizeof(suffixed), "%s-%zu", key, suffix);
			free(base);
			base = NULL;
			status = path_join_alloc(&base, directory, suffixed);
			if (status < 0)
				break;
			status = fs_get_link_info(&info, base);
			if (status == IO_ERROR_NOT_FOUND) {
				missing = true;
				status = 0;
				break;
			}
			if (status < 0)
				break;
			if (suffix == SIZE_MAX) {
				status = IO_ERROR_OUT_OF_RANGE;
				break;
			}
		}
	}
	if (status == 0 && !create && missing) {
		*result = base;
		return IO_ERROR_NOT_FOUND;
	}
	if (status == 0 && missing)
		status = fs_create_directory(base);
	/* Only the unsuffixed, legacy namespace may be claimed without a name. */
	if (status == 0)
		status = write_name(base, name);
	if (status == 0)
		*result = base;
	else
		free(base);
	return status;
}

int
st_bbs_cache_path_for_name(char **result, const char *directory,
    const char *name)
{
	st_file_lock_t *lock = NULL;
	int status;

	if (result == NULL || directory == NULL || !st_bbs_name_valid(name))
		return IO_ERROR_INVALID_ARGUMENT;
	status = cache_lock(&lock, directory);
	if (status == 0)
		status = resolve(result, directory, name, true);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_cache_path(char **result, const st_bbs_list_t *list,
    const char *name)
{
	return list == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_bbs_cache_path_for_name(result, list->cache_path, name);
}

int
st_bbs_cache_rename(st_bbs_list_t *list, const char *old_name,
    const char *new_name)
{
	st_file_lock_t *lock = NULL;
	char *old_path = NULL, *new_path = NULL;
	int status = cache_lock(&lock, list->cache_path);

	if (status == 0)
		status = resolve(&old_path, list->cache_path, old_name, false);
	if (status == IO_ERROR_NOT_FOUND) {
		free(old_path);
		old_path = NULL;
		status = 0;
	}
	if (status == 0 && old_path != NULL) {
		status = resolve(&new_path, list->cache_path, new_name, false);
		if (status == 0)
			status = IO_ERROR_ALREADY_EXISTS;
		else if (status == IO_ERROR_NOT_FOUND) {
			status = fs_rename(old_path, new_path);
			if (status == 0) {
				status = write_name(new_path, new_name);
				if (status < 0)
					(void)fs_rename(new_path, old_path);
			}
		}
	}
	free(old_path); free(new_path);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_cache_remove(st_bbs_list_t *list, const char *name)
{
	st_file_lock_t *lock = NULL;
	char *path = NULL;
	int status = cache_lock(&lock, list->cache_path);

	if (status == 0)
		status = resolve(&path, list->cache_path, name, false);
	if (status == IO_ERROR_NOT_FOUND)
		status = 0;
	if (status == 0 && path != NULL)
		status = st_bbs_cache_remove_tree(path);
	free(path);
	st_file_lock_release(lock);
	return status;
}

static int
sweep_one(const char *key, void *context)
{
	st_bbs_list_t *list = context;
	char owner[ST_PROFILE_NAME_MAX + 1];
	char base[ST_BBS_CACHE_KEY_MAX + 1];
	char *path = NULL;
	struct fs_info info;
	bool legacy = false;
	int status;

	if (!st_bbs_cache_key_recognized(key))
		return 0;
	if (list->system_cache_path != NULL) {
		const char *system = list->system_cache_path;
		for (const char *p = system; *p != '\0'; ++p)
			if (*p == '/' || *p == '\\') system = p + 1;
		if (st_bbs_name_equal(system, key))
			return 0;
	}
	status = path_join_alloc(&path, list->cache_path, key);
	if (status == 0)
		status = fs_get_link_info(&info, path);
	if (status == 0 && info.kind != FS_ENTRY_DIRECTORY) {
		free(path);
		return 0;
	}
	if (status == 0) {
		status = read_name(path, owner);
		legacy = status == IO_ERROR_NOT_FOUND
		    && strlen(key) == ST_BBS_CACHE_KEY_MAX;
		if (legacy)
			status = 0;
		else if (status == 0 && (!st_bbs_cache_key(base, sizeof(base), owner)
		    || strncmp(base, key, ST_BBS_CACHE_KEY_MAX) != 0))
			status = IO_ERROR_INVALID_FORMAT;
	}
	if (status == 0) {
		bool current = false;
		for (size_t i = 0; i < st_bbs_list_count(list); ++i) {
			const char *name = st_bbs_list_at(list, i)->profile->name;
			if (legacy ? (st_bbs_cache_key(base, sizeof(base), name)
			    && strcmp(base, key) == 0) : st_bbs_name_equal(owner, name)) {
				current = true;
				break;
			}
		}
		if (!current)
			status = st_bbs_cache_remove_tree(path);
	}
	free(path);
	/* Unknown ownership is not authority to delete a directory. */
	return status == IO_ERROR_INVALID_FORMAT || status == IO_ERROR_NOT_FOUND
	    ? 0 : status;
}

int
st_bbs_cache_sweep(st_bbs_list_t *list)
{
	st_file_lock_t *lock = NULL;
	int status = cache_lock(&lock, list->cache_path);

	if (status == 0)
		status = st_bbs_cache_visit(list->cache_path, sweep_one, list);
	st_file_lock_release(lock);
	return status;
}

int
st_bbs_list_sweep_orphan_caches(st_bbs_list_t *list)
{
	return list == NULL ? IO_ERROR_INVALID_ARGUMENT : st_bbs_cache_sweep(list);
}
