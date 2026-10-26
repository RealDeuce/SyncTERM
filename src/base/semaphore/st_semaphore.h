/* Bounded counting semaphores. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_SEMAPHORE_H_
#define ST_SEMAPHORE_H_

#include "st_threads.h"

#include <stddef.h>
#include <time.h>

typedef struct st_semaphore st_semaphore_t;

/*
 * The maximum count must be nonzero and the initial count must not exceed it.
 * Result is unchanged on failure.
 */
int st_semaphore_create(st_semaphore_t **result, size_t initial_count,
    size_t maximum_count);

/*
 * Post adds one permit. It returns ST_THREAD_ERROR without changing the count
 * if the semaphore is already at its maximum. Other operations return the
 * corresponding ST_THREAD_* result.
 */
int st_semaphore_post(st_semaphore_t *semaphore);
int st_semaphore_wait(st_semaphore_t *semaphore);
int st_semaphore_trywait(st_semaphore_t *semaphore);
int st_semaphore_timedwait(st_semaphore_t *semaphore,
    const struct timespec *deadline);

/* The semaphore must have no active operations or waiters. NULL is accepted. */
void st_semaphore_destroy(st_semaphore_t *semaphore);

#endif
