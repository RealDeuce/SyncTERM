/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "dynamic_library.h"

#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

struct dynamic_library {
	HMODULE handle;
};

static bool
dynamic_library_name_valid(const char *name)
{
	const unsigned char *current;

	if (name == NULL || *name == '\0')
		return false;
	for (current = (const unsigned char *)name; *current != '\0'; current++) {
		if (*current > 0x7f || *current == '/' || *current == '\\')
			return false;
	}
	return true;
}

int
dynamic_library_open(dynamic_library_t **result, const char *const names[],
    size_t name_count, unsigned preferred_major,
    enum dynamic_library_visibility visibility)
{
	dynamic_library_t *library;
	size_t maximum_name = 0;
	size_t i;
	wchar_t *filename;
	HMODULE handle = NULL;

	(void)preferred_major;
	if (result == NULL || names == NULL || name_count == 0 ||
	    (visibility != DYNAMIC_LIBRARY_LOCAL &&
	    visibility != DYNAMIC_LIBRARY_GLOBAL))
		return IO_ERROR_INVALID_ARGUMENT;
	for (i = 0; i < name_count; i++) {
		size_t length;

		if (!dynamic_library_name_valid(names[i]))
			return IO_ERROR_INVALID_ARGUMENT;
		length = strlen(names[i]);
		if (length > maximum_name)
			maximum_name = length;
	}
	if (maximum_name > SIZE_MAX / sizeof(*filename) - 5)
		return IO_ERROR_OUT_OF_RANGE;
	filename = malloc((maximum_name + 5) * sizeof(*filename));
	if (filename == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (i = 0; i < name_count && handle == NULL; i++) {
		size_t j;

		for (j = 0; names[i][j] != '\0'; j++)
			filename[j] = (wchar_t)(unsigned char)names[i][j];
		filename[j++] = L'.';
		filename[j++] = L'd';
		filename[j++] = L'l';
		filename[j++] = L'l';
		filename[j] = L'\0';
		handle = LoadLibraryW(filename);
	}
	free(filename);
	if (handle == NULL)
		return IO_ERROR_NOT_FOUND;
	library = malloc(sizeof(*library));
	if (library == NULL) {
		(void)FreeLibrary(handle);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	library->handle = handle;
	*result = library;
	return 0;
}

void *
dynamic_library_symbol(const dynamic_library_t *library, const char *name)
{
	FARPROC native_symbol;

	if (library == NULL || name == NULL || *name == '\0')
		return NULL;
	native_symbol = GetProcAddress(library->handle, name);
	if (native_symbol == NULL)
		return NULL;
	return (void *)native_symbol;
}

int
dynamic_library_close(dynamic_library_t *library)
{
	int result;

	if (library == NULL)
		return 0;
	result = FreeLibrary(library->handle) ? 0 : IO_ERROR_IO;
	free(library);
	return result;
}
