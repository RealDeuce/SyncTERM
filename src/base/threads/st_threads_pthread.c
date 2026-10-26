/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_threads.h"

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>

struct thread_arguments {
	st_thread_start_t function;
	void *argument;
};

static void *
thread_entry(void *argument)
{
	struct thread_arguments arguments = *(struct thread_arguments *)argument;

	free(argument);
	return (void *)(intptr_t)arguments.function(arguments.argument);
}

static bool
valid_mutex_type(int type)
{
	return type == ST_MUTEX_PLAIN || type == ST_MUTEX_RECURSIVE
	    || type == ST_MUTEX_TIMED
	    || type == (ST_MUTEX_RECURSIVE | ST_MUTEX_TIMED);
}

#if !ST_THREADS_HAVE_PTHREAD_MUTEX_TIMEDLOCK
static int
compare_time(const struct timespec *left, const struct timespec *right)
{
	if (left->tv_sec < right->tv_sec)
		return -1;
	if (left->tv_sec > right->tv_sec)
		return 1;
	if (left->tv_nsec < right->tv_nsec)
		return -1;
	return left->tv_nsec > right->tv_nsec;
}
#endif

void
st_call_once(st_once_flag_t *flag, void (*function)(void))
{
	(void)pthread_once(flag, function);
}

int
st_condition_broadcast(st_condition_t *condition)
{
	return pthread_cond_broadcast(condition) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

void
st_condition_destroy(st_condition_t *condition)
{
	(void)pthread_cond_destroy(condition);
}

int
st_condition_init(st_condition_t *condition)
{
	int result = pthread_cond_init(condition, NULL);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == ENOMEM ? ST_THREAD_NO_MEMORY : ST_THREAD_ERROR;
}

int
st_condition_signal(st_condition_t *condition)
{
	return pthread_cond_signal(condition) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

int
st_condition_timedwait(st_condition_t *restrict condition,
    st_mutex_t *restrict mutex, const struct timespec *restrict deadline)
{
	int result = pthread_cond_timedwait(condition, mutex, deadline);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == ETIMEDOUT ? ST_THREAD_TIMED_OUT : ST_THREAD_ERROR;
}

int
st_condition_wait(st_condition_t *condition, st_mutex_t *mutex)
{
	return pthread_cond_wait(condition, mutex) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

void
st_mutex_destroy(st_mutex_t *mutex)
{
	(void)pthread_mutex_destroy(mutex);
}

int
st_mutex_init(st_mutex_t *mutex, int type)
{
	pthread_mutexattr_t attributes;
	int result;

	if (!valid_mutex_type(type))
		return ST_THREAD_ERROR;
	result = pthread_mutexattr_init(&attributes);
	if (result != 0)
		return result == ENOMEM ? ST_THREAD_NO_MEMORY : ST_THREAD_ERROR;
	if ((type & ST_MUTEX_RECURSIVE) != 0)
		result = pthread_mutexattr_settype(&attributes,
		    PTHREAD_MUTEX_RECURSIVE);
	else
		result = pthread_mutexattr_settype(&attributes,
		    PTHREAD_MUTEX_NORMAL);
	if (result == 0)
		result = pthread_mutex_init(mutex, &attributes);
	(void)pthread_mutexattr_destroy(&attributes);
	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == ENOMEM ? ST_THREAD_NO_MEMORY : ST_THREAD_ERROR;
}

int
st_mutex_lock(st_mutex_t *mutex)
{
	return pthread_mutex_lock(mutex) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

int
st_mutex_timedlock(st_mutex_t *restrict mutex,
    const struct timespec *restrict deadline)
{
#if ST_THREADS_HAVE_PTHREAD_MUTEX_TIMEDLOCK
	int result = pthread_mutex_timedlock(mutex, deadline);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == ETIMEDOUT ? ST_THREAD_TIMED_OUT : ST_THREAD_ERROR;
#else
	static const struct timespec retry_delay = {0, 1000000};
	struct timespec now;
	int result;

	do {
		result = pthread_mutex_trylock(mutex);
		if (result == 0)
			return ST_THREAD_SUCCESS;
		if (result != EBUSY)
			return ST_THREAD_ERROR;
		if (timespec_get(&now, TIME_UTC) != TIME_UTC)
			return ST_THREAD_ERROR;
		if (compare_time(&now, deadline) >= 0)
			return ST_THREAD_TIMED_OUT;
		(void)nanosleep(&retry_delay, NULL);
	} while (true);
#endif
}

int
st_mutex_trylock(st_mutex_t *mutex)
{
	int result = pthread_mutex_trylock(mutex);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == EBUSY ? ST_THREAD_BUSY : ST_THREAD_ERROR;
}

int
st_mutex_unlock(st_mutex_t *mutex)
{
	return pthread_mutex_unlock(mutex) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

int
st_thread_create(st_thread_t *thread, st_thread_start_t function,
    void *argument)
{
	struct thread_arguments *arguments = malloc(sizeof(*arguments));
	int result;

	if (arguments == NULL)
		return ST_THREAD_NO_MEMORY;
	arguments->function = function;
	arguments->argument = argument;
	result = pthread_create(thread, NULL, thread_entry, arguments);
	if (result != 0) {
		free(arguments);
		return result == ENOMEM ? ST_THREAD_NO_MEMORY : ST_THREAD_ERROR;
	}
	return ST_THREAD_SUCCESS;
}

st_thread_t
st_thread_current(void)
{
	return pthread_self();
}

int
st_thread_detach(st_thread_t thread)
{
	return pthread_detach(thread) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

bool
st_thread_equal(st_thread_t left, st_thread_t right)
{
	return pthread_equal(left, right) != 0;
}

_Noreturn void
st_thread_exit(int result)
{
	pthread_exit((void *)(intptr_t)result);
	abort();
}

int
st_thread_join(st_thread_t thread, int *result)
{
	void *thread_result;

	if (pthread_join(thread, &thread_result) != 0)
		return ST_THREAD_ERROR;
	if (result != NULL)
		*result = (int)(intptr_t)thread_result;
	return ST_THREAD_SUCCESS;
}

int
st_thread_sleep(const struct timespec *duration, struct timespec *remaining)
{
	return nanosleep(duration, remaining);
}

void
st_thread_yield(void)
{
	(void)sched_yield();
}

int
st_thread_storage_create(st_thread_storage_t *storage,
    st_thread_storage_destructor_t destructor)
{
	return pthread_key_create(storage, destructor) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

void
st_thread_storage_delete(st_thread_storage_t storage)
{
	(void)pthread_key_delete(storage);
}

void *
st_thread_storage_get(st_thread_storage_t storage)
{
	return pthread_getspecific(storage);
}

int
st_thread_storage_set(st_thread_storage_t storage, void *value)
{
	return pthread_setspecific(storage, value) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}
