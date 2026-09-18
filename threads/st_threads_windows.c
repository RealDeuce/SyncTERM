/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_threads.h"

#include <process.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <windows.h>

struct st_mutex_impl {
	int type;
	union {
		SRWLOCK plain;
		CRITICAL_SECTION recursive;
	} native;
};

struct st_condition_impl {
	CONDITION_VARIABLE native;
};

struct st_thread_storage_impl {
	DWORD index;
	st_thread_storage_destructor_t destructor;
	atomic_uint references;
	atomic_bool active;
};

struct thread_storage_value {
	struct st_thread_storage_impl *storage;
	void *value;
	struct thread_storage_value *next;
};

struct thread_arguments {
	st_thread_start_t function;
	void *argument;
};

struct once_arguments {
	void (*function)(void);
};

static ST_THREAD_LOCAL struct thread_storage_value *thread_storage_values;
static ST_THREAD_LOCAL bool thread_storage_cleanup_active;

static void thread_storage_cleanup(void);

_Static_assert(sizeof(st_once_flag_t) == sizeof(INIT_ONCE),
    "st_once_flag_t must hold INIT_ONCE");
_Static_assert(_Alignof(st_once_flag_t) >= _Alignof(INIT_ONCE),
    "st_once_flag_t must have INIT_ONCE alignment");

static bool
valid_mutex_type(int type)
{
	return type == ST_MUTEX_PLAIN || type == ST_MUTEX_RECURSIVE
	    || type == ST_MUTEX_TIMED
	    || type == (ST_MUTEX_RECURSIVE | ST_MUTEX_TIMED);
}

static bool
valid_timespec(const struct timespec *value)
{
	return value != NULL && value->tv_nsec >= 0
	    && value->tv_nsec < 1000000000L;
}

static DWORD
milliseconds_until(const struct timespec *deadline, bool *expired)
{
	struct timespec now;
	uintmax_t seconds;
	long nanoseconds;
	uint64_t milliseconds;

	if (!valid_timespec(deadline)
	    || timespec_get(&now, TIME_UTC) != TIME_UTC) {
		*expired = true;
		return 0;
	}
	if (deadline->tv_sec < now.tv_sec
	    || (deadline->tv_sec == now.tv_sec
	    && deadline->tv_nsec <= now.tv_nsec)) {
		*expired = true;
		return 0;
	}
	seconds = (uintmax_t)deadline->tv_sec - (uintmax_t)now.tv_sec;
	nanoseconds = deadline->tv_nsec - now.tv_nsec;
	if (nanoseconds < 0) {
		--seconds;
		nanoseconds += 1000000000L;
	}
	*expired = false;
	if (seconds > (uintmax_t)(INFINITE - 1U) / 1000U)
		return INFINITE - 1U;
	milliseconds = (uint64_t)seconds * 1000U;
	milliseconds += ((uint64_t)nanoseconds + 999999U) / 1000000U;
	if (milliseconds >= INFINITE)
		return INFINITE - 1U;
	return (DWORD)milliseconds;
}

static BOOL CALLBACK
once_entry(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
	struct once_arguments *arguments = parameter;

	(void)once;
	(void)context;
	arguments->function();
	return TRUE;
}

static unsigned __stdcall
thread_entry(void *argument)
{
	struct thread_arguments arguments = *(struct thread_arguments *)argument;
	int result;

	free(argument);
	result = arguments.function(arguments.argument);
	thread_storage_cleanup();
	return (unsigned)result;
}

static void
thread_storage_release(struct st_thread_storage_impl *storage)
{
	if (atomic_fetch_sub_explicit(&storage->references, 1,
	    memory_order_acq_rel) == 1)
		free(storage);
}

static void
thread_storage_unlink(struct thread_storage_value *stored)
{
	struct thread_storage_value **link = &thread_storage_values;

	while (*link != NULL && *link != stored)
		link = &(*link)->next;
	if (*link == stored)
		*link = stored->next;
}

static void
thread_storage_cleanup(void)
{
	struct thread_storage_value *stored;
	unsigned iteration;

	thread_storage_cleanup_active = true;
	for (iteration = 0;
	    iteration < ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS; ++iteration) {
		bool invoked = false;

		for (stored = thread_storage_values; stored != NULL;
		    stored = stored->next) {
			st_thread_storage_destructor_t destructor;
			void *value;

			if (!atomic_load_explicit(&stored->storage->active,
			    memory_order_acquire) || stored->value == NULL)
				continue;
			destructor = stored->storage->destructor;
			if (destructor == NULL)
				continue;
			value = stored->value;
			stored->value = NULL;
			invoked = true;
			destructor(value);
		}
		if (!invoked)
			break;
	}
	while (thread_storage_values != NULL) {
		stored = thread_storage_values;
		thread_storage_values = stored->next;
		if (atomic_load_explicit(&stored->storage->active,
		    memory_order_acquire)
		    && TlsGetValue(stored->storage->index) == stored)
			(void)TlsSetValue(stored->storage->index, NULL);
		thread_storage_release(stored->storage);
		free(stored);
	}
	thread_storage_cleanup_active = false;
}

