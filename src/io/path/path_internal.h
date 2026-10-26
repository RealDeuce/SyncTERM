/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_PATH_INTERNAL_H_
#define SYNCTERM_PATH_INTERNAL_H_

#include <stddef.h>

size_t syncterm_path_root_length(const char *path);
char syncterm_path_separator(void);
ptrdiff_t syncterm_path_store(char *result, size_t result_size,
    const char *value, size_t length);

#endif
