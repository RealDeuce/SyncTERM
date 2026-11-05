/* Keyboard forwarding shares the native terminal encoding policy. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include "st_term.h"
#include "connection_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

static int
observe_event(void *context, const struct st_console_event *event, bool *consumed)
{
	unsigned *calls = context;
	(void)event;
	++*calls;
	*consumed = true;
	return 0;
}

static int
expect_output(struct st_connection *connection, const char *bytes, size_t size)
{
	if (connection->output.count != size)
		fprintf(stderr, "output size %zu, expected %zu\n", connection->output.count, size);
	CHECK(connection->output.count == size);
	CHECK(memcmp(connection->output.data, bytes, size) == 0);
	connection->output.count = 0;
	return 0;
}

int
main(void)
{
	static const struct st_connection_transport_ops operations = {0};
	struct st_connection connection = {.operations = &operations};
	const struct st_console_display_config display = {
		.backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 16,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	unsigned hook_calls = 0;

	/* No worker consumes the ring, so output is independent of network timing. */
	atomic_init(&connection.connected, true);
	atomic_init(&connection.stopping, false);
	atomic_init(&connection.binary_mode, false);
	CHECK(st_mutex_init(&connection.input.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.input.changed) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&connection.output.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.output.changed) == ST_THREAD_SUCCESS);
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	for (unsigned mode = 0; mode < 4; ++mode) {
		bool utf8 = (mode & 1) == 0;
		bool rip = mode >= 2;
		st_term_session_t *session = NULL;
		const struct st_term_config config = {
			.console = console, .connection = &connection, .fonts = fonts,
			.screen_mode = ST_SCREEN_MODE_80X25, .hide_status = true,
			.encoding = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
			.codepage = ST_CODEPAGE_CP437,
			.graphics_mode = rip ? ST_CTERM_GRAPHICS_RIP_1 : ST_CTERM_GRAPHICS_OFF,
			.event_hook = observe_event, .event_hook_context = &hook_calls,
		};
		CHECK(st_term_session_create(&session, &config) == 0);
		struct st_console_event event = {
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text = {.codepoint = 0xe9,
				.modifiers = ST_CONSOLE_MODIFIER_SHIFT | ST_CONSOLE_MODIFIER_CAPS_LOCK},
		};
		bool handled = false;
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		CHECK(expect_output(&connection, utf8 ? "\xc3\xa9" : "\x82", utf8 ? 2 : 1) == 0);
		if (utf8) {
			event.data.text.codepoint = 0x1f600;
			CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
			CHECK(expect_output(&connection, "\xf0\x9f\x98\x80", 4) == 0);
		}
		event.data.text.codepoint = 0;
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		CHECK(expect_output(&connection, "\0", 1) == 0);
		event = (struct st_console_event){.type = ST_CONSOLE_EVENT_CODEPAGE_BYTE,
		    .data.codepage_byte = 0x82};
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		if (connection.output.count != (utf8 ? 2U : 1U))
			fprintf(stderr, "numeric entry mode %u (UTF-8 %d, RIP %d)\n", mode, utf8, rip);
		CHECK(expect_output(&connection, utf8 ? "\xc2\x82" : "\x82", utf8 ? 2 : 1) == 0);
		event = (struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE,
		    .data.key = {.code = '\t', .modifiers = ST_CONSOLE_MODIFIER_SHIFT}};
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		CHECK(expect_output(&connection, "\x1b[Z", 3) == 0);
		event.data.key.code = ST_CONSOLE_KEY_RIGHT;
		event.data.key.modifiers = ST_CONSOLE_MODIFIER_ALT;
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && !handled);
		CHECK(connection.output.count == 0);
		/* A local hangup shortcut must not be dispatched by forwarding. */
		event.data.key.code = 'x';
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && !handled);
		CHECK(connection.output.count == 0 && hook_calls == 0);
		event = (struct st_console_event){.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		    .data.doorway_key.scan_code = 0x48};
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		CHECK(expect_output(&connection, "\x1b[A", 3) == 0);
		event = (struct st_console_event){.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		    .data.physical_key = {.evdev = 30, .pressed = true,
			.modifiers = ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT}};
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && !handled);
		CHECK(connection.output.count == 0);
		event.type = ST_CONSOLE_EVENT_TEXT;
		event.data.text.codepoint = 0xd800;
		CHECK(st_term_session_send_key(session, &event, &handled) == IO_ERROR_INVALID_ENCODING && !handled);
		event.type = ST_CONSOLE_EVENT_MOUSE;
		CHECK(st_term_session_send_key(session, &event, &handled) == IO_ERROR_INVALID_ARGUMENT && !handled);
		CHECK(st_term_session_send_key(NULL, &event, &handled) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_term_session_send_key(session, NULL, &handled) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_term_session_send_key(session, &event, NULL) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(connection.output.count == 0 && hook_calls == 0);
		connection.output.stopped = true;
		event = (struct st_console_event){.type = ST_CONSOLE_EVENT_KEY_CODE,
		    .data.key.code = '\r'};
		CHECK(st_term_session_send_key(session, &event, &handled) == IO_ERROR_NOT_CONNECTED);
		CHECK(handled && connection.output.count == 0);
		st_term_session_destroy(&session);
		connection.output.stopped = false;
	}
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	st_condition_destroy(&connection.output.changed);
	st_mutex_destroy(&connection.output.mutex);
	st_condition_destroy(&connection.input.changed);
	st_mutex_destroy(&connection.input.mutex);
	return EXIT_SUCCESS;
}
