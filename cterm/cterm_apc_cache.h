/* CTerm-private, confined APC cache access. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef CTERM_APC_CACHE_H_
#define CTERM_APC_CACHE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct cterm_apc_cache_entries {
	char **names;
	size_t count;
	size_t capacity;
};

bool cterm_apc_cache_ready(const char *directory);
/* Platform implementation anchors each component without following links.
 * Write creates parent directories and truncates only regular files. */
FILE *cterm_apc_cache_open(const char *directory, const char *name, bool write);
int cterm_apc_cache_list(struct cterm_apc_cache_entries *result,
	const char *directory, const char *pattern);
int cterm_apc_cache_read(void **result, size_t *size,
	const char *directory, const char *name);
void cterm_apc_cache_entries_free(struct cterm_apc_cache_entries *entries);

/* Shared helpers for the platform implementations. */
bool cterm_apc_cache_name_valid(const char *name, bool pattern, bool windows);
int cterm_apc_cache_add_entry(struct cterm_apc_cache_entries *entries,
	const char *name, bool directory);

#endif
