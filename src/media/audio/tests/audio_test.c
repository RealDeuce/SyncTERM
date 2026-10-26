/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio.h"
#include "audio_internal.h"
#include "st_audio_runtime.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdatomic.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct fake_state {
	atomic_size_t writes;
	bool fail_write;
};

struct latency_state {
	struct audio_backend_callbacks callbacks;
	st_mutex_t mutex;
	st_condition_t condition;
	bool entered;
	bool released;
};

struct drain_context {
	audio_stream_t *stream;
	int result;
};

static atomic_size_t total_writes = 0;
static struct latency_state *latency_state;

static bool
fake_available(void)
{
	return true;
}

static bool
fake_unavailable(void)
{
	return false;
}

static int
fake_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct fake_state *state = calloc(1, sizeof(*state));

	(void)callbacks;
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	atomic_init(&state->writes, 0);
	*result = state;
	return 0;
}

static int
fake_failing_open(void **result,
    const struct audio_backend_callbacks *callbacks)
{
	(void)result;
	(void)callbacks;
	return IO_ERROR_IO;
}

static int
fake_immediate_failure_open(void **result,
    const struct audio_backend_callbacks *callbacks)
{
	struct fake_state *state = calloc(1, sizeof(*state));

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	atomic_init(&state->writes, 0);
	*result = state;
	callbacks->failed(callbacks->context);
	return 0;
}

static void
fake_close(void *opaque)
{
	free(opaque);
}

static int
fake_write(void *opaque, const int16_t *frames, size_t frame_count)
{
	struct fake_state *state = opaque;

	(void)frames;
	(void)frame_count;
	(void)atomic_fetch_add_explicit(&state->writes, 1,
	    memory_order_relaxed);
	(void)atomic_fetch_add_explicit(&total_writes, 1,
	    memory_order_relaxed);
	return state->fail_write ? IO_ERROR_IO : 0;
}

static uint32_t
fake_latency(void *opaque)
{
	(void)opaque;
	return 0;
}

static int
latency_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct latency_state *state = calloc(1, sizeof(*state));

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&state->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(state);
		return IO_ERROR_IO;
	}
	if (st_condition_init(&state->condition) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&state->mutex);
		free(state);
		return IO_ERROR_IO;
	}
	state->callbacks = *callbacks;
	latency_state = state;
	*result = state;
	return 0;
}

static void
latency_close(void *opaque)
{
	struct latency_state *state = opaque;

	latency_state = NULL;
	st_condition_destroy(&state->condition);
	st_mutex_destroy(&state->mutex);
	free(state);
}

static uint32_t
gated_latency(void *opaque)
{
	struct latency_state *state = opaque;

	if (st_mutex_lock(&state->mutex) != ST_THREAD_SUCCESS)
		return 1;
	state->entered = true;
	(void)st_condition_broadcast(&state->condition);
	while (!state->released) {
		if (st_condition_wait(&state->condition, &state->mutex)
		    != ST_THREAD_SUCCESS)
			break;
	}
	(void)st_mutex_unlock(&state->mutex);
	return 1;
}

static int
drain_thread(void *opaque)
{
	struct drain_context *context = opaque;

	context->result = audio_stream_drain(context->stream);
	return 0;
}

static const struct audio_backend_descriptor unavailable_backend = {
	AUDIO_BACKEND_CORE_AUDIO, fake_unavailable, fake_open, fake_close,
	fake_write, fake_latency
};
static const struct audio_backend_descriptor failing_backend = {
	AUDIO_BACKEND_WASAPI, fake_available, fake_failing_open, fake_close,
	fake_write, fake_latency
};
static const struct audio_backend_descriptor working_backend = {
	AUDIO_BACKEND_ALSA, fake_available, fake_open, fake_close, fake_write,
	fake_latency
};
static const struct audio_backend_descriptor immediate_failure_backend = {
	AUDIO_BACKEND_PIPEWIRE, fake_available, fake_immediate_failure_open,
	fake_close, NULL, fake_latency
};
static const struct audio_backend_descriptor latency_backend = {
	AUDIO_BACKEND_PORTAUDIO, fake_available, latency_open, latency_close,
	NULL, gated_latency
};

