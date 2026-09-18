/* Native path-string operations. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */


#ifndef SYNCTERM_PATH_H_
#define SYNCTERM_PATH_H_

#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>

bool path_is_separator(char character);
/* Returns a view into path. Local parsing recognizes native separators. */
const char *path_basename(const char *path);
/* Recognizes both '/' and '\\' for names received from another system. */
const char *path_untrusted_basename(const char *path);
/* A drive-relative Windows path such as C:foo is not rooted. */
bool path_is_rooted(const char *path);
/* Rejects empty, dot, dot-dot, and both native and foreign separators. */
bool path_untrusted_component_valid(const char *component);

/* Allocates directory joined to name with the native separator. */
int path_join_alloc(char **result, const char *directory, const char *name);

/*
 * Output functions return the required byte count, excluding the terminating
 * NUL, or a negative IO_ERROR_* value. A NULL result is permitted only when
 * result_size is zero. If the result does not fit, the buffer is unchanged.
 * Exact in-place operation (result == path) is supported.
 */
ptrdiff_t path_parent(char *result, size_t result_size, const char *path);
ptrdiff_t path_append_separator(char *result, size_t result_size,
    const char *path);
/* Lexical normalization does not require the named object to exist. */
ptrdiff_t path_make_absolute(char *result, size_t result_size,
    const char *path);
/* Expands '~' and '~/' from HOME on POSIX; Windows leaves them literal. */
ptrdiff_t path_expand_user(char *result, size_t result_size,
    const char *path);

#endif
