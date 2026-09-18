/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "path.h"

#include "io_error.h"
#include "path_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

ptrdiff_t
syncterm_path_store(char *result, size_t result_size, const char *value,
    size_t length)
{
	if (result == NULL && result_size != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length > PTRDIFF_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (result != NULL && result_size > length) {
		memmove(result, value, length);
		result[length] = '\0';
	}
	return (ptrdiff_t)length;
}

const char *
path_basename(const char *path)
{
	const char *base;

	if (path == NULL)
		return NULL;
	base = path;
	for (const char *current = path; *current != '\0'; current++) {
		if (path_is_separator(*current))
			base = current + 1;
	}
	return base;
}

const char *
path_untrusted_basename(const char *path)
{
	const char *base;

	if (path == NULL)
		return NULL;
	base = path;
	for (const char *current = path; *current != '\0'; current++) {
		if (*current == '/' || *current == '\\')
			base = current + 1;
	}
	return base;
}

bool
path_untrusted_component_valid(const char *component)
{
	if (component == NULL || component[0] == '\0'
	    || strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
		return false;
	return strchr(component, '/') == NULL && strchr(component, '\\') == NULL;
}

int
path_join_alloc(char **result, const char *directory, const char *name)
{
	char *joined;
	size_t directory_length;
	size_t name_length;
	size_t overhead;
	bool separator;

	if (result == NULL || directory == NULL || name == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	directory_length = strlen(directory);
	name_length = strlen(name);
	separator = directory_length != 0
	    && !path_is_separator(directory[directory_length - 1]);
	overhead = separator ? 2U : 1U;
	if (directory_length > SIZE_MAX - overhead
	    || name_length > SIZE_MAX - overhead - directory_length)
		return IO_ERROR_OUT_OF_RANGE;
	joined = malloc(directory_length + name_length + overhead);
	if (joined == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(joined, directory, directory_length);
	if (separator)
		joined[directory_length++] = syncterm_path_separator();
	memcpy(joined + directory_length, name, name_length + 1);
	*result = joined;
	return 0;
}

ptrdiff_t
path_parent(char *result, size_t result_size, const char *path)
{
	const char current_directory[] = ".";
	size_t length;
	size_t root_length;
	size_t end;
	size_t separator;

	if (path == NULL || *path == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	length = strlen(path);
	root_length = syncterm_path_root_length(path);
	end = length;
	while (end > root_length && path_is_separator(path[end - 1]))
		end--;
	if (end == root_length && root_length != 0)
		return syncterm_path_store(result, result_size, path, root_length);
	separator = end;
	while (separator > root_length && !path_is_separator(path[separator - 1]))
		separator--;
	if (separator == root_length) {
		if (root_length != 0)
			return syncterm_path_store(result, result_size, path,
			    root_length);
		return syncterm_path_store(result, result_size,
		    current_directory, sizeof(current_directory) - 1);
	}
	end = separator - 1;
	while (end > root_length && path_is_separator(path[end - 1]))
		end--;
	if (end < root_length)
		end = root_length;
	return syncterm_path_store(result, result_size, path, end);
}

ptrdiff_t
path_append_separator(char *result, size_t result_size, const char *path)
{
	size_t length;
	size_t required;
	char separator;

	if (path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	length = strlen(path);
	separator = syncterm_path_separator();
	required = length;
	if (length != 0 && !path_is_separator(path[length - 1])) {
		if (length == SIZE_MAX)
			return IO_ERROR_OUT_OF_RANGE;
		required++;
	}
	if (required > PTRDIFF_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (result == NULL && result_size != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (result != NULL && result_size > required) {
		if (result != path)
			memmove(result, path, length);
		if (length != 0 && path_is_separator(path[length - 1]))
			result[length - 1] = separator;
		else if (required != length)
			result[length] = separator;
		result[required] = '\0';
	}
	return (ptrdiff_t)required;
}
