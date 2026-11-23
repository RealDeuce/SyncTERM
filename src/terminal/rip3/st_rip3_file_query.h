/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_FILE_QUERY_H_
#define ST_RIP3_FILE_QUERY_H_

#include <stddef.h>
#include <stdint.h>

struct st_rip3_resource_paths;

/* Return an owned, NUL-terminated engine reply, or NULL for a silent
 * outcome. Length excludes the NUL. The parser owns diagnostics and recovery. */
int st_rip3_file_query(const struct st_rip3_resource_paths *paths, unsigned mode,
	const uint8_t *filename, size_t length, char **reply, size_t *reply_length);

#endif
