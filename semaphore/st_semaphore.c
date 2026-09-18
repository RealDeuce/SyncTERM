/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_semaphore.h"

#include <stdbool.h>
#include <stdlib.h>

struct st_semaphore {
	st_mutex_t mutex;
	st_condition_t condition;
	size_t count;
	size_t maximum_count;
};

int
st_semaphore_create(st_semaphore_t **result, size_t initial_count,
    size_t maximum_count)
{
	st_semaphore_t *semaphore;
	int status;

	if (result == NULL || maximum_count == 0
	    || initial_count > maximum_count)
		return ST_THREAD_ERROR;
	semaphore = malloc(sizeof(*semaphore));
	if (semaphore == NULL)
		return ST_THREAD_NO_MEMORY;
	status = st_mutex_init(&semaphore->mutex, ST_MUTEX_PLAIN);
	if (status != ST_THREAD_SUCCESS) {
		free(semaphore);
		return status;
	}
	status = st_condition_init(&semaphore->condition);
	if (status != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&semaphore->mutex);
		free(semaphore);
		return status;
	}
	semaphore->count = initial_count;
	semaphore->maximum_count = maximum_count;
	*result = semaphore;
	return ST_THREAD_SUCCESS;
}

int
st_semaphore_post(st_semaphore_t *semaphore)
{
	int notify_status;
	int unlock_status;
	int status;

	if (semaphore == NULL)
		return ST_THREAD_ERROR;
	status = st_mutex_lock(&semaphore->mutex);
	if (status != ST_THREAD_SUCCESS)
		return status;
	if (semaphore->count == semaphore->maximum_count) {
		status = ST_THREAD_ERROR;
	}
	else {
		++semaphore->count;
		notify_status = st_condition_signal(&semaphore->condition);
		if (notify_status != ST_THREAD_SUCCESS)
			status = notify_status;
	}
	unlock_status = st_mutex_unlock(&semaphore->mutex);
	return status != ST_THREAD_SUCCESS ? status : unlock_status;
}

static int
semaphore_wait(st_semaphore_t *semaphore, const struct timespec *deadline,
    bool polling)
{
	int status;
	int unlock_status;

	if (semaphore == NULL)
		return ST_THREAD_ERROR;
	status = st_mutex_lock(&semaphore->mutex);
	if (status != ST_THREAD_SUCCESS)
		return status;
	while (semaphore->count == 0) {
		if (polling) {
			status = ST_THREAD_BUSY;
			break;
		}
		if (deadline == NULL)
			status = st_condition_wait(&semaphore->condition,
			    &semaphore->mutex);
		else
			status = st_condition_timedwait(&semaphore->condition,
			    &semaphore->mutex, deadline);
		if (status != ST_THREAD_SUCCESS)
			break;
	}
	if (status == ST_THREAD_SUCCESS)
		--semaphore->count;
	unlock_status = st_mutex_unlock(&semaphore->mutex);
	return unlock_status == ST_THREAD_SUCCESS ? status : unlock_status;
}

int
st_semaphore_wait(st_semaphore_t *semaphore)
{
	return semaphore_wait(semaphore, NULL, false);
}

int
st_semaphore_trywait(st_semaphore_t *semaphore)
{
	return semaphore_wait(semaphore, NULL, true);
}

int
st_semaphore_timedwait(st_semaphore_t *semaphore,
    const struct timespec *deadline)
{
	if (deadline == NULL)
		return ST_THREAD_ERROR;
	return semaphore_wait(semaphore, deadline, false);
}

void
st_semaphore_destroy(st_semaphore_t *semaphore)
{
	if (semaphore == NULL)
		return;
	st_condition_destroy(&semaphore->condition);
	st_mutex_destroy(&semaphore->mutex);
	free(semaphore);
}
