/* Wayland backend failure-path tests which do not require a compositor. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stddef.h>
#include <sys/mman.h>

static size_t unmapped_size;
static unsigned int unmap_count;
static int test_munmap(void *, size_t);

#define munmap test_munmap
#define st_console_wayland_init st_console_wayland_test_unused_init
#include "../console_wayland.c"
#undef st_console_wayland_init
#undef munmap

#include <signal.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <sys/wait.h>

#if ST_CONSOLE_HAVE_WAYLAND_DECORATION
#include "xdg-decoration-unstable-v1-client.h"
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

enum marshal_mode {
	MARSHAL_POOL_FAIL,
	MARSHAL_BUFFER_FAIL,
	MARSHAL_LISTENER_FAIL,
};

static enum marshal_mode marshal_mode;
static unsigned int marshal_count;
static int display_descriptor;
static int pending_result;
static int flush_result;
static int flush_error;
static int dispatch_result;

static int
test_munmap(void *address, size_t size)
{
	++unmap_count;
	unmapped_size = size;
	return munmap(address, size);
}

static struct wl_proxy *
fake_marshal(struct wl_proxy *proxy, uint32_t opcode,
    const struct wl_interface *interface, uint32_t version, uint32_t flags,
    ...)
{
	unsigned int call = ++marshal_count;

	(void)proxy;
	(void)opcode;
	(void)interface;
	(void)version;
	(void)flags;
	if (marshal_mode == MARSHAL_POOL_FAIL)
		return NULL;
	if (marshal_mode == MARSHAL_BUFFER_FAIL && call == 2)
		return NULL;
	return (struct wl_proxy *)(uintptr_t)1;
}

static int
fake_add_listener(struct wl_proxy *proxy, void (**listener)(void), void *data)
{
	(void)proxy;
	(void)listener;
	(void)data;
	return marshal_mode == MARSHAL_LISTENER_FAIL ? -1 : 0;
}

static uint32_t
fake_get_version(struct wl_proxy *proxy)
{
	(void)proxy;
	return 1;
}

static int
fake_get_fd(struct wl_display *display)
{
	(void)display;
	return display_descriptor;
}

static int
fake_dispatch_pending(struct wl_display *display)
{
	(void)display;
	return pending_result;
}

static int
fake_flush(struct wl_display *display)
{
	(void)display;
	if (flush_result < 0)
		errno = flush_error;
	return flush_result;
}

static int
fake_dispatch(struct wl_display *display)
{
	(void)display;
	return dispatch_result;
}

static void
install_buffer_fakes(struct st_wayland_api *saved, enum marshal_mode mode)
{
	*saved = st_wayland_api;
	memset(&st_wayland_api, 0, sizeof(st_wayland_api));
	marshal_mode = mode;
	marshal_count = 0;
	st_wayland_api.proxy_marshal_flags = fake_marshal;
	st_wayland_api.proxy_add_listener = fake_add_listener;
	st_wayland_api.proxy_get_version = fake_get_version;
}

static int
test_mapping_cleanup(void)
{
	static const enum marshal_mode modes[] = {
		MARSHAL_POOL_FAIL,
		MARSHAL_BUFFER_FAIL,
		MARSHAL_LISTENER_FAIL,
	};

	for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
		struct st_wayland_api saved;
		struct state state = {.shm = (struct wl_shm *)(uintptr_t)1};
		struct buffer buffer = {.state = &state};

		install_buffer_fakes(&saved, modes[i]);
		unmap_count = 0;
		unmapped_size = 0;
		CHECK(buffer_create(&state, &buffer, 8, 4,
		    WL_SHM_FORMAT_XRGB8888) == IO_ERROR_IO);
		CHECK(unmap_count == 1);
		CHECK(unmapped_size == 8U * 4U * sizeof(uint32_t));
		CHECK(buffer.pixels == NULL);
		CHECK(buffer.size == 0);
		CHECK(buffer.state == &state);
		st_wayland_api = saved;
	}
	return 0;
}

static int
test_busy_buffers_skip(void)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 2,
		.rows = 2,
		.cell_width = 1,
		.cell_height = 1,
		.scaling_mode = ST_CONSOLE_SCALING_INTERNAL,
		.wrap = true,
	};
	struct st_wayland_api saved;
	struct state state = {0};
	struct buffer *result = (struct buffer *)(uintptr_t)1;
	st_console_t *console = NULL;

	state.buffers[0].busy = true;
	state.buffers[1].busy = true;
	state.window_width = 2;
	state.window_height = 2;
	CHECK(get_buffer(&state, 8, 16, &result) == IO_ERROR_WOULD_BLOCK);
	CHECK(result == NULL);
	CHECK(st_console_create(&console, &display_config, &console_config) == 0);
	CHECK(st_console_bitmap_presenter_create(&state.presenter, console) == 0);
	install_buffer_fakes(&saved, MARSHAL_LISTENER_FAIL);
	state.video_surface = (struct wl_surface *)(uintptr_t)1;
	CHECK(draw(&state, console, true) == 0);
	st_wayland_api = saved;
	st_console_bitmap_presenter_destroy(state.presenter);
	st_console_destroy(console);
	return 0;
}

static int
prepare_worker_state(struct state *state, int display_pair[2])
{
	memset(state, 0, sizeof(*state));
	state->display = (struct wl_display *)(uintptr_t)1;
	state->wake[0] = state->wake[1] = -1;
	if (pipe(state->wake) != 0)
		return -1;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, display_pair) != 0) {
		(void)close(state->wake[0]);
		(void)close(state->wake[1]);
		state->wake[0] = state->wake[1] = -1;
		return -1;
	}
	display_descriptor = display_pair[0];
	pending_result = 0;
	flush_result = 0;
	flush_error = 0;
	dispatch_result = 0;
	memset(&st_wayland_api, 0, sizeof(st_wayland_api));
	st_wayland_api.display_get_fd = fake_get_fd;
	st_wayland_api.display_dispatch_pending = fake_dispatch_pending;
	st_wayland_api.display_flush = fake_flush;
	st_wayland_api.display_dispatch = fake_dispatch;
	return 0;
}

static void
close_worker_state(struct state *state, int display_pair[2])
{
	if (state->wake[0] >= 0) {
		(void)close(state->wake[0]);
		(void)close(state->wake[1]);
	}
	if (display_pair[0] >= 0)
		(void)close(display_pair[0]);
	if (display_pair[1] >= 0)
		(void)close(display_pair[1]);
}

static int
test_worker_failures(void)
{
	struct st_wayland_api saved = st_wayland_api;
	struct state state;
	int display_pair[2] = {-1, -1};
	char byte = 0;

	CHECK(prepare_worker_state(&state, display_pair) == 0);
	pending_result = -1;
	CHECK(worker_loop(&state) == IO_ERROR_IO);
	close_worker_state(&state, display_pair);

	display_pair[0] = display_pair[1] = -1;
	CHECK(prepare_worker_state(&state, display_pair) == 0);
	flush_result = -1;
	flush_error = EPIPE;
	CHECK(worker_loop(&state) == IO_ERROR_IO);
	close_worker_state(&state, display_pair);

	display_pair[0] = display_pair[1] = -1;
	CHECK(prepare_worker_state(&state, display_pair) == 0);
	CHECK(write(display_pair[1], &byte, 1) == 1);
	dispatch_result = -1;
	CHECK(worker_loop(&state) == IO_ERROR_IO);
	close_worker_state(&state, display_pair);

	display_pair[0] = display_pair[1] = -1;
	CHECK(prepare_worker_state(&state, display_pair) == 0);
	(void)close(display_pair[1]);
	display_pair[1] = -1;
	CHECK(worker_loop(&state) == IO_ERROR_IO);
	close_worker_state(&state, display_pair);

	display_pair[0] = display_pair[1] = -1;
	CHECK(prepare_worker_state(&state, display_pair) == 0);
	(void)close(display_pair[0]);
	display_pair[0] = -1;
	CHECK(worker_loop(&state) == IO_ERROR_IO);
	close_worker_state(&state, display_pair);

	CHECK(!terminal_poll_event(0));
	CHECK(!terminal_poll_event(POLLIN));
	CHECK(terminal_poll_event(POLLHUP));
	CHECK(terminal_poll_event(POLLERR));
	CHECK(terminal_poll_event(POLLNVAL));
	st_wayland_api = saved;
	return 0;
}

struct reader_state {
	int descriptor;
	char *text;
	size_t capacity;
	size_t used;
};

static int
read_clipboard(void *argument)
{
	struct reader_state *reader = argument;

	while (reader->used < reader->capacity) {
		ssize_t amount = read(reader->descriptor,
		    reader->text + reader->used, reader->capacity - reader->used);

		if (amount > 0)
			reader->used += (size_t)amount;
		else if (amount == 0)
			break;
		else if (errno != EINTR)
			break;
	}
	(void)close(reader->descriptor);
	return 0;
}

static int
test_pointer_surface_coordinates(void)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	st_console_display_t *display = NULL;
	st_console_t *console = NULL;
	struct st_console_event event;
	struct state state = {
		.surface = (struct wl_surface *)(uintptr_t)1,
		.video_surface = (struct wl_surface *)(uintptr_t)2,
		.frame = {
			.destination = {.x = 20, .y = 30, .width = 1280, .height = 800},
			.source_width = 640, .source_height = 400,
			.cell_width = 8, .cell_height = 16,
		},
	};
	CHECK(st_console_display_create(&display, &display_config) == 0);
	CHECK(st_console_create_on_display(&console, display, &geometry) == 0);
	state.console_display = display;
	/* Entry can be followed by a click without any motion event. */
	pointer_enter(&state, NULL, 1, state.video_surface,
	    wl_fixed_from_int(160), wl_fixed_from_int(64));
	pointer_button(&state, NULL, 2, 0, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
	CHECK(st_console_event_try(console, &event) == 0);
	CHECK(event.type == ST_CONSOLE_EVENT_MOUSE && event.data.mouse.x == 10
	    && event.data.mouse.y == 2 && event.data.mouse.pixel_x == 80
	    && event.data.mouse.pixel_y == 32);
	pointer_motion(&state, NULL, 0, wl_fixed_from_int(176), wl_fixed_from_int(96));
	CHECK(st_console_event_try(console, &event) == 0);
	CHECK(event.data.mouse.x == 11 && event.data.mouse.y == 3);
	pointer_button(&state, NULL, 3, 0, 0x110, WL_POINTER_BUTTON_STATE_RELEASED);
	CHECK(st_console_event_try(console, &event) == 0);
	CHECK(event.data.mouse.x == 11 && event.data.mouse.y == 3 && event.data.mouse.buttons == 0);
	pointer_axis(&state, NULL, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(1));
	CHECK(st_console_event_try(console, &event) == 0);
	CHECK(event.data.mouse.x == 11 && event.data.mouse.y == 3);
	pointer_leave(&state, NULL, 4, state.video_surface);
	/* Background coordinates already include the video offset. */
	pointer_enter(&state, NULL, 5, state.surface, wl_fixed_from_int(180), wl_fixed_from_int(94));
	pointer_button(&state, NULL, 6, 0, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
	CHECK(st_console_event_try(console, &event) == 0);
	CHECK(event.data.mouse.x == 10 && event.data.mouse.y == 2);
	st_console_destroy(console);
	st_console_display_destroy(display);
	return 0;
}

static int
test_clipboard_send(void)
{
	pid_t child = fork();
	int status;

	CHECK(child >= 0);
	if (child == 0) {
		char text[] = "clipboard";
		struct state state = {.clipboard = text};
		int descriptors[2];

		if (signal(SIGPIPE, SIG_DFL) == SIG_ERR || pipe(descriptors) != 0)
			_exit(2);
		(void)close(descriptors[0]);
		source_send(
		    &state, NULL, "text/plain;charset=utf-8", descriptors[1]);
		_exit(0);
	}
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	{
		const size_t length = 1024U * 1024U;
		char *source = malloc(length + 1);
		char *destination = malloc(length);
		struct state state = {.clipboard = source};
		struct reader_state reader;
		st_thread_t thread;
		int descriptors[2];

		CHECK(source != NULL && destination != NULL);
		for (size_t i = 0; i < length; ++i)
			source[i] = (char)('A' + i % 23);
		source[length] = '\0';
		CHECK(pipe(descriptors) == 0);
		reader = (struct reader_state){
		    .descriptor = descriptors[0],
		    .text = destination,
		    .capacity = length,
		};
		CHECK(st_thread_create(&thread, read_clipboard, &reader)
		    == ST_THREAD_SUCCESS);
		source_send(
		    &state, NULL, "text/plain;charset=utf-8", descriptors[1]);
		CHECK(st_thread_join(thread, NULL) == ST_THREAD_SUCCESS);
		CHECK(reader.used == length);
		CHECK(memcmp(source, destination, length) == 0);
		free(destination);
		free(source);
	}
	return 0;
}

static struct {
	unsigned int sources;
	unsigned int regular_sets, utf8, plain;
	uint32_t regular_serial;
	bool invalid;
} clipboard_calls;

static void
clipboard_destroy(struct wl_proxy *proxy)
{
	(void)proxy;
}

static int
clipboard_listener(struct wl_proxy *proxy, void (**listener)(void), void *data)
{
	(void)proxy;
	(void)listener;
	(void)data;
	return 0;
}

static struct wl_proxy *
clipboard_marshal(struct wl_proxy *proxy, uint32_t opcode,
    const struct wl_interface *interface, uint32_t version, uint32_t flags, ...)
{
	va_list args;
	uintptr_t object = (uintptr_t)proxy;
	(void)version;
	if (interface != NULL) {
		if (interface == &wl_data_source_interface)
			return (struct wl_proxy *)(uintptr_t)(100 + ++clipboard_calls.sources);
		clipboard_calls.invalid = true;
		return NULL;
	}
	if ((flags & WL_MARSHAL_FLAG_DESTROY) != 0) {
		clipboard_destroy(proxy);
		return NULL;
	}
	va_start(args, flags);
	if (object >= 100 && opcode == WL_DATA_SOURCE_OFFER) {
		const char *mime = va_arg(args, const char *);
		if (strcmp(mime, "text/plain;charset=utf-8") == 0)
			++clipboard_calls.utf8;
		else if (strcmp(mime, "text/plain") == 0)
			++clipboard_calls.plain;
		else
			clipboard_calls.invalid = true;
	} else if (object == 2 && opcode == WL_DATA_DEVICE_SET_SELECTION) {
		(void)va_arg(args, struct wl_data_source *);
		clipboard_calls.regular_serial = va_arg(args, uint32_t);
		++clipboard_calls.regular_sets;
	} else if (object == 10) {
		const char *mime = va_arg(args, const char *);
		int fd = va_arg(args, int);
		const char *text = "clipboard peer";
		if (strcmp(mime, "text/plain;charset=utf-8") != 0
		    || write(fd, text, strlen(text)) != (ssize_t)strlen(text))
			clipboard_calls.invalid = true;
	} else {
		clipboard_calls.invalid = true;
	}
	va_end(args);
	return NULL;
}

static int
test_clipboard_protocol(void)
{
	struct st_wayland_api saved = st_wayland_api;
	struct state state = {
		.data_manager = (struct wl_data_device_manager *)(uintptr_t)1,
		.data_device = (struct wl_data_device *)(uintptr_t)2,
	};
	char *result = NULL;
	int display_pair[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, display_pair) == 0);
	display_descriptor = display_pair[0];
	memset(&clipboard_calls, 0, sizeof(clipboard_calls));
	st_wayland_api.proxy_marshal_flags = clipboard_marshal;
	st_wayland_api.proxy_add_listener = clipboard_listener;
	st_wayland_api.proxy_get_version = fake_get_version;
	st_wayland_api.proxy_destroy = clipboard_destroy;
	st_wayland_api.display_get_fd = fake_get_fd;
	st_wayland_api.display_flush = fake_flush;
	flush_result = 0;
	CHECK(set_clipboard(&state, "no input") == IO_ERROR_NOT_SUPPORTED);
	CHECK(clipboard_calls.regular_sets == 0);
	pointer_button(&state, NULL, 51, 0, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
	pointer_button(&state, NULL, 52, 0, 0x110, WL_POINTER_BUTTON_STATE_RELEASED);
	CHECK(set_clipboard(&state, "Caf\xc3\xa9") == 0);
	CHECK(clipboard_calls.regular_sets == 1);
	CHECK(clipboard_calls.utf8 == 1 && clipboard_calls.plain == 1);
	CHECK(clipboard_calls.regular_serial == 52);
	CHECK(get_clipboard(&state, &result) == 0 && strcmp(result, "Caf\xc3\xa9") == 0);
	free(result);
	source_cancelled(&state, state.selection_source);
	CHECK(state.selection_source == NULL);
	device_offer(&state, NULL, (struct wl_data_offer *)(uintptr_t)10);
	offer_mime(state.pending_offers, state.pending_offers->object, "text/plain;charset=utf-8");
	device_selection(&state, NULL, state.pending_offers->object);
	CHECK(get_clipboard(&state, &result) == 0 && strcmp(result, "clipboard peer") == 0);
	free(result);
	objects_destroy(&state);
	CHECK(!clipboard_calls.invalid);
	CHECK(close(display_pair[0]) == 0 && close(display_pair[1]) == 0);
	st_wayland_api = saved;
	return 0;
}

static int
test_worker_completion(void)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config console_config = {
		.columns = 2,
		.rows = 2,
		.wrap = true,
	};
	struct st_console_event event;
	st_console_t *console = NULL;
	struct command command = {0};
	struct command later = {0};
	struct state state = {.command = &command};

	CHECK(st_console_create(&console, &display_config, &console_config) == 0);
	state.console_display = console->display;
	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.changed) == ST_THREAD_SUCCESS);
	worker_finish(&state, IO_ERROR_IO);
	CHECK(state.failed);
	CHECK(state.close_posted);
	CHECK(state.command == NULL);
	CHECK(command.done && command.result == IO_ERROR_IO);
	worker_finish(&state, IO_ERROR_IO);
	CHECK(st_console_event_try(console, &event) == 0);
	CHECK(event.type == ST_CONSOLE_EVENT_CLOSE);
	CHECK(st_console_event_try(console, &event) == IO_ERROR_WOULD_BLOCK);
	CHECK(submit(&state, &later) == IO_ERROR_IO);
	st_condition_destroy(&state.changed);
	st_mutex_destroy(&state.mutex);
	st_console_destroy(console);

	memset(&state, 0, sizeof(state));
	state.stop = true;
	CHECK(st_mutex_init(&state.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&state.changed) == ST_THREAD_SUCCESS);
	worker_finish(&state, 0);
	CHECK(!state.close_posted);
	st_condition_destroy(&state.changed);
	st_mutex_destroy(&state.mutex);
	return 0;
}

