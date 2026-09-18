/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "dynamic_library.h"

#include "io_error.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dynamic_library {
	void *handle;
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

static void *
dynamic_library_try_so(char *filename, size_t size, const char *name,
    unsigned major, bool versioned, bool prefixed, int flags)
{
	if (versioned)
		(void)snprintf(filename, size, "lib%s.so.%u", name, major);
	else if (prefixed)
		(void)snprintf(filename, size, "lib%s.so", name);
	else
		(void)snprintf(filename, size, "%s.so", name);
	return dlopen(filename, flags);
}

#if defined(SYNCTERM_DYNAMIC_LIBRARY_DYLIB_NAMES)
static void *
dynamic_library_try_dylib(char *filename, size_t size, const char *name,
    unsigned major, bool versioned, int flags)
{
	if (versioned)
		(void)snprintf(filename, size, "lib%s.%u.dylib", name, major);
	else
		(void)snprintf(filename, size, "lib%s.dylib", name);
	return dlopen(filename, flags);
}
#endif

int
dynamic_library_open(dynamic_library_t **result, const char *const names[],
    size_t name_count, unsigned preferred_major,
    enum dynamic_library_visibility visibility)
{
	dynamic_library_t *library;
	size_t maximum_name = 0;
	size_t i;
	unsigned version;
	char *filename;
	void *handle = NULL;
	int flags;

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
	if (maximum_name > SIZE_MAX - 64)
		return IO_ERROR_OUT_OF_RANGE;
	filename = malloc(maximum_name + 64);
	if (filename == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	flags = RTLD_NOW;
	flags |= visibility == DYNAMIC_LIBRARY_GLOBAL ? RTLD_GLOBAL : RTLD_LOCAL;
	for (i = 0; i < name_count && handle == NULL; i++) {
		handle = dynamic_library_try_so(filename, maximum_name + 64,
		    names[i], preferred_major, true, true, flags);
		if (handle == NULL)
			handle = dynamic_library_try_so(filename, maximum_name + 64,
			    names[i], 0, false, true, flags);
		if (handle == NULL)
			handle = dynamic_library_try_so(filename, maximum_name + 64,
			    names[i], 0, false, false, flags);
		for (version = preferred_major; handle == NULL && version > 0; ) {
			version--;
			handle = dynamic_library_try_so(filename,
			    maximum_name + 64, names[i], version, true, true, flags);
		}
#if defined(SYNCTERM_DYNAMIC_LIBRARY_DYLIB_NAMES)
		if (handle == NULL)
			handle = dynamic_library_try_dylib(filename,
			    maximum_name + 64, names[i], preferred_major, true, flags);
		if (handle == NULL)
			handle = dynamic_library_try_dylib(filename,
			    maximum_name + 64, names[i], 0, false, flags);
		for (version = preferred_major; handle == NULL && version > 0; ) {
			version--;
			handle = dynamic_library_try_dylib(filename,
			    maximum_name + 64, names[i], version, true, flags);
		}
#endif
	}
	free(filename);
	if (handle == NULL)
		return IO_ERROR_NOT_FOUND;
	library = malloc(sizeof(*library));
	if (library == NULL) {
		(void)dlclose(handle);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	library->handle = handle;
	*result = library;
	return 0;
}

void *
dynamic_library_symbol(const dynamic_library_t *library, const char *name)
{
	if (library == NULL || name == NULL || *name == '\0')
		return NULL;
	return dlsym(library->handle, name);
}

int
dynamic_library_close(dynamic_library_t *library)
{
	int result;

	if (library == NULL)
		return 0;
	result = dlclose(library->handle) == 0 ? 0 : IO_ERROR_IO;
	free(library);
	return result;
}
