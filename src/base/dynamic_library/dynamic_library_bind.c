/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "dynamic_library.h"

static bool
bind_imports(dynamic_library_t *library, const struct dynamic_library_import imports[], size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		void *symbol = dynamic_library_symbol(library, imports[i].name);
		if (!symbol || !imports[i].destination || imports[i].size != sizeof(symbol))
			return false;
		memcpy(imports[i].destination, &symbol, sizeof(symbol));
	}
	return true;
}

bool
dynamic_library_bind(const char *const names[], size_t name_count, unsigned preferred_major,
    const struct dynamic_library_import imports[], size_t count)
{
	if (!names || !imports || !count)
		return false;
	for (size_t i = 0; i < name_count; ++i) {
		dynamic_library_t *library;
		if (dynamic_library_open(&library, &names[i], 1, preferred_major, DYNAMIC_LIBRARY_LOCAL))
			continue;
		if (bind_imports(library, imports, count))
			return true;
		dynamic_library_close(library);
	}
	return false;
}
