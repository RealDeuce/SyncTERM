/* Exercise window snapping and scaling persistence through application exit. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_application.h"
#include "filesystem.h"
#include "ini.h"
#include "st_threads.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (false)

struct scaling_test {
	st_console_display_t *display;
	st_thread_t thread;
	bool started;
	int result;
};

static void
pause_test(void)
{
	(void)st_thread_sleep(&(struct timespec){0, 10000000}, NULL);
}

static bool
menu_visible(st_console_display_t *display)
{
	st_console_t *console = NULL;
	struct st_console_cell cells[80];
	char line[81];

	if (st_console_display_active(display, &console) != 0 || console == NULL)
		return false;
	for (uint32_t y = 0; y < 25; ++y) {
		const struct st_console_rect row = {0, y, 80, 1};

		if (st_console_read_cells(console, &row, cells, 80) != 0)
			return false;
		for (size_t x = 0; x < 80; ++x) {
			char text[8];
			size_t required;

			line[x] = st_console_font_glyph_text(console,
			    cells[x].glyph_font, cells[x].glyph, text, sizeof(text),
			    &required) == 0 && required == 1 ? text[0] : '?';
		}
		line[80] = '\0';
		if (strstr(line, "SyncTERM Settings") != NULL)
			return true;
	}
	return false;
}

static Window
application_window(Display *display)
{
	Window root, parent, *children = NULL, result = None;
	unsigned int count = 0;

	if (XQueryTree(display, DefaultRootWindow(display), &root, &parent,
	    &children, &count) == 0)
		return None;
	for (unsigned int i = 0; i < count && result == None; ++i) {
		char *name = NULL;

		if (XFetchName(display, children[i], &name) != 0 && name != NULL
		    && strncmp(name, u8"SyncTERM 2.0\u03b1",
		    sizeof(u8"SyncTERM 2.0\u03b1") - 1) == 0)
			result = children[i];
		if (name != NULL)
			(void)XFree(name);
	}
	if (children != NULL)
		(void)XFree(children);
	return result;
}

static int
snap_and_exit(void *argument)
{
	struct scaling_test *test = argument;
	Display *display = NULL;
	Window window;
	XEvent event = {0};
	double scaling = 0;
	int result = 1;
	struct st_console_event finish = {.type = ST_CONSOLE_EVENT_CLOSE};

	for (unsigned i = 0; i < 1000 && !menu_visible(test->display); ++i)
		pause_test();
	if (!menu_visible(test->display))
		goto finished;
	display = XOpenDisplay(NULL);
	if (display == NULL)
		goto finished;
	window = application_window(display);
	if (window == None)
		goto finished;
	event.xkey = (XKeyEvent){
		.type = KeyPress, .display = display, .window = window,
		.root = DefaultRootWindow(display), .same_screen = True,
		.state = Mod1Mask, .keycode = XKeysymToKeycode(display, XK_Right),
	};
	if (event.xkey.keycode == 0
	    || XSendEvent(display, window, False, KeyPressMask, &event) == 0)
		goto finished;
	event.xkey.type = KeyRelease;
	if (XSendEvent(display, window, False, KeyReleaseMask, &event) == 0)
		goto finished;
	(void)XFlush(display);
	for (unsigned i = 0; i < 1000; ++i) {
		if (st_console_display_get_scaling(test->display, &scaling) == 0
		    && scaling == 2.0)
			break;
		pause_test();
	}
	if (scaling != 2.0)
		goto finished;
	finish = (struct st_console_event){
		.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = 27,
	};
	result = 0;
finished:
	if (display != NULL)
		(void)XCloseDisplay(display);
	/* An assertion failure must also unblock the application's menu loop. */
	if (st_console_display_post_event(test->display, &finish) != 0)
		result = 1;
	return result;
}

static void
application_ready(void *context, st_console_display_t *display,
	st_audio_runtime_t *audio)
{
	struct scaling_test *test = context;
	(void)audio;
	test->display = display;
	test->started = st_thread_create(&test->thread, snap_and_exit, test)
	    == ST_THREAD_SUCCESS;
	if (!test->started) {
		const struct st_console_event close = {.type = ST_CONSOLE_EVENT_CLOSE};
		(void)st_console_display_post_event(display, &close);
	}
}

static void
application_release(void *context)
{
	struct scaling_test *test = context;
	if (test->started
	    && st_thread_join(test->thread, &test->result) != ST_THREAD_SUCCESS)
		test->result = 1;
}

int
main(int argc, char **argv)
{
	struct scaling_test test = {.result = 1};
	char *arguments[] = {"syncterm", "-iX"};
	char settings_path[1024];
	ini_document_t *document = NULL;
	FILE *file;
	int status;

	if (getenv("DISPLAY") == NULL)
		return 77;
	CHECK(argc == 2 && strlen(argv[1]) < 900);
	CHECK(fs_create_directories(argv[1]) == 0);
	(void)snprintf(settings_path, sizeof(settings_path), "%s/syncterm.ini", argv[1]);
	file = fopen(settings_path, "wb");
	CHECK(file != NULL);
	CHECK(fputs("[SyncTERM]\nScreenMode=80x25\nConfirmClose=false\nAudioModes=0\n"
	    "ScalingFactor=1\n[Unrelated]\nKeep=this\n", file) >= 0);
	CHECK(fclose(file) == 0);
	const struct st_application_host host = {
		.directory = argv[1], .ready = application_ready,
		.release = application_release, .context = &test,
	};
	status = st_application_run(2, arguments, &host);
	CHECK(test.started && test.result == 0);
	CHECK(status == 0);
	file = fopen(settings_path, "rb");
	CHECK(file != NULL);
	CHECK(ini_document_read(&document, file) == 0);
	CHECK(fclose(file) == 0);
	CHECK(ini_section_get_double(ini_document_find_section(document,
	    "SyncTERM"), "ScalingFactor", 0.0) == 2.0);
	CHECK(ini_document_find_section(document, "Unrelated") != NULL);
	ini_document_free(&document);
	return 0;
}