static int
test_scaling_command(void)
{
	double scaling = 17.0;
	struct state state = {
		.window_geometry = {640, 400, 4, 3, false},
		.window_width = 1280,
		.window_height = 960,
	};
	struct command command = {
		.type = CMD_GET_SCALING,
		.data.scaling_result = &scaling,
	};

	CHECK(process(&state, &command) == 0);
	CHECK(scaling == 2.0);
	return 0;
}

static int
test_pointer_shapes(void)
{
	uint32_t cursors[ST_CONSOLE_POINTER_LINK + 1][16 * 16];

	for (size_t shape = ST_CONSOLE_POINTER_DEFAULT;
	    shape <= ST_CONSOLE_POINTER_LINK; ++shape) {
		draw_cursor(cursors[shape], (enum st_console_pointer_shape)shape);
		bool visible = false;

		for (size_t pixel = 0; pixel < 16 * 16; ++pixel)
			visible = visible || cursors[shape][pixel] != 0;
		CHECK(visible);
	}
	CHECK(memcmp(cursors[ST_CONSOLE_POINTER_DEFAULT],
	    cursors[ST_CONSOLE_POINTER_TEXT], sizeof(cursors[0])) != 0);
	CHECK(memcmp(cursors[ST_CONSOLE_POINTER_TEXT],
	    cursors[ST_CONSOLE_POINTER_LINK], sizeof(cursors[0])) != 0);
	return 0;
}

