/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_internal.h"
#include "console_ansi_system.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		exit(EXIT_FAILURE); \
	} \
} while (false)

/* Replace only the native input mechanism. Input is available immediately,
 * as with redirected regular files, without requiring a scheduler race. */
struct st_console_ansi_system {
	unsigned int reads;
};

static struct st_console_ansi_system input;
static st_console_display_t *expected_display;
static bool fail_open;

int
st_console_ansi_system_open(struct st_console_ansi_system **result,
	bool *available, bool utf8)
{
	CHECK(utf8);
	input.reads = 0;
	if (fail_open)
		return IO_ERROR_IO;
	*available = true;
	*result = &input;
	return 0;
}

int
st_console_ansi_system_read(struct st_console_ansi_system *system,
	void *buffer, size_t size, size_t *received, unsigned int timeout_ms)
{
	st_console_t *active = NULL;

	(void)timeout_ms;
	CHECK(system == &input && size != 0);
	CHECK(st_console_display_active(expected_display, &active) == 0);
	CHECK(active != NULL);
	*received = 0;
	if (system->reads++ != 0)
		return IO_ERROR_NOT_CONNECTED;
	*(unsigned char *)buffer = 'x';
	*received = 1;
	return 0;
}

void st_console_ansi_system_stop(struct st_console_ansi_system *system)
{
	CHECK(system == &input);
}

bool
st_console_ansi_system_size(struct st_console_ansi_system *system,
	uint32_t *columns, uint32_t *rows)
{
	(void)columns;
	(void)rows;
	CHECK(system == &input);
	return false;
}

void st_console_ansi_system_close(struct st_console_ansi_system *system)
{
	CHECK(system == &input);
}

static void
check_events(st_console_t *console)
{
	struct timespec deadline;
	struct st_console_event event;

	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	deadline.tv_sec += 2;
	CHECK(st_console_event_timedwait(console, &event, &deadline) == 0);
	CHECK(event.type == ST_CONSOLE_EVENT_TEXT && event.data.text.codepoint == 'x');
	CHECK(st_console_event_timedwait(console, &event, &deadline) == 0);
	CHECK(event.type == ST_CONSOLE_EVENT_CLOSE);
	CHECK(st_console_event_try(console, &event) == IO_ERROR_WOULD_BLOCK);
}

int
main(void)
{
	struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config config = {.columns = 8, .rows = 3};
	st_console_t *console = NULL;

	CHECK(st_console_display_create(&expected_display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, expected_display, &config) == 0);
	atomic_store(&expected_display->input_active, NULL);
	CHECK(st_console_ansi_init(console, NULL) == 0);
	CHECK(input.reads == 0);
	CHECK(expected_display->backend_ops->start_input != NULL);
	CHECK(expected_display->backend_ops->start_input(expected_display)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(input.reads == 0);
	atomic_store(&expected_display->input_active, console);
	CHECK(expected_display->backend_ops->start_input(expected_display) == 0);
	check_events(console);
	st_console_destroy(console);
	st_console_display_destroy(expected_display);

	/* Backend preparation may be unwound before readers are started. */
	CHECK(st_console_display_create(&expected_display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, expected_display, &config) == 0);
	atomic_store(&expected_display->input_active, NULL);
	CHECK(st_console_ansi_init(console, NULL) == 0);
	CHECK(input.reads == 0);
	st_console_destroy(console);
	CHECK(input.reads == 0);
	st_console_display_destroy(expected_display);

	/* Also exercise publication through the ordinary constructor. */
	display_config.backend = ST_CONSOLE_BACKEND_ANSI;
	CHECK(st_console_display_create(&expected_display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, expected_display, &config) == 0);
	check_events(console);
	st_console_destroy(console);
	st_console_display_destroy(expected_display);

	/* A failed constructor must not leave a freed input destination. */
	fail_open = true;
	console = NULL;
	CHECK(st_console_display_create(&expected_display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, expected_display, &config)
	    == IO_ERROR_IO);
	CHECK(console == NULL && input.reads == 0);
	CHECK(st_console_display_active(expected_display, &console) == 0);
	CHECK(console == NULL);
	st_console_display_destroy(expected_display);
	return EXIT_SUCCESS;
}
