/* Persistent advisory locks for file transactions. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_FILE_LOCK_H_
#define ST_FILE_LOCK_H_

#include <stdbool.h>

typedef struct st_file_lock st_file_lock_t;

/*
 * Acquires a blocking lock on the persistent "<path>.lock" sidecar.  An
 * exclusive lock excludes every other holder; shared locks may coexist.
 * The returned lock must be released with st_file_lock_release().
 */
int st_file_lock_acquire(st_file_lock_t **result, const char *path,
    bool exclusive);
void st_file_lock_release(st_file_lock_t *lock);

#endif