void
st_call_once(st_once_flag_t *flag, void (*function)(void))
{
	struct once_arguments arguments = {function};

	(void)InitOnceExecuteOnce((PINIT_ONCE)flag, once_entry, &arguments, NULL);
}

int
st_condition_broadcast(st_condition_t *condition)
{
	WakeAllConditionVariable(&(*condition)->native);
	return ST_THREAD_SUCCESS;
}

void
st_condition_destroy(st_condition_t *condition)
{
	free(*condition);
	*condition = NULL;
}

int
st_condition_init(st_condition_t *condition)
{
	struct st_condition_impl *created = malloc(sizeof(*created));

	if (created == NULL)
		return ST_THREAD_NO_MEMORY;
	InitializeConditionVariable(&created->native);
	*condition = created;
	return ST_THREAD_SUCCESS;
}

int
st_condition_signal(st_condition_t *condition)
{
	WakeConditionVariable(&(*condition)->native);
	return ST_THREAD_SUCCESS;
}

int
st_condition_timedwait(st_condition_t *restrict condition,
    st_mutex_t *restrict mutex, const struct timespec *restrict deadline)
{
	bool expired;
	BOOL result;
	DWORD timeout;

	if (!valid_timespec(deadline))
		return ST_THREAD_ERROR;
	for (;;) {
		timeout = milliseconds_until(deadline, &expired);
		if (expired)
			return ST_THREAD_TIMED_OUT;
		if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
			result = SleepConditionVariableCS(&(*condition)->native,
			    &(*mutex)->native.recursive, timeout);
		else
			result = SleepConditionVariableSRW(&(*condition)->native,
			    &(*mutex)->native.plain, timeout, 0);
		if (result)
			return ST_THREAD_SUCCESS;
		if (GetLastError() != ERROR_TIMEOUT)
			return ST_THREAD_ERROR;
	}
}

int
st_condition_wait(st_condition_t *condition, st_mutex_t *mutex)
{
	BOOL result;

	if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
		result = SleepConditionVariableCS(&(*condition)->native,
		    &(*mutex)->native.recursive, INFINITE);
	else
		result = SleepConditionVariableSRW(&(*condition)->native,
		    &(*mutex)->native.plain, INFINITE, 0);
	return result ? ST_THREAD_SUCCESS : ST_THREAD_ERROR;
}

void
st_mutex_destroy(st_mutex_t *mutex)
{
	if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
		DeleteCriticalSection(&(*mutex)->native.recursive);
	free(*mutex);
	*mutex = NULL;
}

int
st_mutex_init(st_mutex_t *mutex, int type)
{
	struct st_mutex_impl *created;

	if (!valid_mutex_type(type))
		return ST_THREAD_ERROR;
	created = malloc(sizeof(*created));
	if (created == NULL)
		return ST_THREAD_NO_MEMORY;
	created->type = type;
	if ((type & ST_MUTEX_RECURSIVE) != 0)
		InitializeCriticalSection(&created->native.recursive);
	else
		InitializeSRWLock(&created->native.plain);
	*mutex = created;
	return ST_THREAD_SUCCESS;
}

int
st_mutex_lock(st_mutex_t *mutex)
{
	if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
		EnterCriticalSection(&(*mutex)->native.recursive);
	else
		AcquireSRWLockExclusive(&(*mutex)->native.plain);
	return ST_THREAD_SUCCESS;
}

int
st_mutex_timedlock(st_mutex_t *restrict mutex,
    const struct timespec *restrict deadline)
{
	bool expired;
	bool locked;

	if (((*mutex)->type & ST_MUTEX_TIMED) == 0
	    || !valid_timespec(deadline))
		return ST_THREAD_ERROR;
	for (;;) {
		if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
			locked = TryEnterCriticalSection(
			    &(*mutex)->native.recursive) != 0;
		else
			locked = TryAcquireSRWLockExclusive(
			    &(*mutex)->native.plain) != 0;
		if (locked)
			return ST_THREAD_SUCCESS;
		(void)milliseconds_until(deadline, &expired);
		if (expired)
			return ST_THREAD_TIMED_OUT;
		Sleep(1);
	}
}

int
st_mutex_trylock(st_mutex_t *mutex)
{
	bool locked;

	if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
		locked = TryEnterCriticalSection(&(*mutex)->native.recursive) != 0;
	else
		locked = TryAcquireSRWLockExclusive(&(*mutex)->native.plain) != 0;
	return locked ? ST_THREAD_SUCCESS : ST_THREAD_BUSY;
}

int
st_mutex_unlock(st_mutex_t *mutex)
{
	if (((*mutex)->type & ST_MUTEX_RECURSIVE) != 0)
		LeaveCriticalSection(&(*mutex)->native.recursive);
	else
		ReleaseSRWLockExclusive(&(*mutex)->native.plain);
	return ST_THREAD_SUCCESS;
}

