/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm_apc_cache.h"
#include "filesystem.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool
cterm_apc_cache_ready(const char *directory)
{
	struct fs_info info;
	int status;

	if (directory == NULL || *directory == 0)
		return false;
	status = fs_get_link_info(&info, directory);
	if (status == IO_ERROR_NOT_FOUND) {
		status = fs_create_directories(directory);
		if (status == 0) status = fs_get_link_info(&info, directory);
	}
	return status == 0 && info.kind == FS_ENTRY_DIRECTORY;
}

bool
cterm_apc_cache_name_valid(const char *name, bool pattern, bool windows)
{
	const char *component = name;

	if (name == NULL || *name == 0)
		return false;
	for (const char *p = name;; ++p) {
		unsigned char ch = (unsigned char)*p;
		bool separator = ch == '/' || (windows && ch == '\\');

		if (separator || ch == 0) {
			size_t length = (size_t)(p - component);

			if (length == 0 || (length == 1 && component[0] == '.')
			    || (length == 2 && memcmp(component, "..", 2) == 0))
				return false;
			if (windows) {
				char base[5] = {0};

				if (p[-1] == '.' || p[-1] == ' ')
					return false;
				for (size_t i = 0; i < 4 && i < length && component[i] != '.'; ++i)
					base[i] = component[i] >= 'a' && component[i] <= 'z'
					    ? (char)(component[i] - 'a' + 'A') : component[i];
				if (strcmp(base, "CON") == 0 || strcmp(base, "PRN") == 0
				    || strcmp(base, "AUX") == 0 || strcmp(base, "NUL") == 0
				    || ((memcmp(base, "COM", 3) == 0 || memcmp(base, "LPT", 3) == 0)
				    && base[3] >= '1' && base[3] <= '9'
				    && (length == 4 || component[4] == '.')))
					return false;
			}
			if (ch == 0) break;
			component = p + 1;
		}
		else if (ch < 0x20 || ch == 0x7f
		    || (windows && (strchr("<>:\"|", ch) != NULL
		    || (!pattern && (ch == '*' || ch == '?')))))
			return false;
	}
	return true;
}

int
cterm_apc_cache_add_entry(struct cterm_apc_cache_entries *entries,
	const char *name, bool directory)
{
	size_t length = strlen(name);
	char *copy;

	if (entries->count == entries->capacity) {
		size_t capacity = entries->capacity == 0 ? 16 : entries->capacity * 2;
		char **names;

		if (capacity < entries->capacity || capacity > SIZE_MAX / sizeof(*names))
			return IO_ERROR_OUT_OF_RANGE;
		names = realloc(entries->names, capacity * sizeof(*names));
		if (names == NULL) return IO_ERROR_OUT_OF_MEMORY;
		entries->names = names;
		entries->capacity = capacity;
	}
	if (length > SIZE_MAX - 2) return IO_ERROR_OUT_OF_RANGE;
	copy = malloc(length + 2);
	if (copy == NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy, name, length);
	if (directory) copy[length++] = '/';
	copy[length] = 0;
	entries->names[entries->count++] = copy;
	return 0;
}

void
cterm_apc_cache_entries_free(struct cterm_apc_cache_entries *entries)
{
	for (size_t i = 0; i < entries->count; ++i) free(entries->names[i]);
	free(entries->names);
	*entries = (struct cterm_apc_cache_entries){0};
}

int
cterm_apc_cache_read(void **result, size_t *size,
	const char *directory, const char *name)
{
	FILE *stream = cterm_apc_cache_open(directory, name, false);
	void *data;
	int64_t length;
	int status = 0;

	if (stream == NULL) return IO_ERROR_NOT_FOUND;
	length = fs_stream_size(stream);
	if (length < 0 || (uint64_t)length >= SIZE_MAX) {
		(void)fclose(stream);
		return length < 0 ? (int)length : IO_ERROR_OUT_OF_RANGE;
	}
	data = malloc((size_t)length + 1);
	if (data == NULL) status = IO_ERROR_OUT_OF_MEMORY;
	else if (fread(data, 1, (size_t)length, stream) != (size_t)length)
		status = IO_ERROR_IO;
	if (fclose(stream) != 0 && status == 0) status = IO_ERROR_IO;
	if (status != 0) { free(data); return status; }
	*result = data;
	*size = (size_t)length;
	return 0;
}
