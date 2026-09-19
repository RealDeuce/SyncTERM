/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"

#include "st_time.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
		    __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

struct scheduler_state {
	st_mutex_t mutex;
	st_condition_t changed;
	uint64_t times[32];
	size_t count;
};

static int
present(st_console_t *console)
{
	struct scheduler_state *state = console->display->backend_state;
	uint64_t now = 0;

	(void)st_time_monotonic_ms(&now);
	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	if (state->count < sizeof(state->times) / sizeof(state->times[0]))
		state->times[state->count] = now;
	++state->count;
	(void)st_condition_broadcast(&state->changed);
	(void)st_mutex_unlock(&state->mutex);
	return 0;
}

static const struct st_console_backend_ops ops = {
	.presents_pixels = true,
	.deferred_present = true,
	.animated_present = true,
	.present = present,
};

static void
wait_for_count(struct scheduler_state *state, size_t count)
{
	struct timespec deadline;

	CHECK(st_time_utc_deadline_after_ms(&deadline, 2000) == 0);
	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	while (state->count < count) {
		CHECK(st_condition_timedwait(&state->changed, &state->mutex,
		    &deadline) == ST_THREAD_SUCCESS);
	}
	(void)st_mutex_unlock(&state->mutex);
}

static size_t
read_count(struct scheduler_state *state)
{
	size_t result;

	CHECK(st_mutex_lock(&state->mutex) == ST_THREAD_SUCCESS);
	result = state->count;
	(void)st_mutex_unlock(&state->mutex);
	return result;
}

int
main(void)
{
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {
		.columns = 1, .rows = 1, .cell_width = 1, .cell_height = 1,
		.blink_mode = ST_CONSOLE_BLINK_ATARI,
	};
	const struct timespec short_pause = {.tv_nsec = 30000000};
	struct scheduler_state state = {0};
	st_console_t *console = NULL;
	size_t baseline;
	uint64_t content_start = 0;
	uint64_t animation_start;

	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.changed) == ST_THREAD_SUCCESS);
	CHECK(st_console_create(&console, &display, &config) == 0);
	console->display->backend_ops = &ops;
	console->display->backend_state = &state;
	CHECK(st_console_start_presentation_worker(console) == 0);
	wait_for_count(&state, 1);
	baseline = read_count(&state);
	(void)st_time_monotonic_ms(&content_start);
	for (uint32_t i = 0; i < 100; ++i)
		CHECK(st_console_set_pixel(console, 0, 0,
		    ST_PIXEL_COLOR_RGB_TAG | (i & 1U)) == 0);
	wait_for_count(&state, baseline + 1);
	CHECK(state.times[baseline] >= content_start + 8);
	CHECK(state.times[baseline] <= content_start + 500);
	(void)st_thread_sleep(&short_pause, NULL);
	CHECK(read_count(&state) == baseline + 1);

	CHECK(st_console_set_blink_mode(console, ST_CONSOLE_BLINK_PC) == 0);
	wait_for_count(&state, baseline + 2);
	animation_start = state.times[baseline + 1];
	wait_for_count(&state, baseline + 3);
	CHECK(state.times[baseline + 2] >= animation_start + 100);
	CHECK(state.times[baseline + 2] <= animation_start + 500);

	CHECK(st_console_set_blink_mode(console, ST_CONSOLE_BLINK_ATARI) == 0);
	wait_for_count(&state, baseline + 4);
	(void)st_thread_sleep(&short_pause, NULL);
	(void)st_thread_sleep(&short_pause, NULL);
	CHECK(read_count(&state) == baseline + 4);

	st_console_destroy(console);
	st_condition_destroy(&state.changed);
	st_mutex_destroy(&state.mutex);
	return EXIT_SUCCESS;
}
