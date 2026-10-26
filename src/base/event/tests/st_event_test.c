/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_event.h"

#include <stdbool.h>
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
test_initial_and_reset(void)
{
	st_event_t *event = NULL;
	struct timespec deadline;

	CHECK(st_event_create(&event, ST_EVENT_AUTO_RESET, true)
	    == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_BUSY);
	CHECK(st_event_set(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_set(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_BUSY);
	deadline = deadline_after(25);
	CHECK(st_event_timedwait(event, &deadline) == ST_THREAD_TIMED_OUT);
	st_event_destroy(event);

	event = NULL;
	CHECK(st_event_create(&event, ST_EVENT_MANUAL_RESET, true)
	    == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_reset(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_BUSY);
	CHECK(st_event_set(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_wait(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(event) == ST_THREAD_SUCCESS);
	st_event_destroy(event);
	return true;
}

#define WAITER_COUNT 5
struct waiter_state {
	st_event_t *event;
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
	status = st_event_wait(state->event);
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
start_waiters(struct waiter_state *state, st_thread_t *threads)
{
	int index;

	CHECK(st_mutex_init(&state->mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state->condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	for (index = 0; index < WAITER_COUNT; ++index) {
		CHECK(st_thread_create(&threads[index], waiter, state)
		    == ST_THREAD_SUCCESS);
	}
	while (state->ready < WAITER_COUNT) {
		CHECK(st_condition_wait(&state->condition, &state->mutex)
		    == ST_THREAD_SUCCESS);
	}
	CHECK(st_mutex_unlock(&state->mutex) == ST_THREAD_SUCCESS);
	return true;
}

static bool
join_waiters(struct waiter_state *state, st_thread_t *threads)
{
	int index;
	int result;

	for (index = 0; index < WAITER_COUNT; ++index) {
		CHECK(st_thread_join(threads[index], &result) == ST_THREAD_SUCCESS);
		CHECK(result == ST_THREAD_SUCCESS);
	}
	st_condition_destroy(&state->condition);
	st_mutex_destroy(&state->mutex);
	return true;
}

static bool
test_auto_reset_waiters(void)
{
	struct waiter_state state = {0};
	st_thread_t threads[WAITER_COUNT];
	struct timespec pause = {0, 20000000L};
	int expected;

	CHECK(st_event_create(&state.event, ST_EVENT_AUTO_RESET, false)
	    == ST_THREAD_SUCCESS);
	CHECK(start_waiters(&state, threads));
	for (expected = 1; expected <= WAITER_COUNT; ++expected) {
		CHECK(st_event_set(state.event) == ST_THREAD_SUCCESS);
		CHECK(wait_for_count(&state, expected));
		if (expected == 1) {
			CHECK(st_thread_sleep(&pause, NULL) == 0);
			CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
			CHECK(state.completed == 1);
			CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);
		}
	}
	CHECK(join_waiters(&state, threads));
	st_event_destroy(state.event);
	return true;
}

static bool
test_manual_reset_waiters(void)
{
	struct waiter_state state = {0};
	st_thread_t threads[WAITER_COUNT];

	CHECK(st_event_create(&state.event, ST_EVENT_MANUAL_RESET, false)
	    == ST_THREAD_SUCCESS);
	CHECK(start_waiters(&state, threads));
	CHECK(st_event_set(state.event) == ST_THREAD_SUCCESS);
	CHECK(wait_for_count(&state, WAITER_COUNT));
	CHECK(join_waiters(&state, threads));
	CHECK(st_event_trywait(state.event) == ST_THREAD_SUCCESS);
	CHECK(st_event_reset(state.event) == ST_THREAD_SUCCESS);
	CHECK(st_event_trywait(state.event) == ST_THREAD_BUSY);
	st_event_destroy(state.event);
	return true;
}

#define HANDSHAKE_COUNT 1000
struct handshake_state {
	st_event_t *request;
	st_event_t *response;
};

static int
handshake_worker(void *argument)
{
	struct handshake_state *state = argument;
	int iteration;

	for (iteration = 0; iteration < HANDSHAKE_COUNT; ++iteration) {
		if (st_event_wait(state->request) != ST_THREAD_SUCCESS)
			return 1;
		if (st_event_set(state->response) != ST_THREAD_SUCCESS)
			return 2;
	}
	return 0;
}

static bool
test_handshake(void)
{
	struct handshake_state state = {0};
	st_thread_t thread;
	int iteration;
	int result;

	CHECK(st_event_create(&state.request, ST_EVENT_AUTO_RESET, false)
	    == ST_THREAD_SUCCESS);
	CHECK(st_event_create(&state.response, ST_EVENT_AUTO_RESET, false)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&thread, handshake_worker, &state)
	    == ST_THREAD_SUCCESS);
	for (iteration = 0; iteration < HANDSHAKE_COUNT; ++iteration) {
		CHECK(st_event_set(state.request) == ST_THREAD_SUCCESS);
		CHECK(st_event_wait(state.response) == ST_THREAD_SUCCESS);
	}
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == 0);
	st_event_destroy(state.response);
	st_event_destroy(state.request);
	return true;
}

static bool
test_errors(void)
{
	st_event_t *event = NULL;
	struct timespec deadline = deadline_after(1);

	CHECK(st_event_create(NULL, ST_EVENT_AUTO_RESET, false)
	    == ST_THREAD_ERROR);
	CHECK(st_event_create(&event, (enum st_event_reset_mode)20, false)
	    == ST_THREAD_ERROR);
	CHECK(event == NULL);
	CHECK(st_event_set(NULL) == ST_THREAD_ERROR);
	CHECK(st_event_reset(NULL) == ST_THREAD_ERROR);
	CHECK(st_event_wait(NULL) == ST_THREAD_ERROR);
	CHECK(st_event_trywait(NULL) == ST_THREAD_ERROR);
	CHECK(st_event_timedwait(NULL, &deadline) == ST_THREAD_ERROR);
	CHECK(st_event_timedwait(event, NULL) == ST_THREAD_ERROR);
	st_event_destroy(NULL);
	return true;
}

static bool
test_expired_deadline(void)
{
	const struct timespec expired = {0, 0};
	st_event_t *event = NULL;

	CHECK(st_event_create(&event, ST_EVENT_AUTO_RESET, true)
	    == ST_THREAD_SUCCESS);
	CHECK(st_event_timedwait(event, &expired) == ST_THREAD_SUCCESS);
	CHECK(st_event_timedwait(event, &expired) == ST_THREAD_TIMED_OUT);
	CHECK(st_event_reset(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_set(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_timedwait(event, NULL) == ST_THREAD_ERROR);
	CHECK(st_event_trywait(event) == ST_THREAD_SUCCESS);
	st_event_destroy(event);
	CHECK(st_event_create(&event, ST_EVENT_MANUAL_RESET, true)
	    == ST_THREAD_SUCCESS);
	CHECK(st_event_timedwait(event, &expired) == ST_THREAD_SUCCESS);
	CHECK(st_event_timedwait(event, &expired) == ST_THREAD_SUCCESS);
	CHECK(st_event_reset(event) == ST_THREAD_SUCCESS);
	CHECK(st_event_timedwait(event, &expired) == ST_THREAD_TIMED_OUT);
	st_event_destroy(event);
	return true;
}

int
main(void)
{
	if (!test_initial_and_reset() || !test_auto_reset_waiters()
	    || !test_manual_reset_waiters() || !test_handshake()
	    || !test_errors() || !test_expired_deadline())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
