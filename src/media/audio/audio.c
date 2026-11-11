/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio.h"
#include "audio_internal.h"

#include "audio_mixer.h"
#include "io_error.h"
#include "st_threads.h"

#include <stdatomic.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUDIO_DEVICE_CHUNK_FRAMES 512

struct audio_stream {
	struct audio_engine *engine;
	audio_mixer_stream_t *mixer_stream;
	struct audio_stream *next;
	bool one_shot;
};

struct audio_engine {
	audio_mixer_t *mixer;
	const struct audio_backend_descriptor *backend;
	void *backend_state;
	struct audio_stream *streams;
	st_mutex_t mutex;
	st_thread_t worker;
	bool worker_started;
	atomic_bool failed;
};

static atomic_bool engine_exists = false;

extern const struct audio_backend_descriptor *const audio_backends[];
extern const size_t audio_backend_count;

static struct audio_mixer_buffer_options
mixer_options(const struct audio_buffer_options *options)
{
	struct audio_mixer_buffer_options converted = { 0 };

	if (options != NULL) {
		converted.volume_left_db = options->volume_left_db;
		converted.volume_right_db = options->volume_right_db;
		converted.fade_in_frames = options->fade_in_frames;
		converted.fade_out_frames = options->fade_out_frames;
		converted.crossfade = options->crossfade;
		converted.loop = options->loop;
	}
	return converted;
}

static void
remove_stream_locked(audio_engine_t *engine, audio_stream_t *stream)
{
	audio_stream_t **link;

	for (link = &engine->streams; *link != NULL; link = &(*link)->next) {
		if (*link == stream) {
			*link = stream->next;
			return;
		}
	}
}

static void
reap_one_shots(audio_engine_t *engine)
{
	audio_stream_t **link;

	if (st_mutex_lock(&engine->mutex) != ST_THREAD_SUCCESS)
		return;
	link = &engine->streams;
	while (*link != NULL) {
		audio_stream_t *stream = *link;

		if (!stream->one_shot
		    || !audio_mixer_stream_is_idle(stream->mixer_stream)) {
			link = &stream->next;
			continue;
		}
		*link = stream->next;
		audio_mixer_stream_destroy(stream->mixer_stream);
		free(stream);
	}
	(void)st_mutex_unlock(&engine->mutex);
}

static size_t
backend_pull(void *context, int16_t *frames, size_t frame_count)
{
	audio_engine_t *engine = context;
	uint64_t ticket = 0;
	size_t result;

	result = audio_mixer_pull_pending(engine->mixer, frames, frame_count,
	    &ticket);
	audio_mixer_acknowledge(engine->mixer, ticket);
	return result;
}

static void
backend_failed(void *context)
{
	audio_engine_t *engine = context;

	atomic_store_explicit(&engine->failed, true, memory_order_release);
	audio_mixer_stop(engine->mixer, IO_ERROR_IO);
}

static int
device_worker(void *argument)
{
	audio_engine_t *engine = argument;
	int16_t frames[AUDIO_DEVICE_CHUNK_FRAMES * AUDIO_CHANNELS];

	for (;;) {
		size_t populated;
		int status = audio_mixer_wait_for_data(engine->mixer);

		if (status != 0)
			break;
		do {
			uint64_t ticket = 0;

			populated = audio_mixer_pull_pending(engine->mixer, frames,
			    AUDIO_DEVICE_CHUNK_FRAMES, &ticket);
			if (populated > 0 && engine->backend->write(
			    engine->backend_state, frames,
			    AUDIO_DEVICE_CHUNK_FRAMES) != 0) {
				backend_failed(engine);
				return 0;
			}
			audio_mixer_acknowledge(engine->mixer, ticket);
		} while (populated > 0);
	}
	return 0;
}

uint32_t
audio_compiled_backends(void)
{
	uint32_t result = 0;
	size_t i;

	for (i = 0; i < audio_backend_count; i++)
		result |= audio_backends[i]->backend;
	return result;
}

uint32_t
audio_available_backends(void)
{
	uint32_t result = 0;
	size_t i;

	for (i = 0; i < audio_backend_count; i++) {
		if (audio_backends[i]->available == NULL
		    || audio_backends[i]->available())
			result |= audio_backends[i]->backend;
	}
	return result;
}

int
audio_engine_create_pull(audio_engine_t **result)
{
	audio_engine_t *engine;
	bool expected = false;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!atomic_compare_exchange_strong_explicit(&engine_exists, &expected,
	    true, memory_order_acq_rel, memory_order_acquire))
		return IO_ERROR_BUSY;
	engine = calloc(1, sizeof(*engine));
	if (engine == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto fail_existence;
	}
	atomic_init(&engine->failed, false);
	status = st_mutex_init(&engine->mutex, ST_MUTEX_PLAIN);
	if (status != ST_THREAD_SUCCESS) {
		status = IO_ERROR_IO;
		goto fail_engine;
	}
	status = audio_mixer_create(&engine->mixer);
	if (status != 0)
		goto fail_mutex;
	*result = engine;
	return 0;

