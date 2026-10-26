/* Binary manual-reset and auto-reset events. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_EVENT_H_
#define ST_EVENT_H_

#include "st_threads.h"

#include <stdbool.h>
#include <time.h>

typedef struct st_event st_event_t;

enum st_event_reset_mode {
	ST_EVENT_AUTO_RESET,
	ST_EVENT_MANUAL_RESET,
};

/* Result is unchanged on failure. */
int st_event_create(st_event_t **result, enum st_event_reset_mode mode,
    bool initially_set);

/* These operations return an ST_THREAD_* result. */
int st_event_set(st_event_t *event);
int st_event_reset(st_event_t *event);
int st_event_wait(st_event_t *event);
int st_event_trywait(st_event_t *event);
int st_event_timedwait(st_event_t *event,
    const struct timespec *deadline);

/* The event must have no active operations or waiters. NULL is accepted. */
void st_event_destroy(st_event_t *event);

#endif
