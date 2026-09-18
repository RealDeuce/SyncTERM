/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_mixer.h"

#include "io_error.h"
#include "st_threads.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct clear_context {
	audio_mixer_stream_t *stream;
	int result;
};

struct acknowledge_context {
	audio_mixer_t *mixer;
	audio_mixer_stream_t *stream;
	uint64_t ticket;
	int result;
};

static int
clear_thread(void *argument)
{
	struct clear_context *context = argument;
	struct timespec delay = { 0, 20000000L };

	(void)st_thread_sleep(&delay, NULL);
	context->result = audio_mixer_stream_clear(context->stream);
	return 0;
}

static int
acknowledge_thread(void *argument)
{
	struct acknowledge_context *context = argument;
	const int16_t later[] = { 9, 9, 10, 10 };
	struct timespec delay = { 0, 20000000L };

	(void)st_thread_sleep(&delay, NULL);
	context->result = audio_mixer_stream_append(context->stream, later, 2,
	    NULL);
	audio_mixer_acknowledge(context->mixer, context->ticket);
	return 0;
}

static int
test_basic_mix(void)
{
	audio_mixer_t *mixer = NULL;
	audio_mixer_stream_t *first = NULL;
	audio_mixer_stream_t *second = NULL;
	const int16_t one[] = { 1000, -1000, 2000, -2000 };
	const int16_t two[] = { 500, 500, 500, 500 };
	int16_t output[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

	CHECK(audio_mixer_create(&mixer) == 0);
	CHECK(audio_mixer_stream_create(&first, mixer, 0.0f, 0.0f) == 0);
	CHECK(audio_mixer_stream_create(&second, mixer, 0.0f, 0.0f) == 0);
	CHECK(audio_mixer_stream_append(first, one, 2, NULL) == 0);
	CHECK(audio_mixer_stream_append(second, two, 2, NULL) == 0);
	CHECK(audio_mixer_pull(mixer, output, 4) == 2);
	CHECK(output[0] > 1400 && output[0] < 1600);
	CHECK(output[1] < -400 && output[1] > -600);
	CHECK(output[2] > 2400 && output[2] < 2600);
	CHECK(output[3] < -1400 && output[3] > -1600);
	CHECK(output[4] == 0 && output[5] == 0);
	CHECK(audio_mixer_stream_is_idle(first));
	CHECK(audio_mixer_stream_is_idle(second));
	audio_mixer_destroy(mixer);
	return 0;
}

static int
test_take_loop_and_volume(void)
{
	audio_mixer_t *mixer = NULL;
	audio_mixer_stream_t *stream = NULL;
	struct audio_mixer_buffer_options options = { 0 };
	int16_t *loop;
	const int16_t ending[] = { 2000, 2000 };
	int16_t output[12];
	float left = 0.0f;
	float right = 0.0f;

	CHECK(audio_mixer_create(&mixer) == 0);
	CHECK(audio_mixer_stream_create(&stream, mixer, -6.0f, -3.0f) == 0);
	loop = malloc(2 * sizeof(*loop));
	CHECK(loop != NULL);
	loop[0] = 1000;
	loop[1] = 1000;
	options.loop = true;
	CHECK(audio_mixer_stream_append_take(stream, loop, 1, &options) == 0);
	CHECK(audio_mixer_stream_drain(stream) == IO_ERROR_BUSY);
	CHECK(audio_mixer_pull(mixer, output, 3) == 3);
	CHECK(!audio_mixer_stream_is_idle(stream));
	CHECK(audio_mixer_stream_append(stream, ending, 1, NULL) == 0);
	CHECK(audio_mixer_pull(mixer, output, 3) == 1);
	CHECK(audio_mixer_stream_is_idle(stream));
	CHECK(audio_mixer_stream_set_volume(stream, -4.0f, -5.0f) == 0);
	CHECK(audio_mixer_stream_get_volume(stream, &left, &right) == 0);
	CHECK(fabsf(left + 4.0f) < 0.001f);
	CHECK(fabsf(right + 5.0f) < 0.001f);
	CHECK(audio_mixer_stream_ramp_volume(stream, 0.0f, 0.0f, 10) == 0);
	audio_mixer_destroy(mixer);
	return 0;
}

static int
test_drain_snapshot_and_clear(void)
{
	audio_mixer_t *mixer = NULL;
	audio_mixer_stream_t *stream = NULL;
	int16_t frames[8] = { 1, 1, 2, 2, 3, 3, 4, 4 };
	int16_t output[8];
	struct clear_context context;
	st_thread_t thread;
	int thread_result;

	CHECK(audio_mixer_create(&mixer) == 0);
	CHECK(audio_mixer_stream_create(&stream, mixer, 0.0f, 0.0f) == 0);
	CHECK(audio_mixer_stream_append(stream, frames, 4, NULL) == 0);
	context.stream = stream;
	context.result = 123;
	CHECK(st_thread_create(&thread, clear_thread, &context)
	    == ST_THREAD_SUCCESS);
	CHECK(audio_mixer_stream_drain(stream) == IO_ERROR_CANCELLED);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0);
	CHECK(context.result == 0);

	CHECK(audio_mixer_stream_append(stream, frames, 2, NULL) == 0);
	CHECK(audio_mixer_pull(mixer, output, 2) == 2);
	CHECK(audio_mixer_stream_drain(stream) == 0);
	audio_mixer_destroy(mixer);
	return 0;
}

