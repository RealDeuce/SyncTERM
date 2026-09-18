/* File-lock coordination tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_file_lock.h"

#include "filesystem.h"
#include "st_threads.h"

#include <stdatomic.h>
#include <stdio.h>

#ifndef ST_FILE_LOCK_TEST_PATH
#error "ST_FILE_LOCK_TEST_PATH is required"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return 1; \
	} \
} while (0)

struct attempt {
	atomic_bool started;
	atomic_bool acquired;
	bool exclusive;
	int status;
};

static int
acquire_lock(void *argument)
{
	struct attempt *attempt = argument;
	st_file_lock_t *lock = NULL;

	atomic_store(&attempt->started, true);
	attempt->status = st_file_lock_acquire(&lock, ST_FILE_LOCK_TEST_PATH,
	    attempt->exclusive);
	if (attempt->status == 0) {
		atomic_store(&attempt->acquired, true);
		st_file_lock_release(lock);
	}
	return 0;
}

static int
exercise(bool first_exclusive, bool second_exclusive, bool should_block)
{
	struct attempt attempt = {0};
	const struct timespec delay = { 0, 50000000L };
	st_file_lock_t *first = NULL;
	st_thread_t thread;
	int thread_status;

	atomic_init(&attempt.started, false);
	atomic_init(&attempt.acquired, false);
	attempt.exclusive = second_exclusive;
	CHECK(st_file_lock_acquire(&first, ST_FILE_LOCK_TEST_PATH,
	    first_exclusive) == 0);
	CHECK(st_thread_create(&thread, acquire_lock, &attempt)
	    == ST_THREAD_SUCCESS);
	while (!atomic_load(&attempt.started))
		st_thread_yield();
	CHECK(st_thread_sleep(&delay, NULL) == ST_THREAD_SUCCESS);
	CHECK(atomic_load(&attempt.acquired) != should_block);
	st_file_lock_release(first);
	CHECK(st_thread_join(thread, &thread_status) == ST_THREAD_SUCCESS);
	CHECK(thread_status == 0 && attempt.status == 0);
	CHECK(atomic_load(&attempt.acquired));
	return 0;
}

int
main(void)
{
	char sidecar[sizeof(ST_FILE_LOCK_TEST_PATH) + sizeof(".lock")];

	CHECK(exercise(false, false, false) == 0);
	CHECK(exercise(false, true, true) == 0);
	CHECK(exercise(true, false, true) == 0);
	(void)snprintf(sidecar, sizeof(sidecar), "%s.lock",
	    ST_FILE_LOCK_TEST_PATH);
	(void)fs_remove_file(sidecar);
	return 0;
}