fail_mutex:
	st_mutex_destroy(&engine->mutex);
fail_engine:
	free(engine);
fail_existence:
	atomic_store_explicit(&engine_exists, false, memory_order_release);
	return status;
}

int
audio_engine_pull(audio_engine_t *engine, int16_t *frames, size_t frame_count)
{
	if (engine == NULL || engine->backend != NULL || frames == NULL
	    || frame_count > SIZE_MAX / AUDIO_FRAME_SIZE)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)backend_pull(engine, frames, frame_count);
	return 0;
}

void
audio_engine_stop(audio_engine_t *engine)
{
	if (engine != NULL)
		audio_mixer_stop(engine->mixer, IO_ERROR_CANCELLED);
}

int
audio_engine_create_with_backends(audio_engine_t **result,
    uint32_t allowed_backends,
    const struct audio_backend_descriptor *const backends[],
    size_t backend_count)
{
	audio_engine_t *engine;
	struct audio_backend_callbacks callbacks;
	bool attempted = false;
	int status;

	if (result == NULL || allowed_backends == 0 || backends == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = audio_engine_create_pull(&engine);
	if (status != 0)
		return status;
	callbacks.context = engine;
	callbacks.pull = backend_pull;
	callbacks.failed = backend_failed;
	status = IO_ERROR_NOT_SUPPORTED;
	for (size_t i = 0; i < backend_count; i++) {
		const struct audio_backend_descriptor *backend = backends[i];

		if ((allowed_backends & backend->backend) == 0)
			continue;
		if (backend->available != NULL && !backend->available())
			continue;
		attempted = true;
		if (backend->open(&engine->backend_state, &callbacks) != 0)
			continue;
		engine->backend = backend;
		status = 0;
		break;
	}
	if (status != 0) {
		status = attempted ? IO_ERROR_IO : IO_ERROR_NOT_SUPPORTED;
		goto failed;
	}
	if (engine->backend->write != NULL) {
		if (st_thread_create(&engine->worker, device_worker, engine)
		    != ST_THREAD_SUCCESS) {
			status = IO_ERROR_IO;
			goto failed;
		}
		engine->worker_started = true;
	}
	*result = engine;
	return 0;

failed:
	audio_engine_destroy(engine);
	return status;
}

int
audio_engine_create(audio_engine_t **result, uint32_t allowed_backends)
{
	return audio_engine_create_with_backends(result, allowed_backends,
	    audio_backends, audio_backend_count);
}

void
audio_engine_destroy(audio_engine_t *engine)
{
	audio_stream_t *stream;

	if (engine == NULL)
		return;
	audio_mixer_stop(engine->mixer, IO_ERROR_CANCELLED);
	if (engine->worker_started)
		(void)st_thread_join(engine->worker, NULL);
	if (engine->backend != NULL)
		engine->backend->close(engine->backend_state);
	while ((stream = engine->streams) != NULL) {
		engine->streams = stream->next;
		audio_mixer_stream_destroy(stream->mixer_stream);
		free(stream);
	}
	audio_mixer_destroy(engine->mixer);
	st_mutex_destroy(&engine->mutex);
	free(engine);
	atomic_store_explicit(&engine_exists, false, memory_order_release);
}

enum audio_backend
audio_engine_backend(const audio_engine_t *engine)
{
	return engine == NULL || engine->backend == NULL ? AUDIO_BACKEND_NONE
	    : engine->backend->backend;
}

#if defined(ST_AUDIO_TESTING)
size_t
audio_engine_stream_count_for_test(audio_engine_t *engine)
{
	audio_stream_t *stream;
	size_t count = 0;

	if (engine == NULL
	    || st_mutex_lock(&engine->mutex) != ST_THREAD_SUCCESS)
		return SIZE_MAX;
	for (stream = engine->streams; stream != NULL; stream = stream->next)
		count++;
	(void)st_mutex_unlock(&engine->mutex);
	return count;
}
#endif

static int
create_stream(audio_stream_t **result, audio_engine_t *engine,
    float volume_left_db, float volume_right_db, bool one_shot)
{
	audio_stream_t *stream;
	int status;

	if (result == NULL || engine == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (atomic_load_explicit(&engine->failed, memory_order_acquire))
		return IO_ERROR_IO;
	/* Stream destruction stays on an ordinary caller, not a device thread. */
	reap_one_shots(engine);
	stream = calloc(1, sizeof(*stream));
	if (stream == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = audio_mixer_stream_create(&stream->mixer_stream,
	    engine->mixer, volume_left_db, volume_right_db);
	if (status != 0) {
		free(stream);
		return status;
	}
	stream->engine = engine;
	stream->one_shot = one_shot;
	if (st_mutex_lock(&engine->mutex) != ST_THREAD_SUCCESS) {
		audio_mixer_stream_destroy(stream->mixer_stream);
		free(stream);
		return IO_ERROR_IO;
	}
	stream->next = engine->streams;
	engine->streams = stream;
	(void)st_mutex_unlock(&engine->mutex);
	*result = stream;
	return 0;
}

static void
mark_one_shot(audio_stream_t *stream)
{
	audio_engine_t *engine = stream->engine;

	(void)st_mutex_lock(&engine->mutex);
	stream->one_shot = true;
	(void)st_mutex_unlock(&engine->mutex);
}

int
audio_stream_create(audio_stream_t **result, audio_engine_t *engine,
    float volume_left_db, float volume_right_db)
{
	return create_stream(result, engine, volume_left_db, volume_right_db,
	    false);
}

void
audio_stream_destroy(audio_stream_t *stream)
{
	audio_engine_t *engine;

	if (stream == NULL)
		return;
	engine = stream->engine;
	(void)st_mutex_lock(&engine->mutex);
	remove_stream_locked(engine, stream);
	audio_mixer_stream_destroy(stream->mixer_stream);
	(void)st_mutex_unlock(&engine->mutex);
	free(stream);
}

int
audio_stream_append(audio_stream_t *stream, const int16_t *frames,
    size_t frame_count, const struct audio_buffer_options *options)
{
	struct audio_mixer_buffer_options converted = mixer_options(options);

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return audio_mixer_stream_append(stream->mixer_stream, frames,
	    frame_count, options == NULL ? NULL : &converted);
}

int
audio_stream_set_source(audio_stream_t *stream,
    size_t (*source)(void *, int16_t *, size_t), void *context)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : audio_mixer_stream_set_source(stream->mixer_stream,source,context);
}

int
audio_stream_source_active(audio_stream_t *stream, bool active)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : audio_mixer_stream_source_active(stream->mixer_stream,active);
}

int
audio_stream_append_take(audio_stream_t *stream, int16_t *frames,
    size_t frame_count, const struct audio_buffer_options *options)
{
	struct audio_mixer_buffer_options converted = mixer_options(options);

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return audio_mixer_stream_append_take(stream->mixer_stream, frames,
	    frame_count, options == NULL ? NULL : &converted);
}

static int
convert_u8_mono_22050(int16_t **result, const uint8_t *samples,
    size_t sample_count)
{
	int16_t *converted;
	size_t i;

	if (result == NULL || samples == NULL || sample_count == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (sample_count > SIZE_MAX / (AUDIO_FRAME_SIZE * 2))
		return IO_ERROR_OUT_OF_RANGE;
	converted = malloc(sample_count * AUDIO_FRAME_SIZE * 2);
	if (converted == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (i = 0; i < sample_count; i++) {
		int16_t current = (int16_t)(((int)samples[i] - 128) * 256);
		int16_t next = i + 1 < sample_count
		    ? (int16_t)(((int)samples[i + 1] - 128) * 256) : current;
		int16_t middle = (int16_t)(((int32_t)current + next) / 2);

		converted[i * 4] = current;
		converted[i * 4 + 1] = current;
		converted[i * 4 + 2] = middle;
		converted[i * 4 + 3] = middle;
	}
	*result = converted;
	return 0;
}

int
audio_stream_append_u8_mono_22050(audio_stream_t *stream,
    const uint8_t *samples, size_t sample_count,
    const struct audio_buffer_options *options)
{
	int16_t *converted;
	int status;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = convert_u8_mono_22050(&converted, samples, sample_count);
	if (status != 0)
		return status;
	status = audio_stream_append_take(stream, converted, sample_count * 2,
	    options);
	if (status != 0)
		free(converted);
	return status;
}

int
audio_stream_clear(audio_stream_t *stream)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : audio_mixer_stream_clear(stream->mixer_stream);
}

static void
sleep_milliseconds(uint32_t milliseconds)
{
	struct timespec duration;

	duration.tv_sec = (time_t)(milliseconds / 1000);
	duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	while (st_thread_sleep(&duration, &duration) != 0)
		;
}

int
audio_stream_drain(audio_stream_t *stream)
{
	int status;
	uint32_t latency;

	if (stream == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = audio_mixer_stream_drain(stream->mixer_stream);
	if (status != 0)
		return status;
	latency = audio_stream_latency_ms(stream);
	if (latency > 0)
		sleep_milliseconds(latency);
	return atomic_load_explicit(&stream->engine->failed,
	    memory_order_acquire) ? IO_ERROR_IO : 0;
}

bool
audio_stream_is_idle(audio_stream_t *stream)
{
	return stream == NULL
	    || audio_mixer_stream_is_idle(stream->mixer_stream);
}

int
audio_stream_set_volume(audio_stream_t *stream, float volume_left_db,
    float volume_right_db)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : audio_mixer_stream_set_volume(stream->mixer_stream,
	    volume_left_db, volume_right_db);
}

size_t audio_stream_buffered_frames(audio_stream_t *stream)
{
	return stream == NULL ? 0 : audio_mixer_stream_buffered_frames(stream->mixer_stream);
}

uint32_t audio_stream_latency_ms(audio_stream_t *stream)
{
	return stream == NULL || stream->engine->backend == NULL
	    || stream->engine->backend->latency_ms == NULL ? 0
	    : stream->engine->backend->latency_ms(stream->engine->backend_state);
}

int
audio_stream_ramp_volume(audio_stream_t *stream, float volume_left_db,
    float volume_right_db, size_t frame_count)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : audio_mixer_stream_ramp_volume(stream->mixer_stream,
	    volume_left_db, volume_right_db, frame_count);
}

int
audio_stream_get_volume(audio_stream_t *stream, float *volume_left_db,
    float *volume_right_db)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : audio_mixer_stream_get_volume(stream->mixer_stream,
	    volume_left_db, volume_right_db);
}

