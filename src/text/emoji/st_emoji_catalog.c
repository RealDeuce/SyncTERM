/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_emoji_catalog.h"
#include <string.h>

size_t
st_emoji_find(const char *text)
{
	if (text != NULL)
		for (size_t i = 0; i < st_emoji_entry_count; i++)
			if (strcmp(text, st_emoji_entries[i].text) == 0)
				return i;
	return st_emoji_entry_count;
}
