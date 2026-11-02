/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm_apc_cache.h"
#include "io_error.h"
#include "path.h"
#include "path_windows.h"
#include "st_windows_utf.h"
#include "string_compat.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct cache_path {
	HANDLE *parents;
	size_t count;
	wchar_t *file;
};

static void
release_path(struct cache_path *path)
{
	for (size_t i = 0; i < path->count; ++i) CloseHandle(path->parents[i]);
	free(path->parents); free(path->file);
	*path = (struct cache_path){0};
}

static bool
lock_directory(struct cache_path *path, const char *name, bool create)
{
	wchar_t *wide = NULL;
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE handle;

	if (path_windows_extended(&wide, name) != 0) return false;
	if (create && !CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		free(wide); return false;
	}
	/* Omitting FILE_SHARE_DELETE pins each ancestor until the final file
	 * is open, preventing a checked directory from being exchanged for
	 * a junction between validation and use. */
	handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	free(wide);
	if (handle == INVALID_HANDLE_VALUE) return false;
	if (!GetFileInformationByHandle(handle, &info)
	    || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
	    || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
		CloseHandle(handle); return false;
	}
	path->parents[path->count++] = handle;
	return true;
}

static bool
resolve_path(struct cache_path *result, const char *directory,
	const char *name, bool write, bool pattern)
{
	struct cache_path path = {0};
	char *copy, *prefix, *component;
	size_t capacity = 1;
	bool success = false;

	if (!cterm_apc_cache_name_valid(name, pattern, true)) return false;
	for (const char *p = name; *p; ++p)
		if (*p == '/' || *p == '\\') ++capacity;
	if (capacity > SIZE_MAX / sizeof(HANDLE)) return false;
	path.parents = malloc(capacity * sizeof(HANDLE));
	copy = strdup(name); prefix = strdup(directory);
	if (path.parents == NULL || copy == NULL || prefix == NULL) goto done;
	if (!lock_directory(&path, directory, false)) goto done;
	for (char *p = copy; *p; ++p) if (*p == '\\') *p = '/';
	component = copy;
	for (;;) {
		char *slash = strchr(component, '/');
		char *next = NULL;

		if (slash != NULL) *slash = 0;
		if (path_join_alloc(&next, prefix, component) != 0) goto done;
		free(prefix); prefix = next;
		if (slash == NULL) break;
		if (!lock_directory(&path, prefix, write)) goto done;
		component = slash + 1;
	}
	if (path_windows_extended(&path.file, prefix) != 0) goto done;
	*result = path;
	path = (struct cache_path){0};
	success = true;
done:
	free(copy); free(prefix); release_path(&path);
	return success;
}

FILE *
cterm_apc_cache_open(const char *directory, const char *name, bool write)
{
	struct cache_path path = {0};
	BY_HANDLE_FILE_INFORMATION info;
	LARGE_INTEGER zero = {0};
	HANDLE handle;
	int descriptor;
	FILE *stream;

	if (!resolve_path(&path, directory, name, write, false)) return NULL;
	handle = CreateFileW(path.file, write ? GENERIC_WRITE : GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    write ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	release_path(&path);
	if (handle == INVALID_HANDLE_VALUE) return NULL;
	if (GetFileType(handle) != FILE_TYPE_DISK
	    || !GetFileInformationByHandle(handle, &info)
	    || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0
	    || (write && (!SetFilePointerEx(handle, zero, NULL, FILE_BEGIN) || !SetEndOfFile(handle)))) {
		CloseHandle(handle); return NULL;
	}
	descriptor = _open_osfhandle((intptr_t)handle, _O_BINARY | (write ? _O_WRONLY : _O_RDONLY));
	if (descriptor < 0) { CloseHandle(handle); return NULL; }
	stream = _fdopen(descriptor, write ? "wb" : "rb");
	if (stream == NULL) _close(descriptor);
	return stream;
}

static int
compare_entries(const void *left, const void *right)
{
	return strcmp(*(const char *const *)left, *(const char *const *)right);
}

int
cterm_apc_cache_list(struct cterm_apc_cache_entries *result,
	const char *directory, const char *pattern)
{
	struct cterm_apc_cache_entries entries = {0};
	struct cache_path path = {0};
	WIN32_FIND_DATAW entry;
	HANDLE search;
	char *prefix, *slash;
	int status = 0;

	if (!cterm_apc_cache_name_valid(pattern, true, true)) return IO_ERROR_INVALID_ARGUMENT;
	if (!resolve_path(&path, directory, pattern, false, true)) {
		*result = entries;
		return 0;
	}
	prefix = strdup(pattern);
	if (prefix == NULL) { release_path(&path); return IO_ERROR_OUT_OF_MEMORY; }
	for (char *p = prefix; *p; ++p) if (*p == '\\') *p = '/';
	slash = strrchr(prefix, '/');
	if (slash != NULL) slash[1] = 0; else *prefix = 0;
	search = FindFirstFileW(path.file, &entry);
	if (search != INVALID_HANDLE_VALUE) {
		do {
			char *name = NULL, *relative = NULL;

			if (entry.cFileName[0] == L'.'
			    || (entry.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
				continue;
			status = st_windows_utf16_to_utf8(&name, entry.cFileName);
			if (status == 0 && !cterm_apc_cache_name_valid(name, false, true)) {
				free(name);
				continue;
			}
			if (status == 0) status = path_join_alloc(&relative, prefix, name);
			if (status == 0) status = cterm_apc_cache_add_entry(&entries, relative,
			    (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
			free(relative); free(name);
		} while (status == 0 && FindNextFileW(search, &entry));
		FindClose(search);
	}
	free(prefix); release_path(&path);
	if (status != 0) { cterm_apc_cache_entries_free(&entries); return status; }
	qsort(entries.names, entries.count, sizeof(*entries.names), compare_entries);
	*result = entries;
	return 0;
}
