/* Process-local coordination for persistent advisory file locks. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_file_lock_internal.h"

#include "io_error.h"
#include "string_compat.h"
#include "st_threads.h"

#include <stdlib.h>
#include <string.h>

struct st_local_lock {
	char *path;
	size_t readers;
	size_t waiters;
	bool writer;
	st_condition_t condition;
	struct st_local_lock *next;
};

static st_once_flag_t lock_once = ST_ONCE_FLAG_INIT;
static st_mutex_t lock_mutex;
static bool lock_mutex_valid;
static struct st_local_lock *local_locks;

static void
initialize_locks(void)
{
	lock_mutex_valid = st_mutex_init(&lock_mutex, ST_MUTEX_PLAIN)
	    == ST_THREAD_SUCCESS;
}

static struct st_local_lock *
find_local_lock(const char *path)
{
	struct st_local_lock *item;

	for (item = local_locks; item != NULL; item = item->next) {
		if (strcmp(item->path, path) == 0)
			return item;
	}
	return NULL;
}

static struct st_local_lock *
create_local_lock(const char *path)
{
	struct st_local_lock *item;

	item = calloc(1, sizeof(*item));
	if (item == NULL)
		return NULL;
	item->path = strdup(path);
	if (item->path == NULL
	    || st_condition_init(&item->condition) != ST_THREAD_SUCCESS) {
		free(item->path);
		free(item);
		return NULL;
	}
	item->next = local_locks;
	local_locks = item;
	return item;
}

static void
remove_unused_local_lock(struct st_local_lock *item)
{
	struct st_local_lock **link;

	if (item->readers != 0 || item->writer || item->waiters != 0)
		return;
	for (link = &local_locks; *link != NULL; link = &(*link)->next) {
		if (*link == item) {
			*link = item->next;
			st_condition_destroy(&item->condition);
			free(item->path);
			free(item);
			return;
		}
	}
}

static void
release_local_lock(struct st_local_lock *item, bool exclusive)
{
	(void)st_mutex_lock(&lock_mutex);
	if (exclusive)
		item->writer = false;
	else
		item->readers--;
	(void)st_condition_broadcast(&item->condition);
	remove_unused_local_lock(item);
	(void)st_mutex_unlock(&lock_mutex);
}

int
st_file_lock_acquire(st_file_lock_t **result, const char *path,
    bool exclusive)
{
	struct st_local_lock *local;
	st_file_lock_t *lock;
	int status;

	if (result == NULL || path == NULL || path[0] == '\0')
		return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	st_call_once(&lock_once, initialize_locks);
	if (!lock_mutex_valid)
		return IO_ERROR_OUT_OF_MEMORY;
	lock = calloc(1, sizeof(*lock));
	if (lock == NULL)
		return IO_ERROR_OUT_OF_MEMORY;

	(void)st_mutex_lock(&lock_mutex);
	local = find_local_lock(path);
	if (local == NULL)
		local = create_local_lock(path);
	if (local == NULL) {
		(void)st_mutex_unlock(&lock_mutex);
		free(lock);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	local->waiters++;
	while (local->writer || (exclusive && local->readers != 0))
		(void)st_condition_wait(&local->condition, &lock_mutex);
	local->waiters--;
	if (exclusive)
		local->writer = true;
	else
		local->readers++;
	(void)st_mutex_unlock(&lock_mutex);

	lock->local = local;
	lock->exclusive = exclusive;
	status = st_file_lock_platform_acquire(lock, path, exclusive);
	if (status < 0) {
		release_local_lock(local, exclusive);
		free(lock);
		return status;
	}
	*result = lock;
	return 0;
}

void
st_file_lock_release(st_file_lock_t *lock)
{
	if (lock == NULL)
		return;
	st_file_lock_platform_release(lock);
	release_local_lock(lock->local, lock->exclusive);
	free(lock);
}
