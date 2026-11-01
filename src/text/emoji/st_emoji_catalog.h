/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_EMOJI_CATALOG_H
#define ST_EMOJI_CATALOG_H
#include <stddef.h>

struct st_emoji_entry {
	const char *text;
	const char *name;
	size_t base;
};
extern const struct st_emoji_entry st_emoji_entries[];
extern const size_t st_emoji_entry_count;
/* Exact canonical sequence, or st_emoji_entry_count when absent. */
size_t st_emoji_find(const char *text);
#endif
