/* X11 backend integration and locale-independent key translation tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"
#include "console_internal.h"
#include "console_config.h"
#include "console_x11_keys.h"

#include "io_error.h"

#include <X11/keysym.h>
#include <X11/Xutil.h>
#if ST_CONSOLE_HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif

#include <locale.h>
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
} while (0)

static int
check_keys(void)
{
	CHECK(st_console_x11_modifiers(ShiftMask | ControlMask | Mod4Mask)
	    == (ST_CONSOLE_MODIFIER_SHIFT | ST_CONSOLE_MODIFIER_CONTROL
	    | ST_CONSOLE_MODIFIER_GUI));
	CHECK(st_console_x11_key_from_keysym(XK_Left) == ST_CONSOLE_KEY_LEFT);
	CHECK(st_console_x11_key_from_keysym(XK_KP_Enter) == 13);
	CHECK(st_console_x11_key_from_keysym(XK_F12) == ST_CONSOLE_KEY_F12);
	CHECK(st_console_x11_evdev_from_keysym(XK_a) == 30);
	CHECK(st_console_x11_evdev_from_keysym(XK_A) == 30);
	CHECK(st_console_x11_evdev_from_keysym(XK_KP_Divide) == 98);
	CHECK(st_console_x11_evdev_from_keyname("AD01") == 16);
	CHECK(st_console_x11_evdev_from_keyname("FK12") == 88);
	CHECK(st_console_x11_evdev_from_keyname("KPEN") == 96);
	CHECK(st_console_x11_evdev_from_keyname("NOPE") == 0);
	return 0;
}

static Window
find_named_window(Display *display, Window parent, const char *wanted)
{
	Window root;
	Window actual_parent;
	Window *children = NULL;
	unsigned int child_count = 0;
	Window result = None;
	char *name = NULL;

	if (XFetchName(display, parent, &name) != 0 && name != NULL) {
		if (strcmp(name, wanted) == 0)
			result = parent;
		(void)XFree(name);
	}
	if (result != None || XQueryTree(display, parent, &root,
	    &actual_parent, &children, &child_count) == 0)
		return result;
	for (unsigned int i = 0; i < child_count && result == None; ++i)
		result = find_named_window(display, children[i], wanted);
	if (children != NULL)
		(void)XFree(children);
	return result;
}

static int
check_window_size(const char *title, int width, int height,
	int minimum_width, int minimum_height)
{
	Display *display = XOpenDisplay(NULL);
	Window window;
	XWindowAttributes attributes;
	XSizeHints hints;
	long supplied = 0;

	CHECK(display != NULL);
	window = find_named_window(display, DefaultRootWindow(display), title);
	CHECK(window != None);
	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		CHECK(XGetWindowAttributes(display, window, &attributes) != 0);
		if (attributes.width == width && attributes.height == height)
			break;
		(void)nanosleep(&(struct timespec){0, 10000000}, NULL);
	}
	CHECK(attributes.width == width && attributes.height == height);
	CHECK(XGetWMNormalHints(display, window, &hints, &supplied) != 0);
	CHECK((hints.flags & PMinSize) != 0);
	CHECK(hints.min_width == minimum_width
	    && hints.min_height == minimum_height);
	CHECK((hints.flags & PAspect) != 0);
	CHECK(hints.min_aspect.x == 4 && hints.min_aspect.y == 3);
	(void)XCloseDisplay(display);
	return 0;
}

static int
window_position(Display *display, Window window, int *x, int *y)
{
	Window child;

	return XTranslateCoordinates(display, window,
	    DefaultRootWindow(display), 0, 0, x, y, &child) == 0;
}

static int send_key_with_state(Display *, Window, KeySym, bool,
	unsigned int);

static int
check_current_monitor_centering(Display *display, Window window)
{
#if ST_CONSOLE_HAVE_XRANDR
	int count = 0;
	XRRMonitorInfo *monitors = XRRGetMonitors(display,
	    DefaultRootWindow(display), True, &count);
	int target = -1;

	for (int i = 1; monitors != NULL && i < count; ++i) {
		if (monitors[i].x != monitors[0].x
		    || monitors[i].y != monitors[0].y) {
			target = i;
			break;
		}
	}
	if (target >= 0) {
		int x;
		int y;
		long center_x;
		long center_y;
		long monitor_center_x;
		long monitor_center_y;

		CHECK(XMoveWindow(display, window, monitors[target].x + 20,
		    monitors[target].y + 20) != 0);
		(void)XSync(display, False);
		(void)nanosleep(&(struct timespec){0, 50000000}, NULL);
		CHECK(send_key_with_state(display, window, XK_Home, true,
		    Mod1Mask) == 0);
		CHECK(send_key_with_state(display, window, XK_Home, false,
		    Mod1Mask) == 0);
		(void)XSync(display, False);
		(void)nanosleep(&(struct timespec){0, 50000000}, NULL);
		CHECK(window_position(display, window, &x, &y) == 0);
		center_x = x + 85 / 2;
		center_y = y + 64 / 2;
		monitor_center_x = monitors[target].x
		    + monitors[target].width / 2;
		monitor_center_y = monitors[target].y
		    + monitors[target].height / 2;
		CHECK(labs(center_x - monitor_center_x)
		    < monitors[target].width / 4);
		CHECK(labs(center_y - monitor_center_y)
		    < monitors[target].height / 4);
	}
	if (monitors != NULL)
		XRRFreeMonitors(monitors);
#else
	(void)display;
	(void)window;
#endif
	return 0;
}

static int
send_key_with_state(Display *display, Window window, KeySym keysym,
	bool pressed, unsigned int state)
{
	XEvent event = {0};

	event.xkey.type = pressed ? KeyPress : KeyRelease;
	event.xkey.display = display;
	event.xkey.window = window;
	event.xkey.root = DefaultRootWindow(display);
	event.xkey.time = CurrentTime;
	event.xkey.same_screen = True;
	event.xkey.state = state;
	event.xkey.keycode = XKeysymToKeycode(display, keysym);
	if (event.xkey.keycode == 0)
		return 1;
	return XSendEvent(display, window, False,
	    pressed ? KeyPressMask : KeyReleaseMask, &event) == 0;
}

static int
send_key(Display *display, Window window, KeySym keysym, bool pressed)
{
	return send_key_with_state(display, window, keysym, pressed, 0);
}

static int
check_escape_event(st_console_t *console)
{
	static const char title[] = "st_console X11 input test";
	struct st_console_event event;
	struct timespec deadline;
	Display *display;
	Window window;
	unsigned int escape_keys = 0;
	unsigned int escape_text = 0;
	unsigned int alt_b = 0;
	bool marker = false;

	CHECK(st_console_set_title(console, title) == 0);
	CHECK(st_console_set_pixel(console, 0, 0, UINT32_C(0x010203)) == 0);
	CHECK(st_console_present(console) == 0);
	display = XOpenDisplay(NULL);
	CHECK(display != NULL);
	window = find_named_window(display, DefaultRootWindow(display), title);
	CHECK(window != None);
	CHECK(check_window_size(title, 85, 64, 85, 64) == 0);
	CHECK(st_console_events_clear(console) == 0);
	CHECK(send_key(display, window, XK_Escape, true) == 0);
	CHECK(send_key(display, window, XK_Escape, false) == 0);
	CHECK(send_key(display, window, XK_F1, true) == 0);
	CHECK(send_key(display, window, XK_F1, false) == 0);
	(void)XFlush(display);
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
	CHECK(st_console_events_clear(console) == 0);
	CHECK(send_key_with_state(display, window, XK_b, true, Mod1Mask) == 0);
	CHECK(send_key_with_state(display, window, XK_b, false, Mod1Mask) == 0);
	CHECK(send_key(display, window, XK_F1, true) == 0);
	CHECK(send_key(display, window, XK_F1, false) == 0);
	(void)XFlush(display);
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	++deadline.tv_sec;
	marker = false;
	while (!marker
	    && st_console_event_timedwait(console, &event, &deadline) == 0) {
		if (event.type == ST_CONSOLE_EVENT_PHYSICAL_KEY
		    && event.data.physical_key.evdev == 48
		    && event.data.physical_key.pressed
		    && (event.data.physical_key.modifiers
		    & ST_CONSOLE_MODIFIER_ALT) != 0)
			++alt_b;
		else if (event.type == ST_CONSOLE_EVENT_KEY_CODE
		    && event.data.key.code == ST_CONSOLE_KEY_F1)
			marker = true;
	}
	CHECK(marker);
	CHECK(alt_b == 1);
	/* Backend shortcuts must not wait on a caller's logical-state lock. */
	CHECK(st_rwlock_write_lock(&console->state_lock) == ST_THREAD_SUCCESS);
	CHECK(send_key_with_state(display, window, XK_Right, true, Mod1Mask)
	    == 0);
	CHECK(send_key_with_state(display, window, XK_Right, false, Mod1Mask)
	    == 0);
	(void)XFlush(display);
	(void)nanosleep(&(struct timespec){0, 50000000}, NULL);
	CHECK(check_window_size(title, 171, 128, 85, 64) == 0);
	CHECK(send_key_with_state(display, window, XK_Left, true, Mod1Mask)
	    == 0);
	CHECK(send_key_with_state(display, window, XK_Left, false, Mod1Mask)
	    == 0);
	(void)XFlush(display);
	(void)nanosleep(&(struct timespec){0, 50000000}, NULL);
	CHECK(check_window_size(title, 85, 64, 85, 64) == 0);
	CHECK(st_rwlock_unlock(&console->state_lock) == ST_THREAD_SUCCESS);
	CHECK(XMoveWindow(display, window, 0, 0) != 0);
	(void)XSync(display, False);
	CHECK(send_key_with_state(display, window, XK_Home, true, Mod1Mask)
	    == 0);
	CHECK(send_key_with_state(display, window, XK_Home, false, Mod1Mask)
	    == 0);
	(void)XSync(display, False);
	(void)nanosleep(&(struct timespec){0, 50000000}, NULL);
	{
		int x;
		int y;

		CHECK(window_position(display, window, &x, &y) == 0);
		CHECK(x > 0 && y > 0);
	}
	CHECK(check_current_monitor_centering(display, window) == 0);
	CHECK(st_console_events_clear(console) == 0);
	for (unsigned int i = 0; i < 200; ++i) {
		XEvent configure = {0};

		configure.xconfigure.type = ConfigureNotify;
		configure.xconfigure.display = display;
		configure.xconfigure.event = window;
		configure.xconfigure.window = window;
		configure.xconfigure.width = 1200 + (int)(i & 31U);
		configure.xconfigure.height = 900 + (int)(i & 31U);
		CHECK(XSendEvent(display, window, False, StructureNotifyMask,
		    &configure) != 0);
	}
	{
		XEvent configure = {0};

		configure.xconfigure.type = ConfigureNotify;
		configure.xconfigure.display = display;
		configure.xconfigure.event = window;
		configure.xconfigure.window = window;
		configure.xconfigure.width = 85;
		configure.xconfigure.height = 64;
		CHECK(XSendEvent(display, window, False, StructureNotifyMask,
		    &configure) != 0);
	}
	CHECK(send_key(display, window, XK_F1, true) == 0);
	CHECK(send_key(display, window, XK_F1, false) == 0);
	(void)XFlush(display);
	CHECK(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
	deadline.tv_sec += 2;
	marker = false;
	while (!marker
	    && st_console_event_timedwait(console, &event, &deadline) == 0) {
		if (event.type == ST_CONSOLE_EVENT_KEY_CODE
		    && event.data.key.code == ST_CONSOLE_KEY_F1)
			marker = true;
	}
	CHECK(marker);
	CHECK(st_console_events_clear(console) == 0);
	(void)XCloseDisplay(display);
	return 0;
}