#if ST_CONSOLE_HAVE_WAYLAND_DECORATION
static struct {
	unsigned int binds, creates, listeners, modes, destroys;
	uint32_t bind_version, mode;
	bool fail_create, fail_listener, invalid;
	struct wl_proxy *destroyed[3];
	const struct zxdg_toplevel_decoration_v1_listener *listener;
	void *listener_data;
} decoration_calls;

static struct wl_proxy *
decoration_marshal(struct wl_proxy *proxy, uint32_t opcode,
    const struct wl_interface *interface, uint32_t version, uint32_t flags,
    ...)
{
	va_list args;
	struct wl_proxy *result = NULL;

	va_start(args, flags);
	if (flags == WL_MARSHAL_FLAG_DESTROY) {
		if (decoration_calls.destroys < 3)
			decoration_calls.destroyed[decoration_calls.destroys++] = proxy;
		else
			decoration_calls.invalid = true;
		if (opcode != 0 || interface != NULL)
			decoration_calls.invalid = true;
	}
	else if (proxy == (struct wl_proxy *)(uintptr_t)1 && opcode == 0
	    && interface == &zxdg_decoration_manager_v1_interface) {
		++decoration_calls.binds;
		decoration_calls.bind_version = version;
		if (va_arg(args, uint32_t) != 42
		    || strcmp(va_arg(args, const char *),
		    "zxdg_decoration_manager_v1") != 0
		    || va_arg(args, uint32_t) != 1
		    || va_arg(args, void *) != NULL)
			decoration_calls.invalid = true;
		result = (struct wl_proxy *)(uintptr_t)2;
	}
	else if (proxy == (struct wl_proxy *)(uintptr_t)2 && opcode == 1
	    && interface == &zxdg_toplevel_decoration_v1_interface) {
		++decoration_calls.creates;
		if (va_arg(args, void *) != NULL
		    || va_arg(args, struct xdg_toplevel *)
		    != (struct xdg_toplevel *)(uintptr_t)3)
			decoration_calls.invalid = true;
		if (!decoration_calls.fail_create)
			result = (struct wl_proxy *)(uintptr_t)4;
	}
	else if (proxy == (struct wl_proxy *)(uintptr_t)4 && opcode == 1
	    && interface == NULL) {
		++decoration_calls.modes;
		decoration_calls.mode = va_arg(args, uint32_t);
	}
	else
		decoration_calls.invalid = true;
	va_end(args);
	return result;
}

