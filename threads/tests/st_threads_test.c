/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_threads.h"

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

static ST_THREAD_LOCAL int thread_local_value;
static st_once_flag_t once_flag = ST_ONCE_FLAG_INIT;
static int once_count;

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

static int
return_value(void *argument)
{
	return *(int *)argument;
}

static int
exit_value(void *argument)
{
	st_thread_exit(*(int *)argument);
}

static int
thread_local_worker(void *argument)
{
	int value = *(int *)argument;

	thread_local_value = value;
	st_thread_yield();
	return thread_local_value;
}

static bool
test_threads(void)
{
	st_thread_t thread;
	int expected = 37;
	int result;
	struct timespec duration = {0, 1000000L};
	struct timespec remaining = {1, 1};

	CHECK(st_thread_create(&thread, return_value, &expected)
	    == ST_THREAD_SUCCESS);
	CHECK(!st_thread_equal(st_thread_current(), thread));
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == expected);

	expected = -23;
	CHECK(st_thread_create(&thread, exit_value, &expected)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == expected);

	thread_local_value = 11;
	expected = 29;
	CHECK(st_thread_create(&thread, thread_local_worker, &expected)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == expected);
	CHECK(thread_local_value == 11);

	CHECK(st_thread_sleep(&duration, &remaining) == 0);
	return true;
}

static bool
test_recursive_mutex(void)
{
	st_mutex_t mutex;

	CHECK(st_mutex_init(&mutex, ST_MUTEX_RECURSIVE) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_trylock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&mutex) == ST_THREAD_SUCCESS);
	st_mutex_destroy(&mutex);
	CHECK(st_mutex_init(&mutex, 0x40) == ST_THREAD_ERROR);
	return true;
}

struct lock_attempt {
	st_mutex_t *mutex;
	long timeout_ms;
	bool timed;
};

static int
lock_attempt_worker(void *argument)
{
	struct lock_attempt *attempt = argument;
	int result;

	if (attempt->timed) {
		struct timespec deadline = deadline_after(attempt->timeout_ms);

		result = st_mutex_timedlock(attempt->mutex, &deadline);
	} else {
		result = st_mutex_trylock(attempt->mutex);
	}
	if (result == ST_THREAD_SUCCESS)
		(void)st_mutex_unlock(attempt->mutex);
	return result;
}

static bool
test_mutex_contention(void)
{
	st_mutex_t mutex;
	st_thread_t thread;
	struct lock_attempt attempt = {&mutex, 40, false};
	struct timespec pause = {0, 20000000L};
	int result;

	CHECK(st_mutex_init(&mutex, ST_MUTEX_TIMED) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&thread, lock_attempt_worker, &attempt)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == ST_THREAD_BUSY);

	attempt.timed = true;
	CHECK(st_thread_create(&thread, lock_attempt_worker, &attempt)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == ST_THREAD_TIMED_OUT);

	attempt.timeout_ms = 1000;
	CHECK(st_thread_create(&thread, lock_attempt_worker, &attempt)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_sleep(&pause, NULL) == 0);
	CHECK(st_mutex_unlock(&mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == ST_THREAD_SUCCESS);
	st_mutex_destroy(&mutex);
	return true;
}

#define WAITER_COUNT 4
struct condition_state {
	st_mutex_t mutex;
	st_condition_t condition;
	int ready;
	int awakened;
	bool release;
};

static int
condition_waiter(void *argument)
{
	struct condition_state *state = argument;

	if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
		return 1;
	++state->ready;
	if (st_condition_broadcast(&state->condition) != ST_THREAD_SUCCESS)
		return 2;
	while (!state->release) {
		if (st_condition_wait(&state->condition, &state->mutex)
		    != ST_THREAD_SUCCESS)
			return 3;
	}
	++state->awakened;
	return st_mutex_unlock(&state->mutex) == ST_THREAD_SUCCESS ? 0 : 4;
}

