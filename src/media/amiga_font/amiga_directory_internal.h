/* Platform directory names only; font policy stays in the store. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_AMIGA_DIRECTORY_INTERNAL_H_
#define ST_AMIGA_DIRECTORY_INTERNAL_H_
#include <stddef.h>
struct amiga_names {
	char **items;
	size_t count;
};
int amiga_directory_names(struct amiga_names *result, const char *path);
void amiga_names_dispose(struct amiga_names *names);
int amiga_names_append(struct amiga_names *names, const char *name);
#endif
