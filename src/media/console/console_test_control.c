/* Pipe transport for the ordinary application's optional testing driver. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "console_test_internal.h"
#include "io_error.h"
#include "st_time.h"
#include "st_utf8.h"
#include <stdlib.h>
#include <string.h>
#if ST_CONSOLE_TEST
#include <errno.h>
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define TEST_REQUEST_LIMIT (1024U * 1024U)
#define TEST_RESPONSE_LIMIT (128U * 1024U * 1024U)
#define TEST_VERSION 1U
struct test_bytes { uint8_t *data; size_t size, capacity; int error; };
struct test_control {
	st_mutex_t mutex; /* Request/reply mailbox and ready predicate. */
	st_condition_t ready;
	st_thread_t thread;
	atomic_bool stop, exited, disconnected;
	bool pending, reply_ready, close_posted;
	uint8_t *request;
	size_t request_size;
	struct test_bytes reply;
#ifdef _WIN32
	HANDLE input, output, native_thread;
#else
	int input, output;
#endif
};

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void bytes(struct test_bytes *b, const void *data, size_t size)
{
	if (b->error != 0) return;
	if (size > TEST_RESPONSE_LIMIT - b->size) { b->error = IO_ERROR_MESSAGE_TOO_LARGE; return; }
	if (b->size + size > b->capacity) {
		size_t capacity = b->capacity == 0 ? 1024 : b->capacity;
		while (capacity < b->size + size) capacity *= 2;
		uint8_t *next = realloc(b->data, capacity);
		if (next == NULL) { b->error = IO_ERROR_OUT_OF_MEMORY; return; }
		b->data = next; b->capacity = capacity;
	}
	if (size != 0) memcpy(b->data + b->size, data, size);
	b->size += size;
}
static void put32(struct test_bytes *b, uint32_t n)
{
	uint8_t encoded[4] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
	bytes(b, encoded, 4);
}
static void put64(struct test_bytes *b, uint64_t n) { put32(b, (uint32_t)n); put32(b, (uint32_t)(n >> 32)); }
static void string(struct test_bytes *b, const char *s) { size_t n = strlen(s); put32(b, (uint32_t)n); bytes(b, s, n); }
static void patch32(struct test_bytes *b, size_t offset, uint32_t n)
{
	if (b->error != 0) return;
	for (unsigned i = 0; i < 4; ++i) b->data[offset + i] = (uint8_t)(n >> (8 * i));
}
static size_t section(struct test_bytes *b, uint32_t tag) { put32(b, tag); size_t start = b->size; put32(b, 0); return start; }
static void end_section(struct test_bytes *b, size_t start) { patch32(b, start, (uint32_t)(b->size - start - 4)); }
static void words(struct test_bytes *b, uint32_t tag, const uint32_t *p, size_t count)
{
	size_t start = section(b, tag);
	for (size_t i = 0; i < count; ++i) put32(b, p[i]);
	end_section(b, start);
}
static void fields(struct test_bytes *b, uint32_t tag, const struct st_console_test_application *app)
{
	size_t start = section(b, tag);
	put32(b, (uint32_t)app->count);
	for (size_t i = 0; i < app->count; ++i) {
		const struct st_console_test_field *f = &app->fields[i];
		string(b, f->name); put32(b, f->text != NULL);
		if (f->text != NULL) string(b, f->text); else put64(b, (uint64_t)f->number);
	}
	end_section(b, start);
}
static void encode_capture(struct test_bytes *b, const struct st_console_test_capture *c)
{
	struct st_console_test_application metadata = {0};
#define N(name, value) st_console_test_number(&metadata, name, (int64_t)(value))
	N("id", c->id); N("console_id", c->console_id); N("presentation", c->presentation);
	N("events_dequeued", c->events_dequeued);
	N("generation", c->generation); N("timestamp", c->timestamp);
	N("columns", c->columns); N("rows", c->rows); N("cell_width", c->cell_width); N("cell_height", c->cell_height);
	N("pixel_top", c->pixel_top); N("plane_width", c->planes->width); N("plane_height", c->planes->height);
	N("output_width", c->output_width); N("output_height", c->output_height);
	N("destination_x", c->destination.x); N("destination_y", c->destination.y);
	N("destination_width", c->destination.width); N("destination_height", c->destination.height);
	N("cursor_x", c->cursor_x); N("cursor_y", c->cursor_y); N("cursor_visible", c->cursor_visible);
	N("cursor_custom", c->cursor_custom);
	N("cursor_shape", c->cursor_shape); N("cursor_start", c->cursor.start); N("cursor_end", c->cursor.end);
	N("pixel_cursor_enabled", c->pixel_cursor.enabled); N("pixel_cursor_visible", c->pixel_cursor.visible);
	N("pixel_cursor_x", c->pixel_cursor.rectangle.x); N("pixel_cursor_y", c->pixel_cursor.rectangle.y);
	N("pixel_cursor_width", c->pixel_cursor.rectangle.width); N("pixel_cursor_height", c->pixel_cursor.rectangle.height);
	N("pixel_cursor_colour", c->pixel_cursor.colour);
	N("cursor_range", c->cursor.range); N("cursor_blink", c->cursor.blink);
	N("blink_mode", c->blink_mode); N("video_flags", c->video_flags); N("fullscreen", c->fullscreen);
	N("pointer_visible", c->pointer_visible); N("pointer_shape", c->pointer_shape);
	N("scaling_millionths", c->scaling * 1000000.0); N("url_serial", c->url_serial);
#undef N
	fields(b, 1, &metadata);
	size_t start = section(b, 2);
	size_t cells = (size_t)c->columns * c->rows;
	for (size_t i = 0; i < cells; ++i) {
		const struct st_console_cell *cell = &c->cells[i];
		put32(b, cell->glyph); put32(b, cell->foreground); put32(b, cell->background); put32(b, cell->flags);
		put32(b, (uint32_t)cell->font | (uint32_t)cell->glyph_font << 16);
		put32(b, (uint32_t)cell->hyperlink | (uint32_t)cell->reserved << 16);
	}
	end_section(b, start);
	words(b, 3, c->palette, UINT16_MAX + 1U);
	size_t pixels = (size_t)c->planes->width * c->planes->height;
	words(b, 4, c->planes->primary, pixels); words(b, 5, c->planes->alternate, pixels);
	words(b, 6, c->rgb->pixels, (size_t)c->rgb->width * c->rgb->height);
	start = section(b, 7); put32(b, (uint32_t)c->glyph_count);
	for (size_t i = 0; i < c->glyph_count; ++i) {
		put32(b, c->glyphs[i].font); put32(b, c->glyphs[i].glyph);
		put32(b, (uint32_t)c->glyphs[i].text_size); bytes(b, c->glyphs[i].text, c->glyphs[i].text_size);
	}
	end_section(b, start);
	fields(b, 8, &c->application);
	start = section(b, 9); string(b, c->title); string(b, c->clipboard); string(b, c->url); end_section(b, start);
}

