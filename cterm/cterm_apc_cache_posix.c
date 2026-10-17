/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm_apc_cache.h"
#include "io_error.h"
#include "path.h"
#include "string_compat.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int
open_directory(const char *directory, const char *relative, bool create)
{
	char *copy = strdup(relative), *component, *next;
	int fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

	if (copy == NULL || fd < 0) { free(copy); if (fd >= 0) close(fd); return -1; }
	for (component = copy; *component != 0; component = next) {
		int child;

		next = strchr(component, '/');
		if (next != NULL) *next++ = 0;
		else next = component + strlen(component);
		if (create && mkdirat(fd, component, 0777) != 0 && errno != EEXIST) {
			close(fd); fd = -1; break;
		}
		child = openat(fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(fd);
		fd = child;
		if (fd < 0) break;
	}
	free(copy);
	return fd;
}

FILE *
cterm_apc_cache_open(const char *directory, const char *name, bool write)
{
	char *copy, *slash, *basename;
	int parent, fd;
	struct stat info;
	FILE *stream;

	if (!cterm_apc_cache_name_valid(name, false, false)) return NULL;
	copy = strdup(name);
	if (copy == NULL) return NULL;
	slash = strrchr(copy, '/');
	if (slash != NULL) { *slash = 0; basename = slash + 1; }
	else basename = copy;
	parent = open_directory(directory, slash != NULL ? copy : "", write);
	if (parent < 0) { free(copy); return NULL; }
	fd = openat(parent, basename, O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK
	    | (write ? O_WRONLY | O_CREAT : O_RDONLY), 0666);
	close(parent);
	free(copy);
	if (fd < 0) return NULL;
	if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)
	    || (write && ftruncate(fd, 0) != 0)) { close(fd); return NULL; }
	stream = fdopen(fd, write ? "wb" : "rb");
	if (stream == NULL) close(fd);
	return stream;
}

struct pending_directory {
	char *prefix;
	const char *pattern;
	struct pending_directory *next;
};

static int
compare_entries(const void *left, const void *right)
{
	return strcoll(*(const char *const *)left, *(const char *const *)right);
}

int
cterm_apc_cache_list(struct cterm_apc_cache_entries *result,
	const char *directory, const char *pattern)
{
	struct cterm_apc_cache_entries entries = {0};
	struct pending_directory *pending;
	int status = 0;

	if (!cterm_apc_cache_name_valid(pattern, true, false))
		return IO_ERROR_INVALID_ARGUMENT;
	pending = calloc(1, sizeof(*pending));
	if (pending == NULL) return IO_ERROR_OUT_OF_MEMORY;
	pending->prefix = strdup("");
	pending->pattern = pattern;
	if (pending->prefix == NULL) { free(pending); return IO_ERROR_OUT_OF_MEMORY; }
	while (pending != NULL && status == 0) {
		struct pending_directory *current = pending;
		const char *slash = strchr(current->pattern, '/');
		size_t length = slash == NULL ? strlen(current->pattern)
		    : (size_t)(slash - current->pattern);
		char *part = strndup(current->pattern, length);
		int fd = open_directory(directory, current->prefix, false);
		DIR *stream = fd < 0 ? NULL : fdopendir(fd);
		struct dirent *entry;

		pending = current->next;
		if (stream == NULL && fd >= 0) close(fd);
		if (part == NULL) status = IO_ERROR_OUT_OF_MEMORY;
		while (status == 0 && stream != NULL && (entry = readdir(stream)) != NULL) {
			struct stat info;
			char *relative = NULL;

			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0
			    || fnmatch(part, entry->d_name, FNM_PERIOD) != 0
			    || fstatat(dirfd(stream), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0
			    || S_ISLNK(info.st_mode)) continue;
			status = path_join_alloc(&relative, current->prefix, entry->d_name);
			if (status != 0) break;
			if (slash == NULL) {
				status = cterm_apc_cache_add_entry(&entries, relative, S_ISDIR(info.st_mode));
				free(relative);
			}
			else if (S_ISDIR(info.st_mode)) {
				struct pending_directory *child = malloc(sizeof(*child));
				if (child == NULL) { free(relative); status = IO_ERROR_OUT_OF_MEMORY; break; }
				*child = (struct pending_directory){relative, slash + 1, pending};
				pending = child;
			}
			else free(relative);
		}
		if (stream != NULL) closedir(stream);
		free(part); free(current->prefix); free(current);
	}
	while (pending != NULL) {
		struct pending_directory *next = pending->next;
		free(pending->prefix); free(pending); pending = next;
	}
	if (status != 0) { cterm_apc_cache_entries_free(&entries); return status; }
	qsort(entries.names, entries.count, sizeof(*entries.names), compare_entries);
	*result = entries;
	return 0;
}
