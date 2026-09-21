/* UTF-8 filesystem operations used by SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */


#ifndef SYNCTERM_FILESYSTEM_H_
#define SYNCTERM_FILESYSTEM_H_

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>

enum fs_entry_kind {
	FS_ENTRY_FILE,
	FS_ENTRY_DIRECTORY,
	FS_ENTRY_LINK,
	FS_ENTRY_OTHER,
};

struct fs_info {
	enum fs_entry_kind kind;
	/* Defined for FS_ENTRY_FILE and zero for every other kind. */
	uint64_t size;
	/* Whole seconds from the Unix epoch. */
	int64_t modified_time;
};

/*
 * Paths are UTF-8. Windows uses exact extended-namespace names internally;
 * callers never pass or receive a Windows namespace prefix.
 *
 * Result objects are unchanged on failure. fs_get_info() follows the final
 * link; fs_get_link_info() reports the final link itself. FS_ENTRY_LINK also
 * covers Windows reparse points.
 */
int fs_get_info(struct fs_info *result, const char *path);
int fs_get_link_info(struct fs_info *result, const char *path);
int fs_open(FILE **result, const char *path, const char *mode);

/* Returns a byte size or a negative IO_ERROR_* value without seeking. */
int64_t fs_stream_size(FILE *stream);
int fs_stream_truncate(FILE *stream, uint64_t size);
int fs_set_modified_time(const char *path, int64_t modified_time);

int fs_remove_file(const char *path);
int fs_remove_directory(const char *path);
/* Replaces an existing non-directory destination; never copies volumes. */
int fs_rename(const char *source, const char *destination);
/* Overwrites destination and preserves permission mode and modified time. */
int fs_copy_file(const char *source, const char *destination);
/* Single creation reports IO_ERROR_ALREADY_EXISTS. */
int fs_create_directory(const char *path);
/* Existing directory components, including the final one, are successful. */
int fs_create_directories(const char *path);
/* Creates the parent hierarchy of a file path. */
int fs_create_parent_directories(const char *path);

#endif
