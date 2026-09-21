/* Shared ownership helpers for the Wren-host directory inventory. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_directory.h"

#include <stdlib.h>

void
st_wren_directory_entries_dispose(struct st_wren_directory_entries *entries)
{
	if (entries == NULL)
		return;
	for (size_t i = 0; i < entries->count; ++i)
		free(entries->items[i].name);
	free(entries->items);
	*entries = (struct st_wren_directory_entries){0};
}