static int
decoration_add_listener(struct wl_proxy *proxy, void (**listener)(void),
    void *data)
{
	++decoration_calls.listeners;
	if (proxy != (struct wl_proxy *)(uintptr_t)4)
		decoration_calls.invalid = true;
	decoration_calls.listener =
	    (const struct zxdg_toplevel_decoration_v1_listener *)listener;
	decoration_calls.listener_data = data;
	return decoration_calls.fail_listener ? -1 : 0;
}

static int
test_decoration_binding(void)
{
	struct st_wayland_api saved = st_wayland_api;
	const uint32_t versions[] = {1, 7};

	st_wayland_api.proxy_marshal_flags = decoration_marshal;
	st_wayland_api.proxy_get_version = fake_get_version;
	for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
		struct state state = {0};

		memset(&decoration_calls, 0, sizeof(decoration_calls));
		registry_global(&state, (struct wl_registry *)(uintptr_t)1, 42,
		    "zxdg_decoration_manager_v1", versions[i]);
		CHECK(decoration_calls.binds == 1);
		CHECK(decoration_calls.bind_version == 1);
		CHECK(!decoration_calls.invalid);
		CHECK(state.decoration_manager
		    == (struct zxdg_decoration_manager_v1 *)(uintptr_t)2);
	}
	st_wayland_api = saved;
	return 0;
}

