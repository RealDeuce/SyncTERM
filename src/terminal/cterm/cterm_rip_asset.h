/* Confined RIP scene and icon file access. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_ASSET_H_
#define CTERM_RIP_ASSET_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* The returned path is allocated. Invalid remote names return NULL. */
char *cterm_rip_asset_path(const char *directory, const char *filename,
    bool append_icon_extension);
/* Resolve a download destination or upload source, with DOS case lookup.
 * The root must be a real directory; existing entries must be regular files.
 * An absent file is allowed only for a destination. */
int cterm_rip_asset_transfer_path(char **result, const char *directory,
    const char *filename, bool upload);
/* Only regular files in the real asset directory are opened. */
FILE *cterm_rip_asset_open_read(const char *directory,
    const char *filename, bool append_icon_extension);
/* Query metadata and read access for a file or directory within the real
 * asset directory, using DOS-style case-insensitive lookup. Links are not
 * followed. Invalid names are distinct from absent entries. */
int cterm_rip_asset_get_info(uint64_t *size, int64_t *modified_time,
    bool *readable, const char *directory, const char *filename);

#endif
