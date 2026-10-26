/* Windows read/write lock with recursive readers and writer preference. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_threads.h"

#include <limits.h>
#include <stdlib.h>
#include <windows.h>

struct st_rwlock_reader {
	struct st_rwlock_reader *next;
	DWORD thread;
	unsigned count;
};

struct st_rwlock_impl {
	CRITICAL_SECTION state_lock;
	CRITICAL_SECTION writer_lock;
	HANDLE zero_readers;
	HANDLE zero_writers;
	unsigned readers;
	unsigned writers;
	unsigned writers_waiting;
	DWORD writer_thread;
	struct st_rwlock_reader *reader_threads;
};

static struct st_rwlock_reader *
find_reader(struct st_rwlock_impl *lock, struct st_rwlock_reader ***previous,
	bool create)
{
	struct st_rwlock_reader *reader;
	DWORD self = GetCurrentThreadId();

	if (previous != NULL)
		*previous = &lock->reader_threads;
	for (reader = lock->reader_threads; reader != NULL;
	    reader = reader->next) {
		if (reader->thread == self)
			return reader;
		if (previous != NULL)
			*previous = &reader->next;
	}
	if (!create)
		return NULL;
	reader = calloc(1, sizeof(*reader));
	if (reader == NULL)
		return NULL;
	reader->next = lock->reader_threads;
	reader->thread = self;
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

	if (handle == NULL)
		return ST_THREAD_ERROR;
	*handle = NULL;
	lock = calloc(1, sizeof(*lock));
	if (lock == NULL)
		return ST_THREAD_NO_MEMORY;
	if (!InitializeCriticalSectionAndSpinCount(&lock->state_lock, 0))
		goto fail_lock;
	if (!InitializeCriticalSectionAndSpinCount(&lock->writer_lock, 0)) {
		DeleteCriticalSection(&lock->state_lock);
		goto fail_lock;
	}
	lock->zero_readers = CreateEventW(NULL, TRUE, TRUE, NULL);
	if (lock->zero_readers == NULL) {
		DeleteCriticalSection(&lock->writer_lock);
		DeleteCriticalSection(&lock->state_lock);
		goto fail_lock;
	}
	lock->zero_writers = CreateEventW(NULL, TRUE, TRUE, NULL);
	if (lock->zero_writers == NULL) {
		(void)CloseHandle(lock->zero_readers);
		DeleteCriticalSection(&lock->writer_lock);
		DeleteCriticalSection(&lock->state_lock);
		goto fail_lock;
	}
	lock->writer_thread = UINT32_MAX;
	*handle = lock;
	return ST_THREAD_SUCCESS;

fail_lock:
	free(lock);
	return ST_THREAD_NO_MEMORY;
}

int
st_rwlock_read_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	struct st_rwlock_reader *reader;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	EnterCriticalSection(&lock->state_lock);
	reader = find_reader(lock, NULL, true);
	if (reader == NULL) {
		LeaveCriticalSection(&lock->state_lock);
		return ST_THREAD_NO_MEMORY;
	}
	while (reader->count == 0
	    && (lock->writers != 0 || lock->writers_waiting != 0)) {
		LeaveCriticalSection(&lock->state_lock);
		if (WaitForSingleObject(lock->zero_writers, INFINITE)
		    != WAIT_OBJECT_0) {
			EnterCriticalSection(&lock->state_lock);
			remove_reader(lock, reader);
			LeaveCriticalSection(&lock->state_lock);
			return ST_THREAD_ERROR;
		}
		EnterCriticalSection(&lock->writer_lock);
		EnterCriticalSection(&lock->state_lock);
		if (lock->writers != 0 || lock->writers_waiting != 0) {
			LeaveCriticalSection(&lock->state_lock);
			LeaveCriticalSection(&lock->writer_lock);
			Sleep(1);
			EnterCriticalSection(&lock->state_lock);
			continue;
		}
		if (lock->readers == UINT_MAX || reader->count == UINT_MAX) {
			remove_reader(lock, reader);
			LeaveCriticalSection(&lock->state_lock);
			LeaveCriticalSection(&lock->writer_lock);
			return ST_THREAD_ERROR;
		}
		++lock->readers;
		++reader->count;
		(void)ResetEvent(lock->zero_readers);
		LeaveCriticalSection(&lock->state_lock);
		LeaveCriticalSection(&lock->writer_lock);
		return ST_THREAD_SUCCESS;
	}
	if (lock->readers == UINT_MAX || reader->count == UINT_MAX) {
		if (reader->count == 0)
			remove_reader(lock, reader);
		LeaveCriticalSection(&lock->state_lock);
		return ST_THREAD_ERROR;
	}
	++lock->readers;
	++reader->count;
	(void)ResetEvent(lock->zero_readers);
	LeaveCriticalSection(&lock->state_lock);
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
	EnterCriticalSection(&lock->state_lock);
	reader = find_reader(lock, NULL, true);
	if (reader == NULL)
		status = ST_THREAD_NO_MEMORY;
	else if (reader->count == 0
	    && (lock->writers != 0 || lock->writers_waiting != 0))
		status = ST_THREAD_BUSY;
	else if (lock->readers == UINT_MAX || reader->count == UINT_MAX)
		status = ST_THREAD_ERROR;
	else {
		++lock->readers;
		++reader->count;
		(void)ResetEvent(lock->zero_readers);
		status = ST_THREAD_SUCCESS;
	}
	if (status != ST_THREAD_SUCCESS && reader != NULL
	    && reader->count == 0)
		remove_reader(lock, reader);
	LeaveCriticalSection(&lock->state_lock);
	return status;
}

int
st_rwlock_write_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	EnterCriticalSection(&lock->state_lock);
	if (lock->writers_waiting == UINT_MAX) {
		LeaveCriticalSection(&lock->state_lock);
		return ST_THREAD_ERROR;
	}
	++lock->writers_waiting;
	(void)ResetEvent(lock->zero_writers);
	LeaveCriticalSection(&lock->state_lock);
	EnterCriticalSection(&lock->writer_lock);
	EnterCriticalSection(&lock->state_lock);
	while (lock->readers != 0) {
		LeaveCriticalSection(&lock->state_lock);
		LeaveCriticalSection(&lock->writer_lock);
		if (WaitForSingleObject(lock->zero_readers, INFINITE)
		    != WAIT_OBJECT_0) {
			EnterCriticalSection(&lock->state_lock);
			--lock->writers_waiting;
			if (lock->writers_waiting == 0 && lock->writers == 0)
				(void)SetEvent(lock->zero_writers);
			LeaveCriticalSection(&lock->state_lock);
			return ST_THREAD_ERROR;
		}
		EnterCriticalSection(&lock->writer_lock);
		EnterCriticalSection(&lock->state_lock);
	}
	--lock->writers_waiting;
	if (lock->writers != 0) {
		LeaveCriticalSection(&lock->state_lock);
		LeaveCriticalSection(&lock->writer_lock);
		return ST_THREAD_ERROR;
	}
	++lock->writers;
	lock->writer_thread = GetCurrentThreadId();
	(void)ResetEvent(lock->zero_writers);
	LeaveCriticalSection(&lock->state_lock);
	return ST_THREAD_SUCCESS;
}

int
st_rwlock_try_write_lock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	int status = ST_THREAD_BUSY;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	if (!TryEnterCriticalSection(&lock->writer_lock))
		return ST_THREAD_BUSY;
	EnterCriticalSection(&lock->state_lock);
	if (lock->readers == 0 && lock->writers == 0) {
		++lock->writers;
		lock->writer_thread = GetCurrentThreadId();
		(void)ResetEvent(lock->zero_writers);
		status = ST_THREAD_SUCCESS;
	}
	LeaveCriticalSection(&lock->state_lock);
	if (status != ST_THREAD_SUCCESS)
		LeaveCriticalSection(&lock->writer_lock);
	return status;
}

int
st_rwlock_unlock(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	struct st_rwlock_reader *reader;
	struct st_rwlock_reader **previous;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	EnterCriticalSection(&lock->state_lock);
	if (lock->writers != 0) {
		if (lock->writer_thread != GetCurrentThreadId()) {
			LeaveCriticalSection(&lock->state_lock);
			return ST_THREAD_ERROR;
		}
		--lock->writers;
		lock->writer_thread = UINT32_MAX;
		if (lock->writers_waiting == 0)
			(void)SetEvent(lock->zero_writers);
		LeaveCriticalSection(&lock->state_lock);
		LeaveCriticalSection(&lock->writer_lock);
		return ST_THREAD_SUCCESS;
	}
	reader = find_reader(lock, &previous, false);
	if (reader == NULL || reader->count == 0) {
		LeaveCriticalSection(&lock->state_lock);
		return ST_THREAD_ERROR;
	}
	--reader->count;
	--lock->readers;
	if (reader->count == 0) {
		*previous = reader->next;
		free(reader);
	}
	if (lock->readers == 0)
		(void)SetEvent(lock->zero_readers);
	LeaveCriticalSection(&lock->state_lock);
	return ST_THREAD_SUCCESS;
}

int
st_rwlock_destroy(st_rwlock_t *handle)
{
	struct st_rwlock_impl *lock;
	int status = ST_THREAD_SUCCESS;

	if (handle == NULL || *handle == NULL)
		return ST_THREAD_ERROR;
	lock = *handle;
	EnterCriticalSection(&lock->state_lock);
	if (lock->readers != 0 || lock->writers != 0
	    || lock->writers_waiting != 0 || lock->reader_threads != NULL) {
		LeaveCriticalSection(&lock->state_lock);
		return ST_THREAD_BUSY;
	}
	LeaveCriticalSection(&lock->state_lock);
	if (!CloseHandle(lock->zero_readers))
		status = ST_THREAD_ERROR;
	if (!CloseHandle(lock->zero_writers))
		status = ST_THREAD_ERROR;
	DeleteCriticalSection(&lock->state_lock);
	DeleteCriticalSection(&lock->writer_lock);
	free(lock);
	*handle = NULL;
	return status;
}
