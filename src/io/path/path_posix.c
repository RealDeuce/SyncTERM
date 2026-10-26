/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "path.h"

#include "io_error.h"
#include "path_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool
path_is_separator(char character)
{
	return character == '/';
}

bool
path_is_rooted(const char *path)
{
	return path != NULL && path[0] == '/';
}

size_t
syncterm_path_root_length(const char *path)
{
	return path[0] == '/' ? 1 : 0;
}

char
syncterm_path_separator(void)
{
	return '/';
}

static char *
path_current_directory(void)
{
	size_t size = 256;

	for (;;) {
		char *directory = malloc(size);
		if (directory == NULL)
			return NULL;
		if (getcwd(directory, size) != NULL)
			return directory;
		int error = errno;
		free(directory);
		if (error != ERANGE) {
			errno = error;
			return NULL;
		}
		if (size > SIZE_MAX / 2) {
			errno = ERANGE;
			return NULL;
		}
		size *= 2;
	}
}

static ptrdiff_t
path_normalize_absolute(char *result, size_t result_size, const char *value)
{
	size_t input_length = strlen(value);
	if (input_length > SIZE_MAX - 2)
		return IO_ERROR_OUT_OF_RANGE;
	char *normalized = malloc(input_length + 2);
	if (normalized == NULL)
		return IO_ERROR_OUT_OF_MEMORY;

	size_t used = 1;
	normalized[0] = '/';
	const char *current = value;
	while (*current == '/')
		current++;
	while (*current != '\0') {
		const char *component = current;
		while (*current != '\0' && *current != '/')
			current++;
		size_t component_length = (size_t)(current - component);
		if (component_length == 1 && component[0] == '.') {
			/* Nothing to add. */
		}
		else if (component_length == 2 && component[0] == '.' &&
		    component[1] == '.') {
			if (used > 1) {
				while (used > 1 && normalized[used - 1] != '/')
					used--;
				if (used > 1)
					used--;
			}
		}
		else if (component_length != 0) {
			if (used > 1 && normalized[used - 1] != '/')
				normalized[used++] = '/';
			memcpy(normalized + used, component, component_length);
			used += component_length;
		}
		while (*current == '/')
			current++;
	}
	normalized[used] = '\0';
	ptrdiff_t stored = syncterm_path_store(result, result_size,
	    normalized, used);
	free(normalized);
	return stored;
}

ptrdiff_t
path_make_absolute(char *result, size_t result_size, const char *path)
{
	char *combined;
	char *directory = NULL;
	size_t path_length;
	size_t directory_length = 0;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	path_length = strlen(path);
	if (path[0] == '/') {
		combined = malloc(path_length + 1);
		if (combined == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memcpy(combined, path, path_length + 1);
	}
	else {
		directory = path_current_directory();
		if (directory == NULL)
			return io_error_from_errno(errno);
		directory_length = strlen(directory);
		if (directory_length > SIZE_MAX - path_length - 2) {
			free(directory);
			return IO_ERROR_OUT_OF_RANGE;
		}
		combined = malloc(directory_length + path_length + 2);
		if (combined == NULL) {
			free(directory);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		memcpy(combined, directory, directory_length);
		combined[directory_length] = '/';
		memcpy(combined + directory_length + 1, path,
		    path_length + 1);
		free(directory);
	}
	ptrdiff_t normalized = path_normalize_absolute(result, result_size,
	    combined);
	free(combined);
	return normalized;
}

ptrdiff_t
path_expand_user(char *result, size_t result_size, const char *path)
{
	const char *home;
	size_t home_length;
	size_t suffix_offset;
	size_t suffix_length;
	bool omit_separator;
	char *expanded;

	if (path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (path[0] != '~' || (path[1] != '\0' && path[1] != '/'))
		return syncterm_path_store(result, result_size, path, strlen(path));
	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		return IO_ERROR_NOT_FOUND;
	home_length = strlen(home);
	suffix_offset = 1;
	omit_separator = home_length != 0 && home[home_length - 1] == '/' &&
	    path[1] == '/';
	if (omit_separator)
		suffix_offset++;
	suffix_length = strlen(path + suffix_offset);
	if (home_length > SIZE_MAX - suffix_length - 1)
		return IO_ERROR_OUT_OF_RANGE;
	expanded = malloc(home_length + suffix_length + 1);
	if (expanded == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(expanded, home, home_length);
	memcpy(expanded + home_length, path + suffix_offset,
	    suffix_length + 1);
	ptrdiff_t stored = syncterm_path_store(result, result_size, expanded,
	    home_length + suffix_length);
	free(expanded);
	return stored;
}
