/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_semaphore.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		return false; \
	} \
} while (false)

static struct timespec
deadline_after(long milliseconds)
{
	struct timespec deadline;

	if (timespec_get(&deadline, TIME_UTC) != TIME_UTC)
		abort();
	deadline.tv_sec += milliseconds / 1000;
	deadline.tv_nsec += (milliseconds % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		++deadline.tv_sec;
		deadline.tv_nsec -= 1000000000L;
	}
	return deadline;
}

static bool
test_counts_and_limit(void)
{
	st_semaphore_t *semaphore = NULL;
	struct timespec deadline;

	CHECK(st_semaphore_create(&semaphore, 2, 3) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_BUSY);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_ERROR);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_BUSY);
	deadline = deadline_after(25);
	CHECK(st_semaphore_timedwait(semaphore, &deadline)
	    == ST_THREAD_TIMED_OUT);
	st_semaphore_destroy(semaphore);
	return true;
}

#define WAITER_COUNT 5
struct waiter_state {
	st_semaphore_t *semaphore;
	st_mutex_t mutex;
	st_condition_t condition;
	int ready;
	int completed;
};

static int
waiter(void *argument)
{
	struct waiter_state *state = argument;
	int status;

	if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
		return ST_THREAD_ERROR;
	++state->ready;
	(void)st_condition_broadcast(&state->condition);
	(void)st_mutex_unlock(&state->mutex);
	status = st_semaphore_wait(state->semaphore);
	if (status != ST_THREAD_SUCCESS)
		return status;
	if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
		return ST_THREAD_ERROR;
	++state->completed;
	(void)st_condition_broadcast(&state->condition);
	(void)st_mutex_unlock(&state->mutex);
	return ST_THREAD_SUCCESS;
}

static bool
wait_for_count(struct waiter_state *state, int count)
{
	struct timespec deadline = deadline_after(2000);

	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	while (state->completed < count) {
		CHECK(st_condition_timedwait(&state->condition, &state->mutex,
		    &deadline) == ST_THREAD_SUCCESS);
	}
	CHECK(st_mutex_unlock(&state->mutex) == ST_THREAD_SUCCESS);
	return true;
}

static bool
test_one_waiter_per_post(void)
{
	struct waiter_state state = {0};
	st_thread_t threads[WAITER_COUNT];
	struct timespec pause = {0, 20000000L};
	int expected;
	int index;
	int result;

	CHECK(st_semaphore_create(&state.semaphore, 0, WAITER_COUNT)
	    == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
	for (index = 0; index < WAITER_COUNT; ++index) {
		CHECK(st_thread_create(&threads[index], waiter, &state)
		    == ST_THREAD_SUCCESS);
	}
	while (state.ready < WAITER_COUNT) {
		CHECK(st_condition_wait(&state.condition, &state.mutex)
		    == ST_THREAD_SUCCESS);
	}
	CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);

	for (expected = 1; expected <= WAITER_COUNT; ++expected) {
		CHECK(st_semaphore_post(state.semaphore) == ST_THREAD_SUCCESS);
		CHECK(wait_for_count(&state, expected));
		if (expected == 1) {
			CHECK(st_thread_sleep(&pause, NULL) == 0);
			CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
			CHECK(state.completed == 1);
			CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);
		}
	}
	for (index = 0; index < WAITER_COUNT; ++index) {
		CHECK(st_thread_join(threads[index], &result)
		    == ST_THREAD_SUCCESS);
		CHECK(result == ST_THREAD_SUCCESS);
	}
	CHECK(st_semaphore_trywait(state.semaphore) == ST_THREAD_BUSY);
	st_condition_destroy(&state.condition);
	st_mutex_destroy(&state.mutex);
	st_semaphore_destroy(state.semaphore);
	return true;
}

#define WORKER_COUNT 8
#define PERMIT_COUNT 3
#define ITERATION_COUNT 500
struct pool_state {
	st_semaphore_t *semaphore;
	st_mutex_t mutex;
	int active;
	int maximum_active;
	bool failed;
};

