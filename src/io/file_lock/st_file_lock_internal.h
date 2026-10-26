/* Internal file-lock definitions shared by the platform implementations. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_FILE_LOCK_INTERNAL_H_
#define ST_FILE_LOCK_INTERNAL_H_

#include "st_file_lock.h"

#include <stdint.h>

struct st_local_lock;

struct st_file_lock {
	struct st_local_lock *local;
	intptr_t native;
	bool exclusive;
};

int st_file_lock_platform_acquire(st_file_lock_t *lock, const char *path,
    bool exclusive);
void st_file_lock_platform_release(st_file_lock_t *lock);

#endif
