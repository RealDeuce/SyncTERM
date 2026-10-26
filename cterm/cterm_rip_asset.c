/* Confined RIP scene and icon file access. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_asset.h"

#include "cterm_rip_asset_lookup.h"
#include "filesystem.h"
#include "io_error.h"
#include "path.h"

#include "string_util.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *
cterm_rip_asset_path(const char *directory, const char *filename,
    bool append_icon_extension)
{
	char *name = NULL;
	char *result = NULL;
	size_t length;
	char base[5] = {0};

	if (directory == NULL
	    || !path_untrusted_component_valid(filename)
	    || strstr(filename, "..") != NULL
	    || strpbrk(filename, "<>:\"|?*") != NULL)
		return NULL;
	length = strlen(filename);
	if (filename[length - 1] == '.' || filename[length - 1] == ' ')
		return NULL;
	for (const unsigned char *byte = (const unsigned char *)filename;
	    *byte != 0; byte++) {
		if (*byte < 0x20 || *byte == 0x7f)
			return NULL;
	}
	for (size_t i = 0; i < 4 && i < length && filename[i] != '.'; i++)
		base[i] = (char)ascii_to_upper((unsigned char)filename[i]);
	if (strcmp(base, "CON") == 0 || strcmp(base, "PRN") == 0
	    || strcmp(base, "AUX") == 0 || strcmp(base, "NUL") == 0
	    || ((memcmp(base, "COM", 3) == 0
	        || memcmp(base, "LPT", 3) == 0)
	        && base[3] >= '1' && base[3] <= '9'
	        && (filename[4] == '\0' || filename[4] == '.')))
		return NULL;
	if (append_icon_extension && strchr(filename, '.') == NULL) {
		if (length > SIZE_MAX - 5U)
			return NULL;
		name = malloc(length + 5U);
		if (name == NULL)
			return NULL;
		memcpy(name, filename, length);
		memcpy(name + length, ".ICN", 5U);
	}
	if (path_join_alloc(&result, directory,
	    name == NULL ? filename : name) != 0)
		result = NULL;
	free(name);
	return result;
}

static int
resolve_read_path(char **result, struct fs_info *info,
    const char *directory, const char *filename,
    bool append_icon_extension, bool allow_directory)
{
	char *path = cterm_rip_asset_path(directory, filename,
	    append_icon_extension);
	int status;

	if (path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_get_link_info(info, directory);
	if (status != 0 || info->kind != FS_ENTRY_DIRECTORY) {
		free(path);
		return status == 0 ? IO_ERROR_NOT_FOUND : status;
	}
	status = fs_get_link_info(info, path);
	if (status == IO_ERROR_NOT_FOUND) {
		char *found = cterm_rip_asset_case_path(directory,
		    path_basename(path));

		free(path);
		path = found;
		if (path == NULL)
			return IO_ERROR_NOT_FOUND;
		status = fs_get_link_info(info, path);
	}
	if (status != 0 || (info->kind != FS_ENTRY_FILE
	    && !(allow_directory && info->kind == FS_ENTRY_DIRECTORY))) {
		free(path);
		return status == 0 ? IO_ERROR_NOT_FOUND : status;
	}
	*result = path;
	return 0;
}

int
cterm_rip_asset_transfer_path(char **result, const char *directory,
    const char *filename, bool upload)
{
	struct fs_info info;
	char *path = cterm_rip_asset_path(directory, filename, false);
	int status;

	if (path == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = fs_get_link_info(&info, directory);
	if (status == 0 && info.kind != FS_ENTRY_DIRECTORY)
		status = IO_ERROR_PERMISSION_DENIED;
	if (status != 0) { free(path); return status; }
	status = fs_get_link_info(&info, path);
	if (status == IO_ERROR_NOT_FOUND) {
		char *found = cterm_rip_asset_case_path(directory, filename);

		if (found != NULL) {
			free(path); path = found;
			status = fs_get_link_info(&info, path);
		}
	}
	if (status == 0 && info.kind != FS_ENTRY_FILE)
		status = IO_ERROR_PERMISSION_DENIED;
	if (status == IO_ERROR_NOT_FOUND && !upload)
		status = 0;
	if (status != 0) { free(path); return status; }
	*result = path;
	return 0;
}

FILE *
cterm_rip_asset_open_read(const char *directory, const char *filename,
    bool append_icon_extension)
{
	struct fs_info info;
	char *path;
	FILE *stream = NULL;

	if (resolve_read_path(&path, &info, directory, filename,
	    append_icon_extension, false) != 0)
		return NULL;
	(void)fs_open(&stream, path, "rb");
	free(path);
	return stream;
}

int
cterm_rip_asset_get_info(uint64_t *size, int64_t *modified_time,
    bool *readable, const char *directory, const char *filename)
{
	struct fs_info info;
	char *path;
	int status;

	if (size == NULL || modified_time == NULL || readable == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = resolve_read_path(&path, &info, directory, filename,
	    false, true);
	if (status != 0)
		return status;
	*readable = cterm_rip_asset_path_readable(path);
	free(path);
	*size = info.size;
	*modified_time = info.modified_time;
	return 0;
}
