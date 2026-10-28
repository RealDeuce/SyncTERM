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
	st_console_display_t *first_display = NULL;
	uint32_t width = 0;
	uint32_t height = 0;
	double scaling;
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

	status = st_console_display_create(&first_display, &config_display);
	if (status == 0)
		status = st_console_create_on_display(&first, first_display, &config);
	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	CHECK(st_console_display_get_scaling(first_display, &scaling) == 0);
	CHECK(scaling == 1.0);
	automatic_display.backend = ST_CONSOLE_BACKEND_AUTO;
	CHECK(st_console_create(&second, &automatic_display, &automatic) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 64 && height == 64);
	CHECK(st_console_set_pixel(first, 4, 5,
	    ST_CONSOLE_COLOR_RGB_VALUE(1, 2, 3)) == 0);
	CHECK(st_console_set_title(first, "st_console SDL test") == 0);
	CHECK(st_console_set_pointer_visible(first, false) == 0);
	CHECK(st_console_set_pointer_visible(first, true) == 0);
	int pointer_shape_status = st_console_set_pointer_shape(first,
	    ST_CONSOLE_POINTER_TEXT);
	CHECK(pointer_shape_status == 0
	    || pointer_shape_status == IO_ERROR_NOT_SUPPORTED);
	if (pointer_shape_status == 0) {
		CHECK(st_console_set_pointer_shape(first, ST_CONSOLE_POINTER_LINK)
		    == 0);
		CHECK(st_console_set_pointer_shape(first, ST_CONSOLE_POINTER_DEFAULT)
		    == 0);
	}
	CHECK(st_console_resize(first, 10, 5) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 80 && height == 80);
	CHECK(st_console_present(first) == 0);
	CHECK(st_console_present(second) == 0);
	writers[0] = (struct writer_context){first,
	    ST_CONSOLE_COLOR_RGB_VALUE(0x14, 0x1e, 0x28), false};
	writers[1] = (struct writer_context){second,
	    ST_CONSOLE_COLOR_RGB_VALUE(0x32, 0x3c, 0x46), false};
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
	SDL_Window *(SDLCALL *window_from_id)(Uint32) =
	    (SDL_Window *(SDLCALL *)(Uint32))dynamic_library_symbol(library,
	    "SDL_GetWindowFromID");
	const char *(SDLCALL *window_title)(SDL_Window *) =
	    (const char *(SDLCALL *)(SDL_Window *))dynamic_library_symbol(library,
	    "SDL_GetWindowTitle");
	CHECK(window_from_id != NULL && window_title != NULL);
	Uint32 window_id = 0;
	for (Uint32 i = 1; i < 32; ++i) {
		SDL_Window *window = window_from_id(i);
		if (window != NULL && strcmp(window_title(window), "st_console SDL test") == 0) {
			window_id = i;
			break;
		}
	}
	CHECK(window_id != 0);
	CHECK(st_console_events_clear(first) == 0);
	const SDL_Scancode keys[] = {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_1,
	    SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0};
	const SDL_Keycode syms[] = {SDLK_LALT, SDLK_KP_1, SDLK_KP_3, SDLK_KP_0};
	for (size_t i = 0; i < 4; ++i) {
		SDL_Event key = {.type = SDL_KEYDOWN};
		key.key.windowID = window_id;
		key.key.keysym.scancode = keys[i];
		key.key.keysym.sym = syms[i];
		key.key.keysym.mod = KMOD_ALT | KMOD_NUM;
		CHECK(push_event(&key) == 1);
		if (i != 0) {
			key.type = SDL_KEYUP;
			CHECK(push_event(&key) == 1);
		}
	}
	SDL_Event release = {.type = SDL_KEYUP};
	release.key.windowID = window_id;
	release.key.keysym.scancode = SDL_SCANCODE_LALT;
	release.key.keysym.sym = SDLK_LALT;
	release.key.keysym.mod = KMOD_NUM;
	CHECK(push_event(&release) == 1);
	/* Emulate a host-generated numeric commit *after* the Alt release. */
	SDL_Event duplicate = {.type = SDL_TEXTINPUT};
	duplicate.text.windowID = window_id;
	strcpy(duplicate.text.text, "\xc3\xa9");
	CHECK(push_event(&duplicate) == 1);
	SDL_Event marker = {.type = SDL_KEYDOWN};
	marker.key.windowID = window_id;
	marker.key.keysym.scancode = SDL_SCANCODE_F1;
	marker.key.keysym.sym = SDLK_F1;
	CHECK(push_event(&marker) == 1);
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	deadline.tv_sec += 2;
	unsigned int composed = 0;
	bool marked = false;
	struct st_console_event event;
	while (!marked && st_console_event_timedwait(first, &event, &deadline) == 0) {
		if (event.type == ST_CONSOLE_EVENT_PHYSICAL_KEY) continue;
		if (event.type == ST_CONSOLE_EVENT_KEY_CODE
		    && event.data.key.code == ST_CONSOLE_KEY_F1) {
			marked = true;
			continue;
		}
		CHECK(event.type == ST_CONSOLE_EVENT_CODEPAGE_BYTE
		    && event.data.codepage_byte == 130 && composed++ == 0);
	}
	CHECK(marked && composed == 1);
	CHECK(push_event(&quit) == 1);
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	++deadline.tv_sec;
	CHECK(wait_for_close(first, &deadline) == 0);
	CHECK(wait_for_close(second, &deadline) == 0);
	st_console_destroy(first);
	st_console_display_destroy(first_display);
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
