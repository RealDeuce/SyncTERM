/* POSIX read/write locks, with a mutex/condition fallback. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_threads.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#if ST_THREADS_HAVE_PTHREAD_RWLOCK

int
st_rwlock_init(st_rwlock_t *lock)
{
	int result = pthread_rwlock_init(lock, NULL);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == ENOMEM ? ST_THREAD_NO_MEMORY : ST_THREAD_ERROR;
}

int
st_rwlock_read_lock(st_rwlock_t *lock)
{
	return pthread_rwlock_rdlock(lock) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

int
st_rwlock_try_read_lock(st_rwlock_t *lock)
{
	int result = pthread_rwlock_tryrdlock(lock);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == EBUSY ? ST_THREAD_BUSY : ST_THREAD_ERROR;
}

int
st_rwlock_write_lock(st_rwlock_t *lock)
{
	return pthread_rwlock_wrlock(lock) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

int
st_rwlock_try_write_lock(st_rwlock_t *lock)
{
	int result = pthread_rwlock_trywrlock(lock);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == EBUSY ? ST_THREAD_BUSY : ST_THREAD_ERROR;
}

int
st_rwlock_unlock(st_rwlock_t *lock)
{
	return pthread_rwlock_unlock(lock) == 0
	    ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

int
st_rwlock_destroy(st_rwlock_t *lock)
{
	int result = pthread_rwlock_destroy(lock);

	if (result == 0)
		return ST_THREAD_SUCCESS;
	return result == EBUSY ? ST_THREAD_BUSY : ST_THREAD_ERROR;
}

#else

struct st_rwlock_reader {
	struct st_rwlock_reader *next;
	pthread_t thread;
	unsigned count;
};

struct st_rwlock_impl {
	pthread_mutex_t mutex;
	pthread_cond_t readers_ready;
	pthread_cond_t writers_ready;
	unsigned readers;
	unsigned writers_waiting;
	bool writer;
	pthread_t writer_thread;
	struct st_rwlock_reader *reader_threads;
};

static struct st_rwlock_reader *
find_reader(struct st_rwlock_impl *lock, struct st_rwlock_reader ***previous,
	bool create)
{
	struct st_rwlock_reader *reader;
	pthread_t self = pthread_self();

	if (previous != NULL)
		*previous = &lock->reader_threads;
	for (reader = lock->reader_threads; reader != NULL;
	    reader = reader->next) {
		if (pthread_equal(reader->thread, self) != 0)
			return reader;
		if (previous != NULL)
			*previous = &reader->next;
	}
	if (!create)
		return NULL;
	reader = calloc(1, sizeof(*reader));
	if (reader == NULL)
		return NULL;
	reader->thread = self;
	reader->next = lock->reader_threads;
	lock->reader_threads = reader;
	return reader;
}

static void
remove_reader(struct st_rwlock_impl *lock, struct st_rwlock_reader *reader)
{
	struct st_rwlock_reader **entry;

	for (entry = &lock->reader_threads; *entry != NULL;
	    entry = &(*entry)->next) {
		if (*entry == reader) {
			*entry = reader->next;
			free(reader);
			return;
		}
	}
}

int
st_rwlock_init(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	int result;

	if (handle == NULL)
		return ST_THREAD_ERROR;
	*handle = NULL;
	lock = calloc(1, sizeof(*lock));
	if (lock == NULL)
		return ST_THREAD_NO_MEMORY;
	result = pthread_mutex_init(&lock->mutex, NULL);
	if (result != 0)
		goto fail_lock;
	result = pthread_cond_init(&lock->readers_ready, NULL);
	if (result != 0)
		goto fail_readers;
	result = pthread_cond_init(&lock->writers_ready, NULL);
	if (result != 0)
		goto fail_writers;
	*handle = lock;
	return ST_THREAD_SUCCESS;

fail_writers:
	(void)pthread_cond_destroy(&lock->readers_ready);
fail_readers:
	(void)pthread_mutex_destroy(&lock->mutex);
fail_lock:
	free(lock);
	return result == ENOMEM ? ST_THREAD_NO_MEMORY : ST_THREAD_ERROR;
}

int
st_rwlock_read_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	struct st_rwlock_reader *reader;
	int result;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (pthread_mutex_lock(&lock->mutex) != 0)
		return ST_THREAD_ERROR;
	reader = find_reader(lock, NULL, true);
	if (reader == NULL) {
		(void)pthread_mutex_unlock(&lock->mutex);
		return ST_THREAD_NO_MEMORY;
	}
	while (reader->count == 0
	    && (lock->writer || lock->writers_waiting != 0)) {
		result = pthread_cond_wait(&lock->readers_ready, &lock->mutex);
		if (result != 0) {
			remove_reader(lock, reader);
			(void)pthread_mutex_unlock(&lock->mutex);
			return ST_THREAD_ERROR;
		}
	}
	if (lock->readers == UINT_MAX || reader->count == UINT_MAX) {
		if (reader->count == 0)
			remove_reader(lock, reader);
		(void)pthread_mutex_unlock(&lock->mutex);
		return ST_THREAD_ERROR;
	}
	++lock->readers;
	++reader->count;
	(void)pthread_mutex_unlock(&lock->mutex);
	return ST_THREAD_SUCCESS;
}

int
st_rwlock_try_read_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	struct st_rwlock_reader *reader;
	int status;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (pthread_mutex_lock(&lock->mutex) != 0)
		return ST_THREAD_ERROR;
	reader = find_reader(lock, NULL, true);
	if (reader == NULL)
		status = ST_THREAD_NO_MEMORY;
	else if (reader->count == 0
	    && (lock->writer || lock->writers_waiting != 0))
		status = ST_THREAD_BUSY;
	else if (lock->readers == UINT_MAX || reader->count == UINT_MAX)
		status = ST_THREAD_ERROR;
	else {
		++lock->readers;
		++reader->count;
		status = ST_THREAD_SUCCESS;
	}
	if (status != ST_THREAD_SUCCESS && reader != NULL
	    && reader->count == 0)
		remove_reader(lock, reader);
	(void)pthread_mutex_unlock(&lock->mutex);
	return status;
}

int
st_rwlock_write_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	int result;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (pthread_mutex_lock(&lock->mutex) != 0)
		return ST_THREAD_ERROR;
	if (lock->writers_waiting == UINT_MAX) {
		(void)pthread_mutex_unlock(&lock->mutex);
		return ST_THREAD_ERROR;
	}
	++lock->writers_waiting;
	while (lock->writer || lock->readers != 0) {
		result = pthread_cond_wait(&lock->writers_ready, &lock->mutex);
		if (result != 0) {
			--lock->writers_waiting;
			if (lock->writers_waiting != 0)
				(void)pthread_cond_signal(&lock->writers_ready);
			else if (!lock->writer)
				(void)pthread_cond_broadcast(&lock->readers_ready);
			(void)pthread_mutex_unlock(&lock->mutex);
			return ST_THREAD_ERROR;
		}
	}
	--lock->writers_waiting;
	lock->writer = true;
	lock->writer_thread = pthread_self();
	(void)pthread_mutex_unlock(&lock->mutex);
	return ST_THREAD_SUCCESS;
}

int
st_rwlock_try_write_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	int status;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (pthread_mutex_lock(&lock->mutex) != 0)
		return ST_THREAD_ERROR;
	if (lock->writer || lock->readers != 0)
		status = ST_THREAD_BUSY;
	else {
		lock->writer = true;
		lock->writer_thread = pthread_self();
		status = ST_THREAD_SUCCESS;
	}
	(void)pthread_mutex_unlock(&lock->mutex);
	return status;
}

int
st_rwlock_unlock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	struct st_rwlock_reader *reader;
	struct st_rwlock_reader **previous;
	int status = ST_THREAD_ERROR;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (pthread_mutex_lock(&lock->mutex) != 0)
		return ST_THREAD_ERROR;
	if (lock->writer) {
		if (pthread_equal(lock->writer_thread, pthread_self()) != 0) {
			lock->writer = false;
			if (lock->writers_waiting != 0)
				(void)pthread_cond_signal(&lock->writers_ready);
			else
				(void)pthread_cond_broadcast(&lock->readers_ready);
			status = ST_THREAD_SUCCESS;
		}
	}
	else {
		reader = find_reader(lock, &previous, false);
		if (reader != NULL && reader->count != 0) {
			--reader->count;
			--lock->readers;
			if (reader->count == 0) {
				*previous = reader->next;
				free(reader);
			}
			if (lock->readers == 0 && lock->writers_waiting != 0)
				(void)pthread_cond_signal(&lock->writers_ready);
			status = ST_THREAD_SUCCESS;
		}
	}
	(void)pthread_mutex_unlock(&lock->mutex);
	return status;
}

int
st_rwlock_destroy(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	int result = ST_THREAD_SUCCESS;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (pthread_mutex_lock(&lock->mutex) != 0)
		return ST_THREAD_ERROR;
	if (lock->writer || lock->readers != 0 || lock->writers_waiting != 0
	    || lock->reader_threads != NULL) {
		(void)pthread_mutex_unlock(&lock->mutex);
		return ST_THREAD_BUSY;
	}
	(void)pthread_mutex_unlock(&lock->mutex);
	if (pthread_cond_destroy(&lock->readers_ready) != 0)
		result = ST_THREAD_ERROR;
	if (pthread_cond_destroy(&lock->writers_ready) != 0)
		result = ST_THREAD_ERROR;
	if (pthread_mutex_destroy(&lock->mutex) != 0)
		result = ST_THREAD_ERROR;
	free(lock);
	*handle = NULL;
	return result;
}

#endif
