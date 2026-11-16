/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_RESOURCES_INTERNAL_H_
#define ST_RIP3_RESOURCES_INTERNAL_H_

#include <stddef.h>

struct st_rip3_resource_match {
	const char *directory;
	const char *key;
	size_t key_size;
	char *path;
};

/* Platform code enumerates names only. Matching and regular-file policy
 * belong to the shared implementation. The caller owns match->path. */
int st_rip3_resource_consider(struct st_rip3_resource_match *match,
    const char *name);
int st_rip3_resource_directory_scan(struct st_rip3_resource_match *match);

#endif
