/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_event.h"

#include <stdlib.h>

struct st_event {
	st_mutex_t mutex;
	st_condition_t condition;
	enum st_event_reset_mode mode;
	bool set;
};

static bool
valid_mode(enum st_event_reset_mode mode)
{
	return mode == ST_EVENT_AUTO_RESET || mode == ST_EVENT_MANUAL_RESET;
}

int
st_event_create(st_event_t **result, enum st_event_reset_mode mode,
    bool initially_set)
{
	st_event_t *event;
	int status;

	if (result == NULL || !valid_mode(mode))
		return ST_THREAD_ERROR;
	event = malloc(sizeof(*event));
	if (event == NULL)
		return ST_THREAD_NO_MEMORY;
	status = st_mutex_init(&event->mutex, ST_MUTEX_PLAIN);
	if (status != ST_THREAD_SUCCESS) {
		free(event);
		return status;
	}
	status = st_condition_init(&event->condition);
	if (status != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&event->mutex);
		free(event);
		return status;
	}
	event->mode = mode;
	event->set = initially_set;
	*result = event;
	return ST_THREAD_SUCCESS;
}

int
st_event_set(st_event_t *event)
{
	int notify_status = ST_THREAD_SUCCESS;
	int unlock_status;
	int status;

	if (event == NULL)
		return ST_THREAD_ERROR;
	status = st_mutex_lock(&event->mutex);
	if (status != ST_THREAD_SUCCESS)
		return status;
	if (!event->set) {
		event->set = true;
		if (event->mode == ST_EVENT_MANUAL_RESET)
			notify_status = st_condition_broadcast(&event->condition);
		else
			notify_status = st_condition_signal(&event->condition);
	}
	unlock_status = st_mutex_unlock(&event->mutex);
	return notify_status != ST_THREAD_SUCCESS
	    ? notify_status : unlock_status;
}

int
st_event_reset(st_event_t *event)
{
	int unlock_status;
	int status;

	if (event == NULL)
		return ST_THREAD_ERROR;
	status = st_mutex_lock(&event->mutex);
	if (status != ST_THREAD_SUCCESS)
		return status;
	event->set = false;
	unlock_status = st_mutex_unlock(&event->mutex);
	return unlock_status;
}

static int
event_wait(st_event_t *event, const struct timespec *deadline, bool polling)
{
	int status;
	int unlock_status;

	if (event == NULL)
		return ST_THREAD_ERROR;
	status = st_mutex_lock(&event->mutex);
	if (status != ST_THREAD_SUCCESS)
		return status;
	while (!event->set) {
		if (polling) {
			status = ST_THREAD_BUSY;
			break;
		}
		if (deadline == NULL)
			status = st_condition_wait(&event->condition,
			    &event->mutex);
		else
			status = st_condition_timedwait(&event->condition,
			    &event->mutex, deadline);
		if (status != ST_THREAD_SUCCESS)
			break;
	}
	if (status == ST_THREAD_SUCCESS && event->mode == ST_EVENT_AUTO_RESET)
		event->set = false;
	unlock_status = st_mutex_unlock(&event->mutex);
	return unlock_status == ST_THREAD_SUCCESS ? status : unlock_status;
}

int
st_event_wait(st_event_t *event)
{
	return event_wait(event, NULL, false);
}

int
st_event_trywait(st_event_t *event)
{
	return event_wait(event, NULL, true);
}

int
st_event_timedwait(st_event_t *event, const struct timespec *deadline)
{
	if (deadline == NULL)
		return ST_THREAD_ERROR;
	return event_wait(event, deadline, false);
}

void
st_event_destroy(st_event_t *event)
{
	if (event == NULL)
		return;
	st_condition_destroy(&event->condition);
	st_mutex_destroy(&event->mutex);
	free(event);
}