int
main(void)
{
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_X11,
	};
	const struct st_console_config config = {
		.columns = 8,
		.rows = 4,
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
	char *clipboard = NULL;
	uint32_t width;
	uint32_t height;
	int status;

	(void)setlocale(LC_CTYPE, "");
	CHECK(check_keys() == 0);
	status = st_console_create(&first, &config_display, &config);
	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED)
		return 77;
	CHECK(status == 0);
	scaled_display.initial_scaling = 2.0;
	CHECK(st_console_create(&second, &scaled_display, &scaled) == 0);
	CHECK(st_console_set_title(first, "st_console X11 test one") == 0);
	CHECK(st_console_set_title(second, "st_console X11 test two") == 0);
	CHECK(check_window_size("st_console X11 test two", 171, 128,
	    85, 64) == 0);
	CHECK(st_console_set_pointer_visible(first, false) == 0);
	CHECK(st_console_set_pointer_visible(first, true) == 0);
	CHECK(st_console_set_pixel(first, 4, 5, UINT32_C(0x102030)) == 0);
	CHECK(st_console_present(first) == 0);
	CHECK(check_escape_event(first) == 0);
	CHECK(st_console_resize(first, 10, 5) == 0);
	CHECK(st_console_pixel_size(first, &width, &height) == 0);
	CHECK(width == 80 && height == 80);
	CHECK(check_window_size("st_console X11 input test", 107, 80,
	    107, 80) == 0);
	CHECK(st_console_set_aspect_ratio(first, 1, 1) == 0);
	CHECK(st_console_set_aspect_ratio(first, 4, 3) == 0);
	CHECK(check_window_size("st_console X11 input test", 107, 80,
	    107, 80) == 0);
	CHECK(st_console_resize(second, 10, 5) == 0);
	CHECK(check_window_size("st_console X11 test two", 213, 160,
	    107, 80) == 0);

	CHECK(st_console_set_clipboard_text(first, "first: \xc2\xa3") == 0);
	CHECK(st_console_get_clipboard_text(first, &clipboard) == 0);
	CHECK(strcmp(clipboard, "first: \xc2\xa3") == 0);
	st_console_free_text(clipboard);
	clipboard = NULL;
	CHECK(st_console_set_clipboard_text(second, "second: \xf0\x9f\x99\x82")
	    == 0);
	CHECK(st_console_get_clipboard_text(first, &clipboard) == 0);
	CHECK(strcmp(clipboard, "second: \xf0\x9f\x99\x82") == 0);
	st_console_free_text(clipboard);

	st_console_destroy(second);
	st_console_destroy(first);
	{
		const struct st_console_display_config display_config = {
			.backend = ST_CONSOLE_BACKEND_X11,
		};
		st_console_display_t *display = NULL;
		st_console_t *active = NULL;
		struct st_console_config smaller = config;

		scaled = config;
		scaled_display.initial_scaling = 2.0;
		smaller.columns /= 2;
		smaller.rows /= 2;

		CHECK(st_console_display_create(&display, &display_config) == 0);
		CHECK(st_console_create_on_display(&first, display, &config) == 0);
		{
			struct st_console_config invalid = config;
			st_console_t *unchanged = first;

			/* Valid retained geometry, but not representable in XSizeHints. */
			invalid.aspect_width = UINT32_MAX;
			invalid.aspect_height = UINT32_MAX;
			CHECK(st_console_create_on_display(&unchanged, display, &invalid)
			    == IO_ERROR_OUT_OF_RANGE);
			CHECK(unchanged == first);
			CHECK(st_console_display_active(display, &active) == 0);
			CHECK(active == first);
		}
		{
			struct st_console_config external = config;
			st_console_t *separate = NULL;
			st_console_t *attached = NULL;
			int separate_status;

			external.scaling_mode = ST_CONSOLE_SCALING_EXTERNAL;
			separate_status = st_console_create(&separate, &display_config,
			    &external);
			CHECK(separate_status == 0);
			CHECK(st_console_create_on_display(&attached, display, &external)
			    == separate_status);
			CHECK(st_console_display_activate(display, attached) == 0);
			CHECK(st_console_display_activate(display, first) == 0);
			st_console_destroy(attached);
			st_console_destroy(separate);
		}
		for (unsigned int i = 0; i < 4; ++i) {
			CHECK(st_console_create_on_display(&second, display, &config) == 0);
			CHECK(st_console_display_activate(display, second) == 0);
			CHECK(check_escape_event(second) == 0);
			st_console_destroy(second);
			CHECK(st_console_display_active(display, &active) == 0);
			CHECK(active == first);
			CHECK(st_console_present(first) == 0);
		}
		st_console_destroy(first);
		st_console_display_destroy(display);
		CHECK(st_console_display_create(&display, &scaled_display) == 0);
		CHECK(st_console_create_on_display(&first, display, &scaled) == 0);
		CHECK(st_console_set_title(first, "st_console X11 large") == 0);
		CHECK(st_console_create_on_display(&second, display, &smaller) == 0);
		CHECK(st_console_set_title(second, "st_console X11 small") == 0);
		for (unsigned int i = 0; i < 4; ++i) {
			CHECK(st_console_display_activate(display, second) == 0);
			CHECK(check_window_size("st_console X11 small", 85, 64,
			    43, 32) == 0);
			CHECK(st_console_display_activate(display, first) == 0);
			CHECK(check_window_size("st_console X11 large", 171, 128,
			    85, 64) == 0);
		}
		/* No host-size query or delay between these switches. */
		for (unsigned int i = 0; i < 20; ++i) {
			CHECK(st_console_display_activate(display, second) == 0);
			CHECK(st_console_display_activate(display, first) == 0);
		}
		CHECK(check_window_size("st_console X11 large", 171, 128,
		    85, 64) == 0);
		st_console_destroy(second);
		st_console_destroy(first);
		st_console_display_destroy(display);
	}
	return 0;
}
