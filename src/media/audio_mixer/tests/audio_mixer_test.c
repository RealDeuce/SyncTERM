/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_mixer.h"

#include "io_error.h"
#include "st_threads.h"

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct drain_context {
	audio_mixer_stream_t *stream;
	atomic_bool started;
	atomic_bool completed;
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
drain_thread(void *argument)
{
	struct drain_context *context = argument;

	atomic_store_explicit(&context->started, true, memory_order_release);
	context->result = audio_mixer_stream_drain(context->stream);
	atomic_store_explicit(&context->completed, true, memory_order_release);
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

static int
test_crossfade_pull_partition(void)
{
	int16_t rendered[2][8];

	for (size_t run = 0; run < 2; ++run) {
		audio_mixer_t *mixer = NULL;
		audio_mixer_stream_t *stream = NULL;
		const int16_t first[] = { 1000, 1000, 1000, 1000 };
		const int16_t second[] = { 2000, 2000, 2000, 2000 };
		const int16_t third[] = { 3000, 3000, 3000, 3000 };
		struct audio_mixer_buffer_options options = { 0 };

		CHECK(audio_mixer_create(&mixer) == 0);
		CHECK(audio_mixer_stream_create(&stream, mixer, 0, 0) == 0);
		CHECK(audio_mixer_stream_append(stream, first, 2, NULL) == 0);
		options.crossfade = true;
		options.fade_in_frames = 1;
		CHECK(audio_mixer_stream_append(stream, second, 2, &options) == 0);
		CHECK(audio_mixer_stream_append(stream, third, 2, NULL) == 0);
		if (run == 0)
			CHECK(audio_mixer_pull(mixer, rendered[run], 4) == 4);
		else {
			for (size_t frame = 0; frame < 4; ++frame)
				CHECK(audio_mixer_pull(mixer,
				    rendered[run] + frame * 2, 1) == 1);
		}
		audio_mixer_destroy(mixer);
	}
	if (memcmp(rendered[0], rendered[1], sizeof(rendered[0])) != 0)
		fprintf(stderr, "crossfade bulk/single-frame tails: %d,%d vs %d,%d\n",
		    rendered[0][4], rendered[0][6], rendered[1][4], rendered[1][6]);
	CHECK(memcmp(rendered[0], rendered[1], sizeof(rendered[0])) == 0);
	return 0;
}

static int
test_crossfade_chain_partition(void)
{
	int16_t rendered[2][16];

	for (size_t run = 0; run < 2; ++run) {
		audio_mixer_t *mixer = NULL;
		audio_mixer_stream_t *stream = NULL;
		const int16_t first[] = {
			1000, 1000, 1000, 1000, 1000, 1000,
		};
		const int16_t second[] = {
			2000, 2000, 2000, 2000, 2000, 2000,
		};
		const int16_t middle[] = { 3000, 3000, 3000, 3000 };
		const int16_t fourth[] = {
			4000, 4000, 4000, 4000, 4000, 4000,
		};
		const int16_t fifth[] = {
			5000, 5000, 5000, 5000, 5000, 5000,
		};
		const size_t partitions[] = { 3, 2, 3 };
		struct audio_mixer_buffer_options options = {
			.fade_in_frames = 2,
			.crossfade = true,
		};
		size_t offset = 0;

		CHECK(audio_mixer_create(&mixer) == 0);
		CHECK(audio_mixer_stream_create(&stream, mixer, 0, 0) == 0);
		CHECK(audio_mixer_stream_append(stream, first, 3, NULL) == 0);
		CHECK(audio_mixer_stream_append(stream, second, 3, &options) == 0);
		CHECK(audio_mixer_stream_append(stream, middle, 2, NULL) == 0);
		CHECK(audio_mixer_stream_append(stream, fourth, 3, NULL) == 0);
		CHECK(audio_mixer_stream_append(stream, fifth, 3, &options) == 0);
		if (run == 0)
			CHECK(audio_mixer_pull(mixer, rendered[run], 8) == 8);
		else {
			for (size_t i = 0;
			    i < sizeof(partitions) / sizeof(partitions[0]); ++i) {
				CHECK(audio_mixer_pull(mixer,
				    rendered[run] + offset * 2, partitions[i])
				    == partitions[i]);
				offset += partitions[i];
			}
		}
		CHECK(audio_mixer_stream_is_idle(stream));
		audio_mixer_destroy(mixer);
	}
	CHECK(memcmp(rendered[0], rendered[1], sizeof(rendered[0])) == 0);
	CHECK(rendered[0][14] != 0 && rendered[0][15] != 0);
	return 0;
}

static int
test_loop_crossfade_partition(void)
{
	int16_t rendered[2][10];

	for (size_t run = 0; run < 2; ++run) {
		audio_mixer_t *mixer = NULL;
		audio_mixer_stream_t *stream = NULL;
		const int16_t looping[] = { 1000, 1000, 2000, 2000 };
		const int16_t successor[] = {
			3000, 3000, 4000, 4000, 5000, 5000,
		};
		const int16_t ending[] = { 6000, 6000, 7000, 7000 };
		struct audio_mixer_buffer_options options = { .loop = true };
		int16_t prelude[6];

		CHECK(audio_mixer_create(&mixer) == 0);
		CHECK(audio_mixer_stream_create(&stream, mixer, 0, 0) == 0);
		CHECK(audio_mixer_stream_append(stream, looping, 2, &options) == 0);
		CHECK(audio_mixer_pull(mixer, prelude, 3) == 3);
		options = (struct audio_mixer_buffer_options){
			.fade_in_frames = 2,
			.crossfade = true,
		};
		CHECK(audio_mixer_stream_append(stream, successor, 3, &options) == 0);
		CHECK(audio_mixer_stream_append(stream, ending, 2, NULL) == 0);
		if (run == 0)
			CHECK(audio_mixer_pull(mixer, rendered[run], 5) == 5);
		else {
			CHECK(audio_mixer_pull(mixer, rendered[run], 2) == 2);
			CHECK(audio_mixer_pull(mixer, rendered[run] + 4, 3) == 3);
		}
		CHECK(audio_mixer_stream_is_idle(stream));
		audio_mixer_destroy(mixer);
	}
	CHECK(memcmp(rendered[0], rendered[1], sizeof(rendered[0])) == 0);
	return 0;
}

static int
test_crossfade_delivery_acknowledgement(void)
{
	audio_mixer_t *mixer = NULL;
	audio_mixer_stream_t *stream = NULL;
	const int16_t first[] = { 1000, 1000, 1000, 1000 };
	const int16_t second[] = { 2000, 2000, 2000, 2000 };
	const int16_t third[] = { 3000, 3000, 3000, 3000 };
	struct audio_mixer_buffer_options options = {
		.fade_in_frames = 1,
		.crossfade = true,
	};
	struct drain_context context;
	struct timespec delay = { 0, 20000000L };
	int16_t output[8];
	uint64_t ticket = 0;
	st_thread_t thread;
	int thread_result;

	CHECK(audio_mixer_create(&mixer) == 0);
	CHECK(audio_mixer_stream_create(&stream, mixer, 0, 0) == 0);
	CHECK(audio_mixer_stream_append(stream, first, 2, NULL) == 0);
	CHECK(audio_mixer_stream_append(stream, second, 2, &options) == 0);
	CHECK(audio_mixer_stream_append(stream, third, 2, NULL) == 0);
	CHECK(audio_mixer_pull_pending(mixer, output, 4, &ticket) == 4);
	CHECK(ticket != 0 && audio_mixer_stream_is_idle(stream));
	context.stream = stream;
	atomic_init(&context.started, false);
	atomic_init(&context.completed, false);
	context.result = 123;
	CHECK(st_thread_create(&thread, drain_thread, &context)
	    == ST_THREAD_SUCCESS);
	while (!atomic_load_explicit(&context.started, memory_order_acquire))
		st_thread_yield();
	(void)st_thread_sleep(&delay, NULL);
	CHECK(!atomic_load_explicit(&context.completed, memory_order_acquire));
	audio_mixer_acknowledge(mixer, ticket);
	CHECK(st_thread_join(thread, &thread_result) == ST_THREAD_SUCCESS);
	CHECK(thread_result == 0 && context.result == 0);
	CHECK(atomic_load_explicit(&context.completed, memory_order_acquire));
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
	CHECK(test_crossfade_pull_partition() == 0);
	CHECK(test_crossfade_chain_partition() == 0);
	CHECK(test_loop_crossfade_partition() == 0);
	CHECK(test_crossfade_delivery_acknowledgement() == 0);
	return 0;
}
