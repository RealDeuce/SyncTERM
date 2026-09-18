/* Windows GDI backend integration tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "st_console.h"

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
check_client_size(const wchar_t *title, LONG width, LONG height)
{
	HWND window = FindWindowW(NULL, title);
	RECT rectangle;

	CHECK(window != NULL);
	CHECK(GetClientRect(window, &rectangle));
	if (rectangle.right != width || rectangle.bottom != height)
		fprintf(stderr, "client size: %ldx%ld, expected %ldx%ld\n",
		    (long)rectangle.right, (long)rectangle.bottom, (long)width,
		    (long)height);
	CHECK(rectangle.right == width && rectangle.bottom == height);
	return 0;
}

static int
post_key(HWND window, UINT virtual_key, bool pressed)
{
	LPARAM detail = (LPARAM)(MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC)
	    << 16);

	if (!pressed)
		detail |= (LPARAM)3 << 30;
	return PostMessageW(window, pressed ? WM_KEYDOWN : WM_KEYUP,
	    virtual_key, detail) ? 0 : 1;
}

static int
check_escape_event(st_console_t *console)
{
	static const wchar_t title[] = L"st_console GDI input test";
	struct st_console_event event;
	struct timespec deadline;
	HWND window;
	unsigned int escape_keys = 0;
	unsigned int escape_text = 0;
	bool marker = false;

	CHECK(st_console_set_title(console, "st_console GDI input test") == 0);
	window = FindWindowW(NULL, title);
	CHECK(window != NULL);
	CHECK(st_console_events_clear(console) == 0);
	CHECK(post_key(window, VK_ESCAPE, true) == 0);
	CHECK(post_key(window, VK_ESCAPE, false) == 0);
	CHECK(post_key(window, VK_F1, true) == 0);
	CHECK(post_key(window, VK_F1, false) == 0);
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	++deadline.tv_sec;
	while (!marker
	    && st_console_event_timedwait(console, &event, &deadline) == 0) {
		if (event.type == ST_CONSOLE_EVENT_KEY_CODE) {
			if (event.data.key.code == 27)
				++escape_keys;
			else if (event.data.key.code == ST_CONSOLE_KEY_F1)
				marker = true;
		}
		else if (event.type == ST_CONSOLE_EVENT_TEXT
		    && event.data.text.codepoint == 0x1b)
			++escape_text;
	}
	CHECK(marker);
	CHECK(escape_keys == 1);
	CHECK(escape_text == 0);
	return 0;
}

int
main(void)
{
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_GDI,
	};
	const struct st_console_config config = {
		.columns = 80,
		.rows = 25,
		.cell_width = 8,
		.cell_height = 16,
		.wrap = true,
		.aspect_width = 4,
		.aspect_height = 3,
	};
	struct st_console_display_config scaled_display = config_display;
	struct st_console_config scaled = config;
	st_console_t *first = NULL;
	st_console_t *second = NULL;
	uint32_t width;
	uint32_t height;
	int status = st_console_create(&first, &config_display, &config);

	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	scaled_display.initial_scaling = 1.25;
	CHECK(st_console_create(&second, &scaled_display, &scaled) == 0);
	CHECK(st_console_set_title(first, "st_console GDI one") == 0);
	CHECK(st_console_set_title(second, "st_console GDI two") == 0);
	CHECK(check_client_size(L"st_console GDI one", 640, 480) == 0);
	CHECK(check_client_size(L"st_console GDI two", 800, 600) == 0);
	CHECK(st_console_set_pointer_visible(first, false) == 0);
	CHECK(st_console_set_pointer_visible(first, true) == 0);
	CHECK(st_console_set_pixel(first, 4, 5, UINT32_C(0x123456)) == 0);
	CHECK(st_console_present(first) == 0);
	CHECK(st_console_present(second) == 0);
	CHECK(check_escape_event(first) == 0);
	CHECK(st_console_resize(first, 90, 25) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 720 && height == 400);
	CHECK(check_client_size(L"st_console GDI input test", 720, 540) == 0);
	CHECK(st_console_set_fullscreen(first, true) == 0);
	CHECK(st_console_set_fullscreen(first, false) == 0);
	CHECK(check_client_size(L"st_console GDI input test", 720, 540) == 0);
	st_console_destroy(second);
	st_console_destroy(first);
	return 0;
}