static void pause_io(void)
{
	const struct timespec interval = {0, 5000000};
	(void)st_thread_sleep(&interval, NULL);
}
/* Only the transport thread touches its pipe handles. The owner only accesses
 * the bounded mailbox. POSIX nonblocking I/O and Windows cancellation keep
 * joining independent of a controller which stops reading or writing. */
static bool transfer_bytes(struct test_control *c, void *buffer, size_t size, bool writing)
{
	uint8_t *p = buffer;
	while (size != 0 && !atomic_load_explicit(&c->stop, memory_order_acquire)) {
#ifdef _WIN32
		DWORD count = 0, available = 0;
		if (!writing) {
			if (!PeekNamedPipe(c->input, NULL, 0, NULL, &available, NULL)) return false;
			if (available == 0) { pause_io(); continue; }
		}
		DWORD chunk = (DWORD)(size > 65536 ? 65536 : size);
		if (!writing && chunk > available) chunk = available;
		BOOL ok = writing ? WriteFile(c->output, p, chunk, &count, NULL)
		    : ReadFile(c->input, p, chunk, &count, NULL);
		if (!ok || count == 0) return false;
#else
		struct pollfd pollfd = {writing ? c->output : c->input, writing ? POLLOUT : POLLIN, 0};
		int ready = poll(&pollfd, 1, 10);
		if (ready < 0 && errno != EINTR) return false;
		if (ready <= 0) continue;
		ssize_t count = writing ? write(c->output, p, size) : read(c->input, p, size);
		if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
		if (count <= 0) return false;
#endif
		p += count; size -= (size_t)count;
	}
	return size == 0;
}
static int control_worker(void *argument)
{
	struct test_control *c = argument;
#ifdef _WIN32
	HANDLE self = NULL;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
	    &self, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		atomic_store_explicit(&c->disconnected, true, memory_order_release);
		atomic_store_explicit(&c->exited, true, memory_order_release);
		return 1;
	}
	(void)st_mutex_lock(&c->mutex); c->native_thread = self; (void)st_mutex_unlock(&c->mutex);
