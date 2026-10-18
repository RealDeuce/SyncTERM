/* Runtime-loaded libraries and symbols. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_DYNAMIC_LIBRARY_H_
#define SYNCTERM_DYNAMIC_LIBRARY_H_

#include "io_error.h"

#include <stddef.h>

typedef struct dynamic_library dynamic_library_t;

enum dynamic_library_visibility {
	DYNAMIC_LIBRARY_LOCAL,
	DYNAMIC_LIBRARY_GLOBAL,
};

/*
 * Names are non-empty ASCII filename fragments, not paths. For each name,
 * the platform's conventional names are tried at preferred_major and then
 * at every older major version. Result is unchanged on failure.
 * A successful load remains mapped for the process lifetime once its required
 * symbols have resolved. The opaque handle is a native-loader token, not a
 * caller-owned allocation; discarding it does not require cleanup.
 */
int dynamic_library_open(dynamic_library_t **result,
    const char *const names[], size_t name_count, unsigned preferred_major,
    enum dynamic_library_visibility visibility);

/* The caller casts a successful result to the symbol's declared type. */
void *dynamic_library_symbol(const dynamic_library_t *library,
    const char *name);

/*
 * Reject a tentative load ONLY when required symbol resolution fails. Never
 * close a successfully resolved feature, including after initialization fails
 * or during shutdown. Consumes one load reference even on error. NULL succeeds.
 */
int dynamic_library_close(dynamic_library_t *library);

#endif