static int
test_decoration_lifecycle(void)
{
	struct st_wayland_api saved = st_wayland_api;

	st_wayland_api.proxy_marshal_flags = decoration_marshal;
	st_wayland_api.proxy_get_version = fake_get_version;
	st_wayland_api.proxy_add_listener = decoration_add_listener;
	for (unsigned int failure = 0; failure < 3; ++failure) {
		struct state state = {
			.decoration_manager =
			    (struct zxdg_decoration_manager_v1 *)(uintptr_t)2,
			.toplevel = (struct xdg_toplevel *)(uintptr_t)3,
		};

		memset(&decoration_calls, 0, sizeof(decoration_calls));
		decoration_calls.fail_create = failure == 1;
		decoration_calls.fail_listener = failure == 2;
		CHECK(setup_decoration(&state) == (failure == 0 ? 0 : IO_ERROR_IO));
		CHECK(decoration_calls.creates == 1);
		CHECK(decoration_calls.listeners == (failure == 1 ? 0U : 1U));
		CHECK(decoration_calls.modes == (failure == 0 ? 1U : 0U));
		if (failure == 0) {
			CHECK(decoration_calls.mode == 2); /* Protocol's server-side mode. */
			CHECK(decoration_calls.listener_data == &state);
			CHECK(decoration_calls.listener->configure != NULL);
			/* Both an accepted and a declined preference are valid. */
			decoration_calls.listener->configure(&state, state.decoration, 2);
			decoration_calls.listener->configure(&state, state.decoration, 1);
		}
		objects_destroy(&state);
		unsigned int offset = failure == 1 ? 0 : 1;

		CHECK(decoration_calls.destroys == offset + 2);
		if (offset != 0)
			CHECK(decoration_calls.destroyed[0]
			    == (struct wl_proxy *)(uintptr_t)4);
		CHECK(decoration_calls.destroyed[offset]
		    == (struct wl_proxy *)(uintptr_t)3);
		CHECK(decoration_calls.destroyed[offset + 1]
		    == (struct wl_proxy *)(uintptr_t)2);
		CHECK(!decoration_calls.invalid);
	}
	{
		struct state state = {.toplevel = (struct xdg_toplevel *)(uintptr_t)3};

		memset(&decoration_calls, 0, sizeof(decoration_calls));
		CHECK(setup_decoration(&state) == 0);
		CHECK(decoration_calls.creates == 0);
		objects_destroy(&state);
		CHECK(decoration_calls.destroys == 1);
		CHECK(decoration_calls.destroyed[0] == (struct wl_proxy *)(uintptr_t)3);
		CHECK(!decoration_calls.invalid);
	}
	st_wayland_api = saved;
	return 0;
}
#endif

int
main(void)
{
#if ST_CONSOLE_HAVE_WAYLAND_DECORATION
	CHECK(test_decoration_binding() == 0);
	CHECK(test_decoration_lifecycle() == 0);
#endif
	CHECK(test_mapping_cleanup() == 0);
	CHECK(test_busy_buffers_skip() == 0);
	CHECK(test_worker_failures() == 0);
	CHECK(test_pointer_surface_coordinates() == 0);
	CHECK(test_clipboard_send() == 0);
	CHECK(test_clipboard_protocol() == 0);
	CHECK(test_worker_completion() == 0);
	CHECK(test_scaling_command() == 0);
	CHECK(test_pointer_shapes() == 0);
	return 0;
}
