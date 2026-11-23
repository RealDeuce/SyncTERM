/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_resources.h"
#include "st_rip3_resources_internal.h"

#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_unicode_compare.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static int
directory_status(const char *path)
{
	struct fs_info info;
	if (path == NULL || path[0] == '\0')
		return IO_ERROR_NOT_FOUND;
	int status = fs_get_link_info(&info,path);
	if (status == 0 && info.kind != FS_ENTRY_DIRECTORY) return IO_ERROR_NOT_DIRECTORY;
	return status;
}

static int
ensure_directory(const char *path)
{
	int status = fs_create_directory(path);
	return status == IO_ERROR_ALREADY_EXISTS ? directory_status(path) : status;
}

static int
host_directory_status(const struct st_rip3_resource_paths *paths)
{
	int status = 0;
	if (paths->named_host_root != NULL)
		status = directory_status(paths->named_host_root);
	return status == 0 ? directory_status(paths->host) : status;
}

static int
cache_bucket_status(const char *root)
{
	size_t capacity = strlen(root) + 1;
	char *bucket = malloc(capacity);
	if (bucket == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	ptrdiff_t length = path_parent(bucket, capacity, root);
	int status = length < 0 ? (int)length : directory_status(bucket);
	free(bucket);
	return status;
}

int
st_rip3_resource_scratch(struct st_rip3_resource *result, const struct st_rip3_resource_paths *paths)
{
	static atomic_uint_fast32_t sequence;
	if (paths == NULL || paths->host == NULL || paths->host[0] == '\0')
		return IO_ERROR_NOT_SUPPORTED;
	const char *root = paths->named_host_root != NULL ? paths->named_host_root : paths->host;
	int status = cache_bucket_status(root);
	if (status == 0)
		status = ensure_directory(root);
	if (status != 0)
		return status;
	for (unsigned attempt = 0; attempt < 256; ++attempt) {
		uint32_t id = (uint32_t)atomic_fetch_add_explicit(&sequence, 1, memory_order_relaxed);
		char name[sizeof(".rip3-overflow-00000000.part")];
		(void)snprintf(name, sizeof(name), ".rip3-overflow-%08" PRIx32 ".part", id);
		struct st_rip3_resource scratch = {0};
		status = path_join_alloc(&scratch.path, root, name);
		if (status == 0)
			status = fs_open(&scratch.stream, scratch.path, "wbx");
		if (status == 0) {
			*result = scratch;
			return 0;
		}
		free(scratch.path);
		if (status != IO_ERROR_ALREADY_EXISTS)
			return status;
	}
	return IO_ERROR_ALREADY_EXISTS;
}

static int
scratch_parent_status(const char *path)
{
	size_t capacity = strlen(path) + 1;
	char *root = malloc(capacity);
	if (root == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	ptrdiff_t length = path_parent(root, capacity, path);
	int status = length < 0 ? (int)length : cache_bucket_status(root);
	if (status == 0)
		status = directory_status(root);
	free(root);
	return status;
}

int
st_rip3_resource_read_scratch(struct st_rip3_resource *result, const struct st_rip3_resource *file)
{
	struct fs_info info;
	int status = scratch_parent_status(file->path);
	if (status == 0)
		status = fs_get_link_info(&info, file->path);
	if (status == 0 && info.kind != FS_ENTRY_FILE)
		status = IO_ERROR_NOT_FOUND;
	return status == 0 ? fs_open(&result->stream, file->path, "rb") : status;
}

void
st_rip3_resource_remove_scratch(struct st_rip3_resource *file)
{
	if (file->stream != NULL) {
		(void)fclose(file->stream);
		file->stream = NULL;
	}
	if (file->path != NULL && scratch_parent_status(file->path) == 0)
		(void)fs_remove_file(file->path);
	st_rip3_resource_dispose(file);
}

int
st_rip3_resource_prepare_cache(const char *root, const char *child)
{
	int status = ensure_directory(root);
	if (status == 0 && child != NULL) status = ensure_directory(child);
	return status;
}

void
st_rip3_resource_dispose(struct st_rip3_resource *resource)
{
	if (resource == NULL)
		return;
	if (resource->stream != NULL)
		(void)fclose(resource->stream);
	free(resource->path);
	*resource = (struct st_rip3_resource){0};
}

int
st_rip3_resource_consider(struct st_rip3_resource_match *match, const char *name)
{
	char *key = NULL, *path = NULL;
	size_t size;
	struct fs_info info;
	int status = st_unicode_caseless_key(&key, &size, name, strlen(name));
	bool equal;

	/* A POSIX directory may contain names outside the UTF-8 client namespace. */
	if (status == IO_ERROR_INVALID_ENCODING)
		return 0;
	if (status != 0)
		return status;
	equal = size == match->key_size && memcmp(key, match->key, size) == 0;
	free(key);
	if (!equal)
		return 0;
	status = path_join_alloc(&path, match->directory, name);
	if (status == 0)
		status = match->writing || match->existence_only
		    ? fs_get_link_info(&info,path) : fs_get_info(&info,path);
	if (status == 0 && info.kind != FS_ENTRY_FILE)
		status = match->writing ? IO_ERROR_CONFLICT : IO_ERROR_NOT_FOUND;
	if (status == 0 && match->path != NULL)
		status = IO_ERROR_CONFLICT;
	if (status != 0) {
		free(path);
		return status == IO_ERROR_NOT_FOUND ? 0 : status;
	}
	match->path = path;
	return 0;
}

static int
lookup_directory(struct st_rip3_resource *result, const char *directory,
    const char *key, size_t key_size, bool *matched)
{
	struct st_rip3_resource_match match = {
		.directory = directory, .key = key, .key_size = key_size, .existence_only = result == NULL,
	};
	int status;

	if (directory == NULL || directory[0] == 0)
		return IO_ERROR_NOT_FOUND;
	status = st_rip3_resource_directory_scan(&match);
	if (status == 0 && match.path == NULL)
		status = IO_ERROR_NOT_FOUND;
	if (status == 0 && result != NULL) {
		struct st_rip3_resource resource = {.path = match.path};
		if (matched != NULL) *matched = true;
		status = fs_open(&resource.stream, match.path, "rb");
		if (status == 0) {
			*result = resource;
			return 0;
		}
	}
	free(match.path);
	return status;
}

bool
st_rip3_resource_name_valid(const char *name)
{
	size_t size;

	if (!path_untrusted_component_valid(name))
		return false;
	size = strlen(name);
	if (name[size - 1] == '.' || name[size - 1] == ' ')
		return false;
	for (const unsigned char *p = (const unsigned char *)name; *p != 0; p++) {
		if (*p < 32 || *p == 127 || strchr(":*?\"<>|", *p) != NULL)
			return false;
	}
	return true;
}

static int
lookup_resource(struct st_rip3_resource *result,
    const struct st_rip3_resource_paths *paths,
    enum st_rip3_resource_kind kind, const char *name, bool *matched)
{
	char *key = NULL;
	size_t key_size;
	int status, first_error = IO_ERROR_NOT_FOUND;
	const char *fixed[2];
	size_t fixed_count;

	if (paths == NULL || !st_rip3_resource_name_valid(name)
	    || (kind != ST_RIP3_RESOURCE_MEDIA && kind != ST_RIP3_RESOURCE_FONT))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_caseless_key(&key, &key_size, name, strlen(name));
	if (status != 0)
		return status;
	if (kind == ST_RIP3_RESOURCE_FONT) {
		fixed[0] = paths->fonts;
		fixed_count = 1;
	}
	else {
		fixed[0] = paths->host;
		fixed[1] = paths->shared;
		fixed_count = 2;
	}
	for (size_t i = 0; i < fixed_count; i++) {
		status = 0;
		if (kind == ST_RIP3_RESOURCE_MEDIA && i == 0) {
			status = host_directory_status(paths);
			if (status == 0 && result == NULL) {
				const char *root = paths->named_host_root != NULL ? paths->named_host_root : paths->host;
				status = cache_bucket_status(root);
			}
		}
		else if (result == NULL)
			status = directory_status(fixed[i]);
		if (status == 0)
			status = lookup_directory(result, fixed[i], key, key_size, matched);
		if (status == 0 || status == IO_ERROR_OUT_OF_MEMORY || (matched != NULL && *matched))
			goto done;
		if (first_error == IO_ERROR_NOT_FOUND && status != IO_ERROR_NOT_FOUND
		    && status != IO_ERROR_NOT_DIRECTORY)
			first_error = status;
	}
	status = first_error;
done:
	free(key);
	return status;
}

int
st_rip3_resource_open(struct st_rip3_resource *result,
    const struct st_rip3_resource_paths *paths, enum st_rip3_resource_kind kind,
    const char *name)
{
	if (result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	return lookup_resource(result,paths,kind,name,NULL);
}

int
st_rip3_resource_open_first(struct st_rip3_resource *result, bool *matched,
    const struct st_rip3_resource_paths *paths, const char *name)
{
	if (matched == NULL) return IO_ERROR_INVALID_ARGUMENT;
	*matched = false;
	if (result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	return lookup_resource(result,paths,ST_RIP3_RESOURCE_MEDIA,name,matched);
}

int
st_rip3_resource_exists(const struct st_rip3_resource_paths *paths, const char *name)
{
	return lookup_resource(NULL, paths, ST_RIP3_RESOURCE_MEDIA, name, NULL);
}

int
st_rip3_resource_write(const struct st_rip3_resource_paths *paths,
    const char *name, const void *bytes, size_t size)
{
	struct st_rip3_resource_match match = {.writing=true};
	char *key = NULL;
	size_t key_size;
	int status;

	if (paths == NULL || !st_rip3_resource_name_valid(name) || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	/* A missing session directory is not authority to write into a shared
	 * lookup tier. Its selection/confinement belongs to client setup. */
	if (paths->host == NULL || paths->host[0] == 0)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_unicode_caseless_key(&key,&key_size,name,strlen(name));
	if (status != 0) return status;
	match.directory = paths->host; match.key = key; match.key_size = key_size;
	status = paths->named_host_root == NULL ? ensure_directory(paths->host)
	    : st_rip3_resource_prepare_cache(paths->named_host_root,paths->host);
	if (status == 0) status = st_rip3_resource_directory_scan(&match);
	if (status == 0 && match.path == NULL)
		status = path_join_alloc(&match.path,paths->host,name);
	/* Preserve an existing unique file's spelling and prepare the whole
	 * replacement before rename. Any failure retains its previous contents.
	 * The directory match rejects links and other non-file entries; this
	 * function never follows a host-named link or writes a fallback tier. */
	if (status == 0) status = fs_replace_file(match.path,bytes,size);
	free(match.path); free(key);
	return status;
}

int
st_rip3_resource_remove(const struct st_rip3_resource_paths *paths, const char *name)
{
	struct st_rip3_resource_match match = {.writing = true};
	char *key = NULL;

	if (paths == NULL || !st_rip3_resource_name_valid(name))
		return IO_ERROR_INVALID_ARGUMENT;
	int status = host_directory_status(paths);
	if (status != 0)
		return status;
	/* The BBS bucket was checked at connection time. Recheck it before
	 * removing a file in case the local cache was relocated while open. */
	const char *root = paths->named_host_root != NULL ? paths->named_host_root : paths->host;
	status = cache_bucket_status(root);
	if (status != 0)
		return status;
	status = st_unicode_caseless_key(&key, &match.key_size, name, strlen(name));
	if (status != 0)
		return status;
	match.directory = paths->host;
	match.key = key;
	status = st_rip3_resource_directory_scan(&match);
	if (status == 0)
		status = match.path == NULL ? IO_ERROR_NOT_FOUND : fs_remove_file(match.path);
	free(match.path);
	free(key);
	return status;
}