#else
	/* Suppress thread-directed SIGPIPE on targets without descriptor control.
	 * This mask affects only this st_threads-created thread. */
	sigset_t blocked;
	sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &blocked, NULL) != 0) {
		atomic_store_explicit(&c->disconnected, true, memory_order_release);
		atomic_store_explicit(&c->exited, true, memory_order_release);
		return 1;
	}
#endif
	for (;;) {
		uint8_t prefix[4];
		if (!transfer_bytes(c, prefix, 4, false)) break;
		uint32_t size = get32(prefix);
		if (size < 16 || size > TEST_REQUEST_LIMIT) break;
		uint8_t *request = malloc(size);
		if (request == NULL) break;
		if (!transfer_bytes(c, request, size, false)) { free(request); break; }
		if (get32(request) != TEST_VERSION || get32(request + 12) != 0) { free(request); break; }
		(void)st_mutex_lock(&c->mutex);
		c->request = request; c->request_size = size; c->pending = true;
		while (!c->reply_ready && !atomic_load_explicit(&c->stop, memory_order_acquire))
			(void)st_condition_wait(&c->ready, &c->mutex);
		struct test_bytes reply = c->reply;
		c->reply = (struct test_bytes){0}; c->reply_ready = false;
		(void)st_mutex_unlock(&c->mutex);
		if (reply.data == NULL) break;
		bool sent = transfer_bytes(c, reply.data, reply.size, true);
		free(reply.data);
		if (!sent) break;
	}
	atomic_store_explicit(&c->disconnected, true, memory_order_release);
	atomic_store_explicit(&c->exited, true, memory_order_release);
	return 0;
}

static int initialize_transport(struct test_control *c)
{
#ifdef _WIN32
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE) return IO_ERROR_INVALID_ARGUMENT;
	if (!DuplicateHandle(GetCurrentProcess(), input, GetCurrentProcess(), &c->input, 0, FALSE, DUPLICATE_SAME_ACCESS)
	    || !DuplicateHandle(GetCurrentProcess(), output, GetCurrentProcess(), &c->output, 0, FALSE, DUPLICATE_SAME_ACCESS)) return IO_ERROR_IO;
	(void)SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
	(void)SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
	HANDLE diagnostic = NULL;
	if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(),
	    &diagnostic, 0, FALSE, DUPLICATE_SAME_ACCESS)) return IO_ERROR_IO;
	int descriptor = _open_osfhandle((intptr_t)diagnostic, _O_WRONLY | _O_BINARY);
	if (descriptor < 0) { CloseHandle(diagnostic); return IO_ERROR_IO; }
	/* GUI launchers can have no CRT stdout descriptor despite valid Win32
	 * inherited handles. Bind it explicitly before redirecting incidental output. */
	if (freopen("NUL", "wb", stdout) == NULL) { _close(descriptor); return IO_ERROR_IO; }
	int redirected = _dup2(descriptor, _fileno(stdout));
	_close(descriptor);
	if (redirected != 0 || !SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE))) return IO_ERROR_IO;
