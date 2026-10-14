/* Case-insensitive lookup of a RIP asset inside one directory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_ASSET_LOOKUP_H_
#define CTERM_RIP_ASSET_LOOKUP_H_

#include <stdbool.h>

/* Returns an allocated path, or NULL if no matching entry is found. The
 * caller validates the entry kind and rejects links. */
char *cterm_rip_asset_case_path(const char *directory, const char *name);
bool cterm_rip_asset_path_readable(const char *path);

#endif
