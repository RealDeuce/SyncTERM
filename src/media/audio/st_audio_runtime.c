/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_audio_runtime.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdlib.h>
#include <string.h>

struct st_audio_runtime {
	audio_engine_t *engine;
	uint32_t allowed;
	uint32_t midi_allowed;
	st_mutex_t mutex;
	bool pull;
	bool stopped;
};

int
st_audio_runtime_create(st_audio_runtime_t **result, uint32_t allowed_backends)
{
	st_audio_runtime_t *runtime;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	runtime = calloc(1, sizeof(*runtime));
	if (runtime == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&runtime->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(runtime);
		return IO_ERROR_IO;
	}
	runtime->allowed = allowed_backends;
	*result = runtime;
	return 0;
}

int
st_audio_runtime_create_pull(st_audio_runtime_t **result)
{
	int status = st_audio_runtime_create(result, 0);
	if (status == 0)
		(*result)->pull = true;
	return status;
}

int
st_audio_runtime_pull(st_audio_runtime_t *runtime, int16_t *frames,
	size_t frame_count)
{
	int status = 0;
	if (runtime == NULL || !runtime->pull || frames == NULL
	    || frame_count > SIZE_MAX / AUDIO_FRAME_SIZE)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&runtime->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (runtime->engine == NULL)
		memset(frames, 0, frame_count * AUDIO_FRAME_SIZE);
	else
		status = audio_engine_pull(runtime->engine, frames, frame_count);
	(void)st_mutex_unlock(&runtime->mutex);
	return status;
}

void
st_audio_runtime_stop(st_audio_runtime_t *runtime)
{
	if (runtime == NULL || st_mutex_lock(&runtime->mutex) != ST_THREAD_SUCCESS)
		return;
	runtime->stopped = true;
	audio_engine_stop(runtime->engine);
	(void)st_mutex_unlock(&runtime->mutex);
}

void
st_audio_runtime_destroy(st_audio_runtime_t *runtime)
{
	if (runtime == NULL)
		return;
	audio_engine_destroy(runtime->engine);
	st_mutex_destroy(&runtime->mutex);
	free(runtime);
}

void
st_audio_runtime_set_allowed(st_audio_runtime_t *runtime,
	uint32_t allowed_backends)
{
	if (runtime == NULL || st_mutex_lock(&runtime->mutex) != ST_THREAD_SUCCESS)
		return;
	runtime->allowed = allowed_backends;
	(void)st_mutex_unlock(&runtime->mutex);
}

uint32_t
st_audio_runtime_allowed(st_audio_runtime_t *runtime)
{
	uint32_t allowed = 0;

	if (runtime != NULL && st_mutex_lock(&runtime->mutex) == ST_THREAD_SUCCESS) {
		allowed = runtime->allowed;
		(void)st_mutex_unlock(&runtime->mutex);
	}
	return allowed;
}

void
st_audio_runtime_set_midi_allowed(st_audio_runtime_t *runtime, uint32_t allowed)
{
	if (runtime == NULL || st_mutex_lock(&runtime->mutex) != ST_THREAD_SUCCESS) return;
	runtime->midi_allowed = allowed;
	(void)st_mutex_unlock(&runtime->mutex);
}
uint32_t
st_audio_runtime_midi_allowed(st_audio_runtime_t *runtime)
{
	uint32_t value = 0;
	if (runtime != NULL && st_mutex_lock(&runtime->mutex) == ST_THREAD_SUCCESS) {
		value = runtime->midi_allowed;
		(void)st_mutex_unlock(&runtime->mutex);
	}
	return value;
}
bool st_audio_runtime_is_pull(st_audio_runtime_t *runtime)
{
	/* The frontend kind is immutable after creation. */
	return runtime != NULL && runtime->pull;
}
bool st_audio_runtime_stopped(st_audio_runtime_t *runtime)
{
	bool value = false;
	if (runtime != NULL && st_mutex_lock(&runtime->mutex) == ST_THREAD_SUCCESS) {
		value = runtime->stopped;
		(void)st_mutex_unlock(&runtime->mutex);
	}
	return value;
}

int
st_audio_runtime_acquire(st_audio_runtime_t *runtime, audio_engine_t **result)
{
	int status = 0;

	if (runtime == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&runtime->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (runtime->stopped)
		status = IO_ERROR_CANCELLED;
	else if (runtime->engine == NULL)
		status = runtime->pull ? audio_engine_create_pull(&runtime->engine)
		    : audio_engine_create(&runtime->engine, runtime->allowed);
	if (status == 0)
		*result = runtime->engine;
	(void)st_mutex_unlock(&runtime->mutex);
	return status;
}
