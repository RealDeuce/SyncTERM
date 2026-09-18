/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio.h"
#include "audio_internal.h"

#include "io_error.h"
#include "st_threads.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static atomic_size_t total_writes = 0;

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
	CHECK(audio_generate_tone(frames, 400, 440.0,
	    (enum audio_wave_shape)100, true) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

int
main(void)
{
	uint32_t compiled = audio_compiled_backends();
	uint32_t available = audio_available_backends();
	uint32_t known = AUDIO_BACKEND_WASAPI | AUDIO_BACKEND_ALSA
	    | AUDIO_BACKEND_OSS | AUDIO_BACKEND_SDL | AUDIO_BACKEND_PORTAUDIO
	    | AUDIO_BACKEND_PULSE_AUDIO | AUDIO_BACKEND_CORE_AUDIO
	    | AUDIO_BACKEND_PIPEWIRE;

	CHECK((compiled & ~known) == 0);
	CHECK((available & ~compiled) == 0);
	CHECK(test_engine_and_stream() == 0);
	CHECK(test_selection_errors() == 0);
	CHECK(test_synthesis() == 0);
	return 0;
}