static bool
test_conditions(void)
{
	struct condition_state state = {0};
	st_thread_t threads[WAITER_COUNT];
	struct timespec deadline;
	int result;
	int index;

	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
	for (index = 0; index < WAITER_COUNT; ++index) {
		CHECK(st_thread_create(&threads[index], condition_waiter, &state)
		    == ST_THREAD_SUCCESS);
	}
	while (state.ready != WAITER_COUNT) {
		CHECK(st_condition_wait(&state.condition, &state.mutex)
		    == ST_THREAD_SUCCESS);
	}
	state.release = true;
	CHECK(st_condition_broadcast(&state.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);
	for (index = 0; index < WAITER_COUNT; ++index) {
		CHECK(st_thread_join(threads[index], &result) == ST_THREAD_SUCCESS);
		CHECK(result == 0);
	}
	CHECK(state.awakened == WAITER_COUNT);

	CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
	deadline = deadline_after(30);
	CHECK(st_condition_timedwait(&state.condition, &state.mutex, &deadline)
	    == ST_THREAD_TIMED_OUT);
	CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);
	st_condition_destroy(&state.condition);
	st_mutex_destroy(&state.mutex);
	return true;
}

static void
initialize_once(void)
{
	++once_count;
}

static int
once_worker(void *argument)
{
	(void)argument;
	st_call_once(&once_flag, initialize_once);
	st_call_once(&once_flag, initialize_once);
	return 0;
}

static bool
test_once(void)
{
	st_thread_t threads[8];
	int index;

	for (index = 0; index < 8; ++index) {
		CHECK(st_thread_create(&threads[index], once_worker, NULL)
		    == ST_THREAD_SUCCESS);
	}
	for (index = 0; index < 8; ++index)
		CHECK(st_thread_join(threads[index], NULL) == ST_THREAD_SUCCESS);
	CHECK(once_count == 1);
	return true;
}

static st_thread_storage_t storage;
static st_mutex_t destructor_mutex;
static int destructor_count;

struct storage_value {
	int destructor_calls;
};

static void
storage_destructor(void *argument)
{
	struct storage_value *value = argument;

	++value->destructor_calls;
	if (st_mutex_lock(&destructor_mutex) != ST_THREAD_SUCCESS)
		abort();
	++destructor_count;
	if (st_mutex_unlock(&destructor_mutex) != ST_THREAD_SUCCESS)
		abort();
	if (value->destructor_calls
	    < ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS
	    && st_thread_storage_set(storage, value) != ST_THREAD_SUCCESS)
		abort();
}

static int
storage_worker(void *argument)
{
	if (st_thread_storage_get(storage) != NULL)
		return 1;
	if (st_thread_storage_set(storage, argument) != ST_THREAD_SUCCESS)
		return 2;
	return st_thread_storage_get(storage) == argument ? 0 : 3;
}

static int
storage_exit_worker(void *argument)
{
	if (st_thread_storage_set(storage, argument) != ST_THREAD_SUCCESS)
		return 1;
	st_thread_exit(17);
}

static bool
test_thread_storage(void)
{
	struct storage_value values[2] = {{0}, {0}};
	st_thread_t threads[2];
	int index;

	CHECK(st_mutex_init(&destructor_mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_storage_create(&storage, storage_destructor)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&threads[0], storage_worker, &values[0])
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&threads[1], storage_exit_worker, &values[1])
	    == ST_THREAD_SUCCESS);
	for (index = 0; index < 2; ++index) {
		int result;

		CHECK(st_thread_join(threads[index], &result) == ST_THREAD_SUCCESS);
		CHECK(result == (index == 0 ? 0 : 17));
		CHECK(values[index].destructor_calls
		    == ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS);
	}
	CHECK(destructor_count
	    == 2 * ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS);
	CHECK(st_thread_storage_get(storage) == NULL);
	CHECK(st_thread_storage_set(storage, &values[0]) == ST_THREAD_SUCCESS);
	st_thread_storage_delete(storage);
	CHECK(destructor_count
	    == 2 * ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS);
	st_mutex_destroy(&destructor_mutex);
	return true;
}

static st_thread_storage_t deleted_storage;
static int deleted_storage_destructor_count;

struct storage_delete_state {
	st_mutex_t mutex;
	st_condition_t condition;
	bool ready;
	bool release;
	int value;
};

static void
deleted_storage_destructor(void *argument)
{
	(void)argument;
	++deleted_storage_destructor_count;
}

