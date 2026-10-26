/* Namespaced C11-style threads and synchronization. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_THREADS_H_
#define ST_THREADS_H_

#include "st_threads_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if ST_THREADS_BACKEND_PTHREAD
	#include <limits.h>
	#include <pthread.h>

	typedef pthread_t st_thread_t;
	typedef pthread_mutex_t st_mutex_t;
	typedef pthread_cond_t st_condition_t;
	typedef pthread_once_t st_once_flag_t;
	typedef pthread_key_t st_thread_storage_t;
	#if ST_THREADS_HAVE_PTHREAD_RWLOCK
		typedef pthread_rwlock_t st_rwlock_t;
	#else
		typedef struct st_rwlock_impl *st_rwlock_t;
	#endif

	#define ST_ONCE_FLAG_INIT PTHREAD_ONCE_INIT
	#if defined(PTHREAD_DESTRUCTOR_ITERATIONS)
		#define ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS \
		    PTHREAD_DESTRUCTOR_ITERATIONS
	#elif defined(_POSIX_THREAD_DESTRUCTOR_ITERATIONS)
		#define ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS \
		    _POSIX_THREAD_DESTRUCTOR_ITERATIONS
	#else
		#define ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS 4
	#endif
#elif ST_THREADS_BACKEND_WINDOWS
	typedef struct {
		uintptr_t handle;
		unsigned long id;
	} st_thread_t;
	typedef struct st_mutex_impl *st_mutex_t;
	typedef struct st_condition_impl *st_condition_t;
	typedef struct {
		void *state;
	} st_once_flag_t;
	typedef struct st_thread_storage_impl *st_thread_storage_t;
	typedef struct st_rwlock_impl *st_rwlock_t;

	#define ST_ONCE_FLAG_INIT { NULL }
	#define ST_THREAD_STORAGE_DESTRUCTOR_ITERATIONS 4
#else
	#error "No SyncTERM thread backend was selected"
#endif

#define ST_THREAD_LOCAL _Thread_local

typedef int (*st_thread_start_t)(void *);
typedef void (*st_thread_storage_destructor_t)(void *);

enum st_thread_result {
	ST_THREAD_SUCCESS,
	ST_THREAD_BUSY,
	ST_THREAD_ERROR,
	ST_THREAD_NO_MEMORY,
	ST_THREAD_TIMED_OUT,
};

enum st_mutex_type {
	ST_MUTEX_PLAIN = 0,
	ST_MUTEX_RECURSIVE = 1U << 0,
	ST_MUTEX_TIMED = 1U << 1,
};

void st_call_once(st_once_flag_t *flag, void (*function)(void));

int st_condition_broadcast(st_condition_t *condition);
void st_condition_destroy(st_condition_t *condition);
int st_condition_init(st_condition_t *condition);
int st_condition_signal(st_condition_t *condition);
int st_condition_timedwait(st_condition_t *restrict condition,
    st_mutex_t *restrict mutex, const struct timespec *restrict deadline);
int st_condition_wait(st_condition_t *condition, st_mutex_t *mutex);

void st_mutex_destroy(st_mutex_t *mutex);
int st_mutex_init(st_mutex_t *mutex, int type);
int st_mutex_lock(st_mutex_t *mutex);
int st_mutex_timedlock(st_mutex_t *restrict mutex,
    const struct timespec *restrict deadline);
int st_mutex_trylock(st_mutex_t *mutex);
int st_mutex_unlock(st_mutex_t *mutex);

/*
 * Read locks are recursive; write locks and read-to-write upgrades are not.
 * Each successful acquisition is released once, by the acquiring thread.
 * Destruction requires external lifetime control and an unlocked lock.
 */
int st_rwlock_init(st_rwlock_t *lock);
int st_rwlock_read_lock(st_rwlock_t *lock);
int st_rwlock_try_read_lock(st_rwlock_t *lock);
int st_rwlock_write_lock(st_rwlock_t *lock);
int st_rwlock_try_write_lock(st_rwlock_t *lock);
int st_rwlock_unlock(st_rwlock_t *lock);
int st_rwlock_destroy(st_rwlock_t *lock);

int st_thread_create(st_thread_t *thread, st_thread_start_t function,
    void *argument);
st_thread_t st_thread_current(void);
int st_thread_detach(st_thread_t thread);
bool st_thread_equal(st_thread_t left, st_thread_t right);
_Noreturn void st_thread_exit(int result);
int st_thread_join(st_thread_t thread, int *result);
int st_thread_sleep(const struct timespec *duration,
    struct timespec *remaining);
void st_thread_yield(void);

int st_thread_storage_create(st_thread_storage_t *storage,
    st_thread_storage_destructor_t destructor);
void st_thread_storage_delete(st_thread_storage_t storage);
void *st_thread_storage_get(st_thread_storage_t storage);
int st_thread_storage_set(st_thread_storage_t storage, void *value);

#endif