int
st_thread_create(st_thread_t *thread, st_thread_start_t function,
    void *argument)
{
	struct thread_arguments *arguments = malloc(sizeof(*arguments));
	uintptr_t handle;
	unsigned id;

	if (arguments == NULL)
		return ST_THREAD_NO_MEMORY;
	arguments->function = function;
	arguments->argument = argument;
	handle = _beginthreadex(NULL, 0, thread_entry, arguments, 0, &id);
	if (handle == 0) {
		free(arguments);
		return ST_THREAD_ERROR;
	}
	thread->handle = handle;
	thread->id = id;
	return ST_THREAD_SUCCESS;
}

st_thread_t
st_thread_current(void)
{
	st_thread_t current = {0, GetCurrentThreadId()};

	return current;
}

int
st_thread_detach(st_thread_t thread)
{
	if (thread.handle == 0
	    || !CloseHandle((HANDLE)(uintptr_t)thread.handle))
		return ST_THREAD_ERROR;
	return ST_THREAD_SUCCESS;
}

bool
st_thread_equal(st_thread_t left, st_thread_t right)
{
	return left.id == right.id;
}

_Noreturn void
st_thread_exit(int result)
{
	thread_storage_cleanup();
	_endthreadex((unsigned)result);
	abort();
}

int
st_thread_join(st_thread_t thread, int *result)
{
	HANDLE handle = (HANDLE)(uintptr_t)thread.handle;
	DWORD code;

	if (handle == NULL || WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0)
		return ST_THREAD_ERROR;
	if (!GetExitCodeThread(handle, &code)) {
		(void)CloseHandle(handle);
		return ST_THREAD_ERROR;
	}
	if (!CloseHandle(handle))
		return ST_THREAD_ERROR;
	if (result != NULL)
		*result = (int)code;
	return ST_THREAD_SUCCESS;
}

int
st_thread_sleep(const struct timespec *duration, struct timespec *remaining)
{
	time_t seconds;
	DWORD milliseconds;

	if (!valid_timespec(duration) || duration->tv_sec < 0)
		return -1;
	seconds = duration->tv_sec;
	while (seconds >= 3600) {
		Sleep(3600000);
		seconds -= 3600;
	}
	milliseconds = (DWORD)seconds * 1000U;
	milliseconds += (DWORD)((duration->tv_nsec + 999999L) / 1000000L);
	Sleep(milliseconds);
	if (remaining != NULL) {
		remaining->tv_sec = 0;
		remaining->tv_nsec = 0;
	}
	return 0;
}

void
st_thread_yield(void)
{
	(void)SwitchToThread();
}

int
st_thread_storage_create(st_thread_storage_t *storage,
    st_thread_storage_destructor_t destructor)
{
	struct st_thread_storage_impl *created = malloc(sizeof(*created));

	if (created == NULL)
		return ST_THREAD_NO_MEMORY;
	created->destructor = destructor;
	atomic_init(&created->references, 1);
	atomic_init(&created->active, true);
	created->index = TlsAlloc();
	if (created->index == TLS_OUT_OF_INDEXES) {
		free(created);
		return ST_THREAD_ERROR;
	}
	*storage = created;
	return ST_THREAD_SUCCESS;
}

void
st_thread_storage_delete(st_thread_storage_t storage)
{
	struct thread_storage_value *stored = TlsGetValue(storage->index);

	atomic_store_explicit(&storage->active, false, memory_order_release);
	(void)TlsFree(storage->index);
	if (!thread_storage_cleanup_active && stored != NULL
	    && stored->storage == storage) {
		thread_storage_unlink(stored);
		thread_storage_release(storage);
		free(stored);
	}
	thread_storage_release(storage);
}

void *
st_thread_storage_get(st_thread_storage_t storage)
{
	struct thread_storage_value *stored;

	if (!atomic_load_explicit(&storage->active, memory_order_acquire))
		return NULL;
	stored = TlsGetValue(storage->index);
	return stored == NULL || stored->storage != storage
	    ? NULL : stored->value;
}

int
st_thread_storage_set(st_thread_storage_t storage, void *value)
{
	struct thread_storage_value *stored;

	if (!atomic_load_explicit(&storage->active, memory_order_acquire))
		return ST_THREAD_ERROR;
	stored = TlsGetValue(storage->index);
	if (stored != NULL && stored->storage != storage)
		stored = NULL;
	if (value == NULL) {
		if (stored == NULL)
			return ST_THREAD_SUCCESS;
		if (!TlsSetValue(storage->index, NULL))
			return ST_THREAD_ERROR;
		stored->value = NULL;
		if (!thread_storage_cleanup_active) {
			thread_storage_unlink(stored);
			thread_storage_release(storage);
			free(stored);
		}
		return ST_THREAD_SUCCESS;
	}
	if (stored == NULL) {
		stored = malloc(sizeof(*stored));
		if (stored == NULL)
			return ST_THREAD_NO_MEMORY;
		stored->storage = storage;
		stored->value = value;
		if (!TlsSetValue(storage->index, stored)) {
			free(stored);
			return ST_THREAD_ERROR;
		}
		(void)atomic_fetch_add_explicit(&storage->references, 1,
		    memory_order_relaxed);
		stored->next = thread_storage_values;
		thread_storage_values = stored;
	}
	stored->value = value;
	return ST_THREAD_SUCCESS;
}