static int
storage_delete_worker(void *argument)
{
	struct storage_delete_state *state = argument;

	if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
		return 1;
	if (st_thread_storage_set(deleted_storage, &state->value)
	    != ST_THREAD_SUCCESS)
		return 2;
	state->ready = true;
	if (st_condition_signal(&state->condition) != ST_THREAD_SUCCESS)
		return 3;
	while (!state->release) {
		if (st_condition_wait(&state->condition, &state->mutex)
		    != ST_THREAD_SUCCESS)
			return 4;
	}
	return st_mutex_unlock(&state->mutex) == ST_THREAD_SUCCESS ? 0 : 5;
}

static bool
test_thread_storage_delete(void)
{
	struct storage_delete_state state = {0};
	st_thread_t thread;
	int result;

	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.condition) == ST_THREAD_SUCCESS);
	CHECK(st_thread_storage_create(&deleted_storage,
	    deleted_storage_destructor) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&thread, storage_delete_worker, &state)
	    == ST_THREAD_SUCCESS);
	while (!state.ready) {
		CHECK(st_condition_wait(&state.condition, &state.mutex)
		    == ST_THREAD_SUCCESS);
	}
	st_thread_storage_delete(deleted_storage);
	state.release = true;
	CHECK(st_condition_signal(&state.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == 0);
	CHECK(deleted_storage_destructor_count == 0);
	st_condition_destroy(&state.condition);
	st_mutex_destroy(&state.mutex);
	return true;
}

struct detached_state {
	st_mutex_t mutex;
	st_condition_t condition;
	bool done;
};

static int
detached_worker(void *argument)
{
	struct detached_state *state = argument;

	if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
		return 1;
	state->done = true;
	(void)st_condition_signal(&state->condition);
	(void)st_mutex_unlock(&state->mutex);
	return 0;
}

static bool
test_detach(void)
{
	struct detached_state state = {0};
	st_thread_t thread;

	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.condition) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&state.mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&thread, detached_worker, &state)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_detach(thread) == ST_THREAD_SUCCESS);
	while (!state.done) {
		CHECK(st_condition_wait(&state.condition, &state.mutex)
		    == ST_THREAD_SUCCESS);
	}
	CHECK(st_mutex_unlock(&state.mutex) == ST_THREAD_SUCCESS);
	st_condition_destroy(&state.condition);
	st_mutex_destroy(&state.mutex);
	return true;
}

#define STRESS_THREADS 4
#define STRESS_ITERATIONS 10000
struct stress_state {
	st_mutex_t mutex;
	int count;
};

static int
stress_worker(void *argument)
{
	struct stress_state *state = argument;
	int iteration;

	for (iteration = 0; iteration < STRESS_ITERATIONS; ++iteration) {
		if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
			return 1;
		++state->count;
		if (st_mutex_unlock(&state->mutex) != ST_THREAD_SUCCESS)
			return 2;
	}
	return 0;
}

static bool
test_contention_stress(void)
{
	struct stress_state state = {0};
	st_thread_t threads[STRESS_THREADS];
	int index;

	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS);
	for (index = 0; index < STRESS_THREADS; ++index) {
		CHECK(st_thread_create(&threads[index], stress_worker, &state)
		    == ST_THREAD_SUCCESS);
	}
	for (index = 0; index < STRESS_THREADS; ++index) {
		int result;

		CHECK(st_thread_join(threads[index], &result) == ST_THREAD_SUCCESS);
		CHECK(result == 0);
	}
	CHECK(state.count == STRESS_THREADS * STRESS_ITERATIONS);
	st_mutex_destroy(&state.mutex);
	return true;
}

struct rwlock_attempt {
	st_rwlock_t *lock;
	bool write;
};

static int
rwlock_attempt_worker(void *argument)
{
	struct rwlock_attempt *attempt = argument;
	int result;

	result = attempt->write
	    ? st_rwlock_try_write_lock(attempt->lock)
	    : st_rwlock_try_read_lock(attempt->lock);
	if (result == ST_THREAD_SUCCESS
	    && st_rwlock_unlock(attempt->lock) != ST_THREAD_SUCCESS)
		return ST_THREAD_ERROR;
	return result;
}