int
audio_engine_play(audio_engine_t *engine, const int16_t *frames,
    size_t frame_count, const struct audio_buffer_options *options)
{
	audio_stream_t *stream;
	int status;

	if (engine == NULL || (options != NULL && options->loop))
		return IO_ERROR_INVALID_ARGUMENT;
	status = create_stream(&stream, engine, 0.0f, 0.0f, false);
	if (status != 0)
		return status;
	status = audio_stream_append(stream, frames, frame_count, options);
	if (status != 0)
		audio_stream_destroy(stream);
	else
		mark_one_shot(stream);
	return status;
}

int
audio_engine_play_u8_mono_22050(audio_engine_t *engine,
    const uint8_t *samples, size_t sample_count,
    const struct audio_buffer_options *options)
{
	audio_stream_t *stream;
	int status;

	if (engine == NULL || (options != NULL && options->loop))
		return IO_ERROR_INVALID_ARGUMENT;
	status = create_stream(&stream, engine, 0.0f, 0.0f, false);
	if (status != 0)
		return status;
	status = audio_stream_append_u8_mono_22050(stream, samples,
	    sample_count, options);
	if (status != 0)
		audio_stream_destroy(stream);
	else
		mark_one_shot(stream);
	return status;
}

int
audio_beep(audio_engine_t *engine, double frequency, uint32_t duration_ms)
{
	audio_stream_t *stream = NULL;
	int16_t *frames = NULL;
	uint64_t calculated_frames;
	size_t frame_count;
	int status = IO_ERROR_NOT_SUPPORTED;

	if (!isfinite(frequency) || frequency < 0.0
	    || frequency > AUDIO_SAMPLE_RATE / 2.0 || duration_ms == 0)
		return duration_ms == 0 ? 0 : IO_ERROR_INVALID_ARGUMENT;
	if (frequency > 0.0 && frequency < 17.0)
		frequency = 17.0;
	calculated_frames = (uint64_t)AUDIO_SAMPLE_RATE * duration_ms / 1000;
	if (frequency > 0.0) {
		uint64_t minimum = (uint64_t)(AUDIO_SAMPLE_RATE / frequency) * 2;

		if (calculated_frames < minimum)
			calculated_frames = minimum;
	}
	if (calculated_frames == 0)
		calculated_frames = 1;
	if (calculated_frames > SIZE_MAX / AUDIO_FRAME_SIZE)
		return IO_ERROR_OUT_OF_RANGE;
	frame_count = (size_t)calculated_frames;
	if (engine != NULL) {
		frames = malloc(frame_count * AUDIO_FRAME_SIZE);
		if (frames == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
		else
			status = audio_generate_tone(frames, frame_count, frequency,
			    AUDIO_WAVE_SINE_SAW_HARM, true);
		if (status == 0)
			status = audio_stream_create(&stream, engine, -12.0f, -12.0f);
		if (status == 0) {
			status = audio_stream_append_take(stream, frames,
			    frame_count, NULL);
			if (status == 0)
				frames = NULL;
		}
		if (status == 0)
			status = audio_stream_drain(stream);
		audio_stream_destroy(stream);
		free(frames);
		if (status == 0)
			return 0;
	}
	if (audio_platform_beep(frequency, duration_ms) == 0)
		return 0;
	return status;
}