#else
	struct stat input, output;
	if (fstat(STDIN_FILENO, &input) != 0 || fstat(STDOUT_FILENO, &output) != 0
	    || !S_ISFIFO(input.st_mode) || !S_ISFIFO(output.st_mode)) return IO_ERROR_INVALID_ARGUMENT;
	c->input = dup(STDIN_FILENO); c->output = dup(STDOUT_FILENO);
	if (c->input < 0 || c->output < 0) return IO_ERROR_IO;
	if (fcntl(c->input, F_SETFD, FD_CLOEXEC) < 0 || fcntl(c->output, F_SETFD, FD_CLOEXEC) < 0
	    || fcntl(c->input, F_SETFL, O_NONBLOCK) < 0 || fcntl(c->output, F_SETFL, O_NONBLOCK) < 0) return IO_ERROR_IO;
#if ST_CONSOLE_TEST_HAVE_F_SETNOSIGPIPE
	/* Process-directed SIGPIPE can bypass the worker's mask on some targets. */
	if (fcntl(c->output, F_SETNOSIGPIPE, 1) < 0) return IO_ERROR_IO;
#endif
	/* stdout belongs exclusively to the protocol; incidental library output
	 * now goes to diagnostics. The duplicated transport descriptor stays open. */
	if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) return IO_ERROR_IO;
	(void)fcntl(STDIN_FILENO, F_SETFD, FD_CLOEXEC);