static bool
test_rwlock_basic(void)
{
	st_rwlock_t lock;
	st_thread_t thread;
	struct rwlock_attempt attempt = {&lock, true};
	int result;

	CHECK(st_rwlock_init(&lock) == ST_THREAD_SUCCESS);
	CHECK(st_rwlock_read_lock(&lock) == ST_THREAD_SUCCESS);
	CHECK(st_rwlock_read_lock(&lock) == ST_THREAD_SUCCESS);
	attempt.write = false;
	CHECK(st_thread_create(&thread, rwlock_attempt_worker, &attempt)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == ST_THREAD_SUCCESS);
	attempt.write = true;
	CHECK(st_thread_create(&thread, rwlock_attempt_worker, &attempt)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == ST_THREAD_BUSY);
	CHECK(st_rwlock_unlock(&lock) == ST_THREAD_SUCCESS);
	CHECK(st_rwlock_unlock(&lock) == ST_THREAD_SUCCESS);

	CHECK(st_rwlock_write_lock(&lock) == ST_THREAD_SUCCESS);
	attempt.write = false;
	CHECK(st_thread_create(&thread, rwlock_attempt_worker, &attempt)
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &result) == ST_THREAD_SUCCESS);
	CHECK(result == ST_THREAD_BUSY);
	CHECK(st_rwlock_unlock(&lock) == ST_THREAD_SUCCESS);
	CHECK(st_rwlock_try_write_lock(&lock) == ST_THREAD_SUCCESS);
	CHECK(st_rwlock_unlock(&lock) == ST_THREAD_SUCCESS);
	CHECK(st_rwlock_destroy(&lock) == ST_THREAD_SUCCESS);
	return true;
}

#define RWLOCK_VALUES 32
#define RWLOCK_WRITES 2000
#define RWLOCK_READS 4000
#define RWLOCK_READERS 3

struct rwlock_stress {
	st_rwlock_t lock;
	unsigned generation;
	unsigned values[RWLOCK_VALUES];
};

static int
rwlock_writer(void *argument)
{
	struct rwlock_stress *state = argument;

	for (unsigned iteration = 1; iteration <= RWLOCK_WRITES; ++iteration) {
		if (st_rwlock_write_lock(&state->lock) != ST_THREAD_SUCCESS)
			return 1;
		state->generation = iteration;
		for (size_t index = 0; index < RWLOCK_VALUES; ++index)
			state->values[index] = iteration;
		if (st_rwlock_unlock(&state->lock) != ST_THREAD_SUCCESS)
			return 2;
	}
	return 0;
}

static int
rwlock_reader(void *argument)
{
	struct rwlock_stress *state = argument;

	for (unsigned iteration = 0; iteration < RWLOCK_READS; ++iteration) {
		unsigned generation;

		if (st_rwlock_read_lock(&state->lock) != ST_THREAD_SUCCESS)
			return 1;
		generation = state->generation;
		for (size_t index = 0; index < RWLOCK_VALUES; ++index) {
			if (state->values[index] != generation) {
				(void)st_rwlock_unlock(&state->lock);
				return 2;
			}
		}
		if (st_rwlock_unlock(&state->lock) != ST_THREAD_SUCCESS)
			return 3;
	}
	return 0;
}

static bool
test_rwlock_stress(void)
{
	struct rwlock_stress state = {0};
	st_thread_t writer;
	st_thread_t readers[RWLOCK_READERS];
	int result;

	CHECK(st_rwlock_init(&state.lock) == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&writer, rwlock_writer, &state)
	    == ST_THREAD_SUCCESS);
	for (size_t index = 0; index < RWLOCK_READERS; ++index) {
		CHECK(st_thread_create(&readers[index], rwlock_reader, &state)
		    == ST_THREAD_SUCCESS);
	}
	CHECK(st_thread_join(writer, &result) == ST_THREAD_SUCCESS);
	CHECK(result == 0);
	for (size_t index = 0; index < RWLOCK_READERS; ++index) {
		CHECK(st_thread_join(readers[index], &result)
		    == ST_THREAD_SUCCESS);
		CHECK(result == 0);
	}
	CHECK(state.generation == RWLOCK_WRITES);
	CHECK(st_rwlock_destroy(&state.lock) == ST_THREAD_SUCCESS);
	return true;
}

int
main(void)
{
	if (!test_threads() || !test_recursive_mutex()
	    || !test_mutex_contention() || !test_conditions() || !test_once()
	    || !test_thread_storage() || !test_thread_storage_delete()
	    || !test_detach()
	    || !test_contention_stress() || !test_rwlock_basic()
	    || !test_rwlock_stress())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