static int
test_engine_and_stream(void)
{
	const struct audio_backend_descriptor *backends[] = {
		&unavailable_backend, &failing_backend, &working_backend
	};
	audio_engine_t *engine = NULL;
	audio_engine_t *sentinel = (audio_engine_t *)(uintptr_t)1;
	audio_stream_t *stream = NULL;
	int16_t frames[] = { 1000, 1000, -1000, -1000 };
	uint8_t samples[] = { 0, 128, 255 };
	struct audio_buffer_options loop_options = AUDIO_BUFFER_OPTIONS_INIT;
	float left;
	float right;

	CHECK(audio_engine_create_with_backends(&engine, AUDIO_BACKENDS_DEFAULT,
	    backends, 3) == 0);
	CHECK(audio_engine_backend(engine) == AUDIO_BACKEND_ALSA);
	CHECK(audio_engine_create_with_backends(&sentinel, AUDIO_BACKEND_ALSA,
	    backends, 3) == IO_ERROR_BUSY);
	CHECK(sentinel == (audio_engine_t *)(uintptr_t)1);
	CHECK(audio_stream_create(&stream, engine, -3.0f, -4.0f) == 0);
	CHECK(audio_stream_get_volume(stream, &left, &right) == 0);
	CHECK(left == -3.0f && right == -4.0f);
	CHECK(audio_stream_append(stream, frames, 2, NULL) == 0);
	CHECK(audio_stream_drain(stream) == 0);
	CHECK(audio_stream_is_idle(stream));
	CHECK(audio_stream_append_u8_mono_22050(stream, samples, 3, NULL) == 0);
	CHECK(audio_stream_drain(stream) == 0);
	CHECK(atomic_load_explicit(&total_writes, memory_order_relaxed) > 0);
	CHECK(audio_stream_set_volume(stream, 0.0f, 0.0f) == 0);
	CHECK(audio_stream_ramp_volume(stream, -6.0f, -6.0f, 4) == 0);
	CHECK(audio_stream_clear(stream) == 0);
	audio_stream_destroy(stream);
	CHECK(audio_engine_play(engine, frames, 2, NULL) == 0);
	CHECK(audio_engine_play_u8_mono_22050(engine, samples, 3, NULL) == 0);
	loop_options.loop = true;
	CHECK(audio_engine_play(engine, frames, 2, &loop_options)
	    == IO_ERROR_INVALID_ARGUMENT);
	audio_engine_destroy(engine);
	return 0;
}

