/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_audio_runtime.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdlib.h>

struct st_audio_runtime {
	audio_engine_t *engine;
	uint32_t allowed;
	st_mutex_t mutex;
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

int
st_audio_runtime_acquire(st_audio_runtime_t *runtime, audio_engine_t **result)
{
	int status = 0;

	if (runtime == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_mutex_lock(&runtime->mutex) != ST_THREAD_SUCCESS)
		return IO_ERROR_IO;
	if (runtime->engine == NULL)
		status = audio_engine_create(&runtime->engine, runtime->allowed);
	if (status == 0)
		*result = runtime->engine;
	(void)st_mutex_unlock(&runtime->mutex);
	return status;
}