static int
pool_worker(void *argument)
{
	struct pool_state *state = argument;
	int iteration;

	for (iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
		if (st_semaphore_wait(state->semaphore) != ST_THREAD_SUCCESS)
			return 1;
		if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
			return 2;
		++state->active;
		if (state->active > PERMIT_COUNT)
			state->failed = true;
		if (state->active > state->maximum_active)
			state->maximum_active = state->active;
		if (st_mutex_unlock(&state->mutex) != ST_THREAD_SUCCESS)
			return 3;
		st_thread_yield();
		if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
			return 4;
		--state->active;
		if (st_mutex_unlock(&state->mutex) != ST_THREAD_SUCCESS)
			return 5;
		if (st_semaphore_post(state->semaphore) != ST_THREAD_SUCCESS)
			return 6;
	}
	return 0;
}

static bool
test_resource_pool(void)
{
	struct pool_state state = {0};
	st_thread_t threads[WORKER_COUNT];
	int index;
	int result;

	CHECK(st_semaphore_create(&state.semaphore, PERMIT_COUNT, PERMIT_COUNT)
	    == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	for (index = 0; index < WORKER_COUNT; ++index) {
		CHECK(st_thread_create(&threads[index], pool_worker, &state)
		    == ST_THREAD_SUCCESS);
	}
	for (index = 0; index < WORKER_COUNT; ++index) {
		CHECK(st_thread_join(threads[index], &result)
		    == ST_THREAD_SUCCESS);
		CHECK(result == 0);
	}
	CHECK(!state.failed);
	CHECK(state.maximum_active > 0);
	for (index = 0; index < PERMIT_COUNT; ++index)
		CHECK(st_semaphore_trywait(state.semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(state.semaphore) == ST_THREAD_BUSY);
	st_mutex_destroy(&state.mutex);
	st_semaphore_destroy(state.semaphore);
	return true;
}

static bool
test_errors(void)
{
	st_semaphore_t *semaphore = NULL;
	struct timespec deadline = deadline_after(1);

	CHECK(st_semaphore_create(NULL, 0, 1) == ST_THREAD_ERROR);
	CHECK(st_semaphore_create(&semaphore, 0, 0) == ST_THREAD_ERROR);
	CHECK(semaphore == NULL);
	CHECK(st_semaphore_create(&semaphore, 2, 1) == ST_THREAD_ERROR);
	CHECK(semaphore == NULL);
	CHECK(st_semaphore_post(NULL) == ST_THREAD_ERROR);
	CHECK(st_semaphore_wait(NULL) == ST_THREAD_ERROR);
	CHECK(st_semaphore_trywait(NULL) == ST_THREAD_ERROR);
	CHECK(st_semaphore_timedwait(NULL, &deadline) == ST_THREAD_ERROR);
	CHECK(st_semaphore_timedwait(semaphore, NULL) == ST_THREAD_ERROR);
	st_semaphore_destroy(NULL);
	return true;
}

static bool
test_boundaries(void)
{
	const struct timespec expired = {0, 0};
	st_semaphore_t *semaphore = NULL;

	CHECK(st_semaphore_create(&semaphore, 1, 1) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_timedwait(semaphore, NULL) == ST_THREAD_ERROR);
	CHECK(st_semaphore_timedwait(semaphore, &expired) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_timedwait(semaphore, &expired) == ST_THREAD_TIMED_OUT);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_BUSY);
	st_semaphore_destroy(semaphore);
	CHECK(st_semaphore_create(&semaphore, SIZE_MAX, SIZE_MAX)
	    == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_ERROR);
	CHECK(st_semaphore_trywait(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_SUCCESS);
	CHECK(st_semaphore_post(semaphore) == ST_THREAD_ERROR);
	st_semaphore_destroy(semaphore);
	return true;
}

int
main(void)
{
	if (!test_counts_and_limit() || !test_one_waiter_per_post()
	    || !test_resource_pool() || !test_errors() || !test_boundaries())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