#endif
	return 0;
}
static void close_transport(struct test_control *c)
{
#ifdef _WIN32
	if (c->native_thread != NULL) CloseHandle(c->native_thread);
	if (c->input != NULL) CloseHandle(c->input);
	if (c->output != NULL) CloseHandle(c->output);
#else
	if (c->input >= 0) close(c->input);
	if (c->output >= 0) close(c->output);
#endif
}
int st_console_test_control_start(st_console_display_t *display)
{
	struct st_console_test_state *state = st_console_test_state(display);
	if (state == NULL) return IO_ERROR_NOT_SUPPORTED;
	if (state->control != NULL) return IO_ERROR_BUSY;
	struct test_control *c = calloc(1, sizeof(*c));
	if (c == NULL) return IO_ERROR_OUT_OF_MEMORY;
	atomic_init(&c->stop, false); atomic_init(&c->exited, false); atomic_init(&c->disconnected, false);
#ifndef _WIN32
	c->input = -1; c->output = -1;
#endif
	if (st_mutex_init(&c->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) { free(c); return IO_ERROR_IO; }
	if (st_condition_init(&c->ready) != ST_THREAD_SUCCESS) { st_mutex_destroy(&c->mutex); free(c); return IO_ERROR_IO; }
	int status = initialize_transport(c);
	if (status == 0 && st_thread_create(&c->thread, control_worker, c) != ST_THREAD_SUCCESS) status = IO_ERROR_IO;
	if (status != 0) { close_transport(c); st_condition_destroy(&c->ready); st_mutex_destroy(&c->mutex); free(c); return status; }
	state->control = c;
	return 0;
}
void st_console_test_control_destroy(st_console_display_t *display)
{
	struct st_console_test_state *state = st_console_test_state(display);
	struct test_control *c = state == NULL ? NULL : state->control;
	if (c == NULL) return;
	atomic_store_explicit(&c->stop, true, memory_order_release);
	(void)st_mutex_lock(&c->mutex); (void)st_condition_broadcast(&c->ready); (void)st_mutex_unlock(&c->mutex);
	while (!atomic_load_explicit(&c->exited, memory_order_acquire)) {
#ifdef _WIN32
		/* Repeat cancellation until exit: shutdown may race the worker's
		 * transition into a synchronous WriteFile on a full pipe. */
		(void)st_mutex_lock(&c->mutex);
		if (c->native_thread != NULL) (void)CancelSynchronousIo(c->native_thread);
		(void)st_mutex_unlock(&c->mutex);
#endif
		pause_io();
	}
	(void)st_thread_join(c->thread, NULL);
	close_transport(c); free(c->request); free(c->reply.data);
	st_condition_destroy(&c->ready); st_mutex_destroy(&c->mutex); free(c);
	state->control = NULL;
}

static int command(st_console_display_t *display, const uint8_t *p, size_t n, struct test_bytes *reply)
{
	uint32_t op = get32(p + 8); p += 16; n -= 16;
	struct st_console_test_state *state = st_console_test_state(display);
	switch (op) {
	case 1: /* hello */
		if (n != 0) return IO_ERROR_INVALID_ARGUMENT;
		put32(reply, TEST_VERSION); put32(reply, st_console_build_features()); put32(reply, 0x1ff);
		return 0;
	case 2: { /* one coherent capture */
		if (n != 0) return IO_ERROR_INVALID_ARGUMENT;
		struct st_console_test_capture *capture = NULL;
		int status = st_console_test_capture(display, &capture);
		if (status == 0) encode_capture(reply, capture);
		st_console_test_capture_destroy(capture);
		return status;
	}
	case 3: { /* normalized input: type and eight explicit union words */
		if (n != 36) return IO_ERROR_INVALID_ARGUMENT;
		uint32_t type = get32(p), a = get32(p + 4), b = get32(p + 8), c = get32(p + 12), d = get32(p + 16);
		struct st_console_event event = {.type = (enum st_console_event_type)type};
		switch (type) {
		case ST_CONSOLE_EVENT_KEY_CODE:
			if (a > UINT16_MAX) return IO_ERROR_OUT_OF_RANGE;
			if ((b & ST_CONSOLE_MODIFIER_ALT) != 0 && (a == ST_CONSOLE_KEY_LEFT || a == ST_CONSOLE_KEY_RIGHT)) {
				(void)st_mutex_lock(&display->backend_mutex);
				uint32_t width, height;
				int status = st_console_snap_geometry_size(&state->geometry, state->width, state->height,
				    a == ST_CONSOLE_KEY_RIGHT, ST_CONSOLE_TEST_MAX_DIMENSION, ST_CONSOLE_TEST_MAX_DIMENSION, &width, &height);
				if (status == 0 && !st_console_test_dimensions(width, height)) status = IO_ERROR_OUT_OF_RANGE;
				if (status == 0) { state->width = width; state->height = height; }
				(void)st_mutex_unlock(&display->backend_mutex);
				return status;
			}
			if ((b & ST_CONSOLE_MODIFIER_ALT) != 0 && a == 13) {
				(void)st_mutex_lock(&display->backend_mutex); state->fullscreen = !state->fullscreen;
				(void)st_mutex_unlock(&display->backend_mutex); return 0;
			}
			event.data.key.code = (uint16_t)a; event.data.key.modifiers = b; break;
		case ST_CONSOLE_EVENT_PHYSICAL_KEY:
			if (a > UINT16_MAX || c > 1 || d > 1) return IO_ERROR_OUT_OF_RANGE;
			event.data.physical_key.evdev = (uint16_t)a; event.data.physical_key.modifiers = b;
			event.data.physical_key.pressed = c != 0; event.data.physical_key.repeat = d != 0; break;
		case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
			if (a > UINT8_MAX) return IO_ERROR_OUT_OF_RANGE;
			event.data.codepage_byte = (uint8_t)a; break;
		case ST_CONSOLE_EVENT_TEXT:
			if (a > 0x10ffff || (a >= 0xd800 && a <= 0xdfff)) return IO_ERROR_INVALID_ENCODING;
			event.data.text.codepoint = a; event.data.text.modifiers = b; break;
		case ST_CONSOLE_EVENT_MOUSE:
			if (a > ST_CONSOLE_MOUSE_WHEEL) return IO_ERROR_OUT_OF_RANGE;
			(void)st_mutex_lock(&display->backend_mutex);
			st_console_bitmap_mouse_position(&state->frame, (int32_t)b, (int32_t)c, &event);
			(void)st_mutex_unlock(&display->backend_mutex);
			event.data.mouse.action = (enum st_console_mouse_action)a;
			event.data.mouse.buttons = d; event.data.mouse.modifiers = get32(p + 20);
			event.data.mouse.wheel_x = (int32_t)get32(p + 24); event.data.mouse.wheel_y = (int32_t)get32(p + 28); break;
		case ST_CONSOLE_EVENT_RESIZE:
			{
				st_console_t *active = atomic_load_explicit(&display->active, memory_order_acquire);
				if (active == NULL) return IO_ERROR_NOT_CONNECTED;
				if (!st_console_test_dimensions((uint64_t)a * active->cell_width + active->pixel_extra_width,
				    (uint64_t)b * active->cell_height + active->pixel_extra_height)) return IO_ERROR_OUT_OF_RANGE;
			}
			event.data.resize.columns = a; event.data.resize.rows = b; break;
		case ST_CONSOLE_EVENT_FOCUS:
			if (a > 1) return IO_ERROR_OUT_OF_RANGE;
			event.data.focus.focused = a != 0; break;
		case ST_CONSOLE_EVENT_CLOSE: break;
		default: return IO_ERROR_NOT_SUPPORTED;
		}
		return st_console_display_post_event(display, &event);
	}
	case 4: /* drawable resize */
		return n != 8 ? IO_ERROR_INVALID_ARGUMENT : st_console_test_host_resize(display, get32(p), get32(p + 4));
	case 5: { /* private clipboard; UTF-8 bytes, no embedded NUL */
		if (memchr(p, 0, n) != NULL) return IO_ERROR_INVALID_ARGUMENT;
		for (size_t offset = 0; offset < n;) {
			uint32_t scalar; size_t used;
			if (st_utf8_decode_one(&scalar, &used, p + offset, n - offset) != 0) return IO_ERROR_INVALID_ENCODING;
			offset += used;
		}
		char *text = malloc(n + 1);
		if (text == NULL) return IO_ERROR_OUT_OF_MEMORY;
		memcpy(text, p, n); text[n] = 0;
		st_console_t *console = atomic_load_explicit(&display->active, memory_order_acquire);
		int status = st_console_set_clipboard_text(console, text); free(text); return status;
	}
	case 6: /* driver-local presentation clock */
		if (n != 8 || get32(p) > 1) return IO_ERROR_INVALID_ARGUMENT;
		(void)st_mutex_lock(&display->backend_mutex);
		state->clock_override = get32(p) != 0; state->clock_ms = get32(p + 4);
		(void)st_mutex_unlock(&display->backend_mutex);
		return 0;
	default: return IO_ERROR_NOT_SUPPORTED;
	}
}

void st_console_test_service(st_console_display_t *display)
{
	struct st_console_test_state *state = st_console_test_state(display);
	struct test_control *c = state == NULL ? NULL : state->control;
	if (c == NULL) return;
	if (atomic_load_explicit(&c->disconnected, memory_order_acquire) && !c->close_posted) {
		c->close_posted = true;
		const struct st_console_event close_event = {.type = ST_CONSOLE_EVENT_CLOSE};
		(void)st_console_display_post_event(display, &close_event);
	}
	(void)st_mutex_lock(&c->mutex);
	uint8_t *request = c->pending ? c->request : NULL;
	size_t size = c->request_size;
	if (request != NULL) { c->request = NULL; c->pending = false; }
	(void)st_mutex_unlock(&c->mutex);
	if (request == NULL) return;
	struct test_bytes reply = {0};
	put32(&reply, 0); put32(&reply, TEST_VERSION); put32(&reply, get32(request + 4)); put32(&reply, get32(request + 8)); put32(&reply, 0);
	int status = command(display, request, size, &reply);
	if (reply.error != 0) status = reply.error;
	if (status != 0) { free(reply.data); reply = (struct test_bytes){0}; put32(&reply, 16); put32(&reply, TEST_VERSION); put32(&reply, get32(request + 4)); put32(&reply, get32(request + 8)); put32(&reply, (uint32_t)status); }
	else patch32(&reply, 0, (uint32_t)(reply.size - 4));
	free(request);
	(void)st_mutex_lock(&c->mutex);
	c->reply = reply; c->reply_ready = true; (void)st_condition_signal(&c->ready);
	(void)st_mutex_unlock(&c->mutex);
}
#endif

int st_console_test_start_control(st_console_display_t *display)
{
	return st_console_test_control_start(display);
}
void st_console_test_stop_control(st_console_display_t *display)
{
	st_console_test_control_destroy(display);
}
