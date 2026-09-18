/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "st_console.h"

#include "dynamic_library.h"
#include "io_error.h"
#include "st_threads.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
wait_for_close(st_console_t *console, const struct timespec *deadline)
{
	struct st_console_event event;
	int status;

	do {
		status = st_console_event_timedwait(console, &event, deadline);
	} while (status == 0 && event.type != ST_CONSOLE_EVENT_CLOSE);
	return status;
}

struct writer_context {
	st_console_t *console;
	uint32_t colour;
	bool succeeded;
};

static int
write_pixels(void *argument)
{
	struct writer_context *context = argument;

	context->succeeded = true;
	for (uint32_t i = 0; i < 32; ++i) {
		if (st_console_set_pixel(context->console, i % 8, i % 16,
		    context->colour) != 0) {
			context->succeeded = false;
			break;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_SDL,
	};
	const struct st_console_config config = {
		.columns = 8,
		.rows = 4,
		.cell_width = 8,
		.cell_height = 16,
		.wrap = true,
	};
	struct st_console_display_config automatic_display = config_display;
	struct st_console_config automatic = config;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	uint32_t width = 0;
	uint32_t height = 0;
	dynamic_library_t *library = NULL;
	int (SDLCALL *push_event)(SDL_Event *) = NULL;
	void *symbol;
	SDL_Event quit = {.type = SDL_QUIT};
	struct timespec deadline;
	struct writer_context writers[2];
	st_thread_t threads[2];
	int status;

	(void)argc;
	(void)argv;

	status = st_console_create(&first, &config_display, &config);
	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	automatic_display.backend = ST_CONSOLE_BACKEND_AUTO;
	CHECK(st_console_create(&second, &automatic_display, &automatic) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 64 && height == 64);
	CHECK(st_console_set_pixel(first, 4, 5,
	    UINT32_C(0x010203)) == 0);
	CHECK(st_console_set_title(first, "st_console SDL test") == 0);
	CHECK(st_console_set_pointer_visible(first, false) == 0);
	CHECK(st_console_set_pointer_visible(first, true) == 0);
	CHECK(st_console_resize(first, 10, 5) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 80 && height == 80);
	CHECK(st_console_present(first) == 0);
	CHECK(st_console_present(second) == 0);
	writers[0] = (struct writer_context){first, UINT32_C(0x141e28), false};
	writers[1] = (struct writer_context){second, UINT32_C(0x323c46), false};
	CHECK(st_thread_create(&threads[0], write_pixels, &writers[0])
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_create(&threads[1], write_pixels, &writers[1])
	    == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(threads[0], NULL) == ST_THREAD_SUCCESS);
	CHECK(st_thread_join(threads[1], NULL) == ST_THREAD_SUCCESS);
	CHECK(writers[0].succeeded && writers[1].succeeded);
	{
		static const char *const names[] = {"SDL2", "SDL2-2.0"};

		CHECK(dynamic_library_open(&library, names, 2, 0,
		    DYNAMIC_LIBRARY_LOCAL) == 0);
	}
	symbol = dynamic_library_symbol(library, "SDL_PushEvent");
	CHECK(symbol != NULL);
	push_event = (int (SDLCALL *)(SDL_Event *))symbol;
	CHECK(push_event(&quit) == 1);
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	++deadline.tv_sec;
	CHECK(wait_for_close(first, &deadline) == 0);
	CHECK(wait_for_close(second, &deadline) == 0);
	CHECK(dynamic_library_close(library) == 0);
	st_console_destroy(first);
	st_console_destroy(second);
	{
		const struct st_console_display_config display_config = {
			.backend = ST_CONSOLE_BACKEND_SDL,
		};
		st_console_display_t *display = NULL;
		st_console_t *active = NULL;

		CHECK(st_console_display_create(&display, &display_config) == 0);
		CHECK(st_console_create_on_display(&first, display, &config) == 0);
		{
			struct st_console_config invalid = config;
			st_console_t *unchanged = first;

			invalid.columns = invalid.rows = 1;
			invalid.cell_width = invalid.cell_height = 1;
			invalid.aspect_width = UINT32_MAX;
			invalid.aspect_height = 1;
			CHECK(st_console_create_on_display(&unchanged, display, &invalid)
			    == IO_ERROR_OUT_OF_RANGE);
			CHECK(unchanged == first);
			CHECK(st_console_display_active(display, &active) == 0);
			CHECK(active == first);
		}
		for (unsigned int i = 0; i < 20; ++i) {
			CHECK(st_console_create_on_display(&second, display, &config) == 0);
			CHECK(st_console_display_activate(display, second) == 0);
			CHECK(st_console_set_pixel(second, 4, 5, i) == 0);
			CHECK(st_console_present(second) == 0);
			st_console_destroy(second);
			CHECK(st_console_display_active(display, &active) == 0);
			CHECK(active == first);
			CHECK(st_console_present(first) == 0);
		}
		st_console_destroy(first);
		st_console_display_destroy(display);
	}
	return 0;
}