static int
test_delivery_acknowledgement(void)
{
	audio_mixer_t *mixer = NULL;
	audio_mixer_stream_t *stream = NULL;
	const int16_t initial[] = { 1, 1, 2, 2 };
	int16_t output[4];
	uint64_t ticket = 0;
	struct acknowledge_context context;
	st_thread_t thread;
	int thread_result;

	CHECK(audio_mixer_create(&mixer) == 0);
	CHECK(audio_mixer_stream_create(&stream, mixer, 0.0f, 0.0f) == 0);
	CHECK(audio_mixer_stream_append(stream, initial, 2, NULL) == 0);
	CHECK(audio_mixer_pull_pending(mixer, output, 2, &ticket) == 2);
	CHECK(ticket != 0);
	CHECK(audio_mixer_stream_is_idle(stream));
	context.mixer = mixer;
	context.stream = stream;
	context.ticket = ticket;
	context.result = 123;
	CHECK(st_thread_create(&thread, acknowledge_thread, &context)
	    == ST_THREAD_SUCCESS);
	CHECK(audio_mixer_stream_drain(stream) == 0);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0 && context.result == 0);
	CHECK(!audio_mixer_stream_is_idle(stream));
	CHECK(audio_mixer_pull(mixer, output, 2) == 2);
	CHECK(audio_mixer_stream_drain(stream) == 0);
	audio_mixer_destroy(mixer);
	return 0;
}

static int
test_validation(void)
{
	audio_mixer_t *mixer = NULL;
	audio_mixer_stream_t *stream = NULL;
	int16_t frame[2] = { 0, 0 };
	struct audio_mixer_buffer_options options = { 0 };

	CHECK(audio_mixer_create(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_create(&mixer) == 0);
	CHECK(audio_mixer_stream_create(&stream, mixer, NAN, 0.0f)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_stream_create(&stream, mixer, 0.0f, 0.0f) == 0);
	CHECK(audio_mixer_stream_append(stream, NULL, 1, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_stream_append(stream, frame, 0, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	options.volume_left_db = INFINITY;
	CHECK(audio_mixer_stream_append(stream, frame, 1, &options)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(audio_mixer_stream_get_volume(stream, NULL, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	audio_mixer_destroy(mixer);
	return 0;
}

int
main(void)
{
	CHECK(test_basic_mix() == 0);
	CHECK(test_take_loop_and_volume() == 0);
	CHECK(test_drain_snapshot_and_clear() == 0);
	CHECK(test_delivery_acknowledgement() == 0);
	CHECK(test_validation() == 0);
	return 0;
}