static int
test_selection_errors(void)
{
	const struct audio_backend_descriptor *unavailable[] = {
		&unavailable_backend
	};
	const struct audio_backend_descriptor *failing[] = {
		&failing_backend
	};
	const struct audio_backend_descriptor *immediate_failure[] = {
		&immediate_failure_backend
	};
	audio_engine_t *engine = NULL;
	audio_stream_t *stream = NULL;

	CHECK(audio_engine_create_with_backends(&engine,
	    AUDIO_BACKEND_CORE_AUDIO, unavailable, 1)
	    == IO_ERROR_NOT_SUPPORTED);
	CHECK(audio_engine_create_with_backends(&engine,
	    AUDIO_BACKEND_WASAPI, failing, 1) == IO_ERROR_IO);
	CHECK(audio_engine_create_with_backends(NULL,
	    AUDIO_BACKEND_WASAPI, failing, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_engine_create_with_backends(&engine,
	    AUDIO_BACKEND_PIPEWIRE, immediate_failure, 1) == 0);
	CHECK(audio_stream_create(&stream, engine, 0.0f, 0.0f) == IO_ERROR_IO);
	audio_engine_destroy(engine);
	return 0;
}

static int
test_clear_after_submission(void)
{
	const struct audio_backend_descriptor *backends[] = {
		&latency_backend
	};
	audio_engine_t *engine = NULL;
	audio_stream_t *stream = NULL;
	struct latency_state *state;
	struct drain_context context;
	st_thread_t thread;
	int16_t frames[] = { 1000, 1000, -1000, -1000 };
	int16_t output[4];
	int clear_status;
	int thread_status;

	CHECK(audio_engine_create_with_backends(&engine,
	    AUDIO_BACKEND_PORTAUDIO, backends, 1) == 0);
	state = latency_state;
	CHECK(state != NULL);
	CHECK(audio_stream_create(&stream, engine, 0.0f, 0.0f) == 0);
	CHECK(audio_stream_append(stream, frames, 2, NULL) == 0);
	CHECK(state->callbacks.pull(state->callbacks.context, output, 2) == 2);
	context.stream = stream;
	context.result = IO_ERROR_IO;
	CHECK(st_thread_create(&thread, drain_thread, &context)
	    == ST_THREAD_SUCCESS);
	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	while (!state->entered) {
		CHECK(st_condition_wait(&state->condition, &state->mutex)
		    == ST_THREAD_SUCCESS);
	}
	CHECK(st_mutex_unlock(&state->mutex) == ST_THREAD_SUCCESS);

	clear_status = audio_stream_clear(stream);
	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	state->released = true;
	(void)st_condition_broadcast(&state->condition);
	CHECK(st_mutex_unlock(&state->mutex) == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(thread, &thread_status) == ST_THREAD_SUCCESS);
	CHECK(thread_status == 0);
	CHECK(clear_status == 0);
	CHECK(context.result == 0);

	audio_stream_destroy(stream);
	audio_engine_destroy(engine);
	return 0;
}

static int
test_one_shot_reclamation(void)
{
	const struct audio_backend_descriptor *backends[] = {
		&latency_backend
	};
	audio_engine_t *engine = NULL;
	struct latency_state *state;
	int16_t frames[] = { 1000, 1000, -1000, -1000 };
	int16_t output[4];
	size_t i;

	CHECK(audio_engine_create_with_backends(&engine,
	    AUDIO_BACKEND_PORTAUDIO, backends, 1) == 0);
	state = latency_state;
	CHECK(state != NULL);
	for (i = 0; i < 32; i++) {
		CHECK(audio_engine_play(engine, frames, 2, NULL) == 0);
		CHECK(audio_engine_stream_count_for_test(engine) == 1);
		CHECK(state->callbacks.pull(state->callbacks.context, output, 2)
		    == 2);
		CHECK(audio_engine_stream_count_for_test(engine) == 1);
	}
	audio_engine_destroy(engine);
	return 0;
}

static int
test_synthesis(void)
{
	int16_t frames[400 * AUDIO_CHANNELS];
	int shape;
	size_t i;

	for (shape = AUDIO_WAVE_SINE; shape <= AUDIO_WAVE_SINE_SAW_HARM;
	    shape++) {
		CHECK(audio_generate_tone(frames, 400, 440.0,
		    (enum audio_wave_shape)shape, false) == 0);
		for (i = 0; i < 400; i++)
			CHECK(frames[i * 2] == frames[i * 2 + 1]);
	}
	CHECK(audio_generate_tone(frames, 400, 0.0, AUDIO_WAVE_SINE,
	    true) == 0);
	for (i = 0; i < 800; i++)
		CHECK(frames[i] == 0);
	CHECK(audio_generate_tone(NULL, 400, 440.0, AUDIO_WAVE_SINE,
	    true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_generate_tone(frames, 400, -1.0, AUDIO_WAVE_SINE,
	    true) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_generate_tone(frames, 400, 30000.0, AUDIO_WAVE_SQUARE,
	    true) == 0);
	CHECK(audio_generate_tone(frames, 400, 1e100, AUDIO_WAVE_SINE_SAW_HARM,
	    true) == 0);
	for (i = 0; i < 800; ++i) CHECK(frames[i] == 0);
	CHECK(audio_generate_tone(frames, 400, DBL_MAX, AUDIO_WAVE_SINE,
	    true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(audio_generate_tone(frames, 400, 440.0,
	    (enum audio_wave_shape)100, true) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

static int
test_pull_runtime(void)
{
	st_audio_runtime_t *runtime = NULL;
	audio_engine_t *engine = NULL, *other = NULL;
	audio_stream_t *stream = NULL;
	int16_t frames[8];
	const int16_t samples[] = {100, -100, 200, -200};
	/* Preserve the mixer's Q15 gain and soft clipping. */
	const int16_t expected[] = {98, -99, 198, -199};
	const struct audio_buffer_options options = AUDIO_BUFFER_OPTIONS_INIT;
	struct drain_context drain;
	st_thread_t thread;

	CHECK(st_audio_runtime_create_pull(&runtime) == 0);
	memset(frames, 1, sizeof(frames));
	CHECK(st_audio_runtime_pull(runtime, frames, 4) == 0);
	for (size_t i = 0; i < 8; ++i) CHECK(frames[i] == 0);
	st_audio_runtime_set_allowed(runtime, AUDIO_BACKEND_OSS);
	CHECK(st_audio_runtime_acquire(runtime, &engine) == 0);
	CHECK(audio_engine_backend(engine) == AUDIO_BACKEND_NONE);
	CHECK(audio_engine_create_pull(&other) == IO_ERROR_BUSY && other == NULL);
	CHECK(audio_stream_create(&stream, engine, 0, 0) == 0);
	CHECK(audio_stream_append(stream, samples, 2, &options) == 0);
	CHECK(st_audio_runtime_pull(runtime, frames, 4) == 0);
	CHECK(memcmp(frames, expected, sizeof(expected)) == 0);
	for (size_t i = 4; i < 8; ++i) CHECK(frames[i] == 0);
	CHECK(audio_stream_drain(stream) == 0);
	CHECK(audio_stream_append(stream, samples, 2, &options) == 0);
	drain = (struct drain_context){.stream = stream};
	CHECK(st_thread_create(&thread, drain_thread, &drain) == ST_THREAD_SUCCESS);
	st_audio_runtime_stop(runtime);
	CHECK(st_thread_join(thread, NULL) == ST_THREAD_SUCCESS);
	CHECK(drain.result == IO_ERROR_CANCELLED);
	CHECK(st_audio_runtime_acquire(runtime, &other) == IO_ERROR_CANCELLED);
	audio_stream_destroy(stream);
	st_audio_runtime_destroy(runtime);
	CHECK(st_audio_runtime_create_pull(&runtime) == 0);
	st_audio_runtime_stop(runtime);
	CHECK(st_audio_runtime_acquire(runtime, &other) == IO_ERROR_CANCELLED);
	st_audio_runtime_destroy(runtime);
	return 0;
}

int
main(void)
{
	st_audio_runtime_t *runtime = NULL;
	uint32_t compiled = audio_compiled_backends();
	uint32_t available = audio_available_backends();
	uint32_t known = AUDIO_BACKEND_WASAPI | AUDIO_BACKEND_ALSA
	    | AUDIO_BACKEND_OSS | AUDIO_BACKEND_SDL | AUDIO_BACKEND_PORTAUDIO
	    | AUDIO_BACKEND_PULSE_AUDIO | AUDIO_BACKEND_CORE_AUDIO
	    | AUDIO_BACKEND_PIPEWIRE;

	CHECK((compiled & ~known) == 0);
	CHECK((available & ~compiled) == 0);
	CHECK(st_audio_runtime_create(&runtime, AUDIO_BACKEND_ALSA) == 0);
	CHECK(st_audio_runtime_allowed(runtime) == AUDIO_BACKEND_ALSA);
	st_audio_runtime_set_allowed(runtime, AUDIO_BACKEND_CORE_AUDIO);
	CHECK(st_audio_runtime_allowed(runtime) == AUDIO_BACKEND_CORE_AUDIO);
	st_audio_runtime_destroy(runtime);
	CHECK(test_engine_and_stream() == 0);
	CHECK(test_selection_errors() == 0);
	CHECK(test_clear_after_submission() == 0);
	CHECK(test_one_shot_reclamation() == 0);
	CHECK(test_synthesis() == 0);
	CHECK(test_pull_runtime() == 0);
	return 0;
}
