/* Keyboard forwarding shares the native terminal encoding policy. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connection_internal.h"
#include "console_internal.h"
#include "io_error.h"
#include "st_term.h"

#define CHECK(expression)                                                                \
	do {                                                                             \
		if (!(expression)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
			return EXIT_FAILURE;                                             \
		}                                                                        \
	} while (0)

static int
headless_present(st_console_t *console)
{
	(void)console;
	return 0;
}

static unsigned prohibited_requests;

static int
limited_pointer(st_console_display_t *display, enum st_console_pointer_shape shape)
{
	(void)display;
	if (shape == ST_CONSOLE_POINTER_PROHIBITED) {
		prohibited_requests++;
		return IO_ERROR_NOT_SUPPORTED;
	}
	return 0;
}

struct graphical_prompt {
	bool     ready;
	unsigned calls;
};

static int
render_prompt(void *context, const char *question, const char *initial, size_t width, bool reject_blank,
    char **result)
{
	struct graphical_prompt *prompt = context;
	(void)question;
	(void)initial;
	(void)width;
	(void)reject_blank;
	prompt->calls++;
	*result = NULL;
	if (prompt->ready) {
		*result = malloc(6);
		if (*result == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memcpy(*result, "Ready", 6);
	}
	return 0;
}

static char observed_order[8];

static int
observe_event(void *context, const struct st_console_event *event, bool *consumed)
{
	unsigned *calls = context;
	(void)event;
	++*calls;
	if (event->type == ST_CONSOLE_EVENT_TEXT && *calls <= sizeof(observed_order))
		observed_order[*calls - 1] = (char)event->data.text.codepoint;
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
st_term_key_forward_test_main(void)
{
	static const struct st_connection_transport_ops operations = {0};
	struct st_connection                            connection = {.operations = &operations};
	const struct st_console_display_config          display    = {
	    .backend = ST_CONSOLE_BACKEND_HEADLESS,
	};
	const struct st_console_config geometry = {
	    .columns     = 80,
	    .rows        = 25,
	    .cell_width  = 8,
	    .cell_height = 16,
	};
	st_console_t           *console          = NULL;
	st_font_catalog_t      *fonts            = NULL;
	unsigned                hook_calls       = 0;
	struct graphical_prompt graphical_prompt = {0};

	/* No worker consumes the ring, so output is independent of network timing. */
	atomic_init(&connection.connected, true);
	atomic_init(&connection.stopping, false);
	atomic_init(&connection.binary_mode, false);
	CHECK(st_mutex_init(&connection.input.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.input.changed) == ST_THREAD_SUCCESS);
	CHECK(st_event_create(&connection.input.activity, ST_EVENT_AUTO_RESET, false) == ST_THREAD_SUCCESS);
	CHECK(st_mutex_init(&connection.output.mutex, ST_MUTEX_PLAIN) == ST_THREAD_SUCCESS);
	CHECK(st_condition_init(&connection.output.changed) == ST_THREAD_SUCCESS);
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	const struct st_console_backend_ops *original_ops = console->display->backend_ops;
	struct st_console_backend_ops        limited_ops  = {.present = headless_present};
	if (original_ops != NULL)
		limited_ops = *original_ops;
	limited_ops.set_pointer_shape = limited_pointer;
	for (unsigned mode = 0; mode < 6; ++mode) {
		bool                        utf8    = (mode & 1) == 0;
		bool                        rip     = mode >= 2;
		st_term_session_t          *session = NULL;
		const struct st_term_config config  = {
		    .console            = console,
		    .connection         = &connection,
		    .fonts              = fonts,
		    .screen_mode        = ST_SCREEN_MODE_80X25,
		    .hide_status        = true,
		    .encoding           = utf8 ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE,
		    .codepage           = ST_CODEPAGE_CP437,
		    .graphics_mode      = mode >= 4 ? ST_CTERM_GRAPHICS_RIP_3
		                          : rip     ? ST_CTERM_GRAPHICS_RIP_1
		                                    : ST_CTERM_GRAPHICS_OFF,
		    .event_hook         = observe_event,
		    .event_hook_context = &hook_calls,
		    .rip_prompt         = render_prompt,
		    .rip_prompt_context = &graphical_prompt,
		};
		CHECK(st_term_session_create(&session, &config) == 0);
		struct st_console_event event = {
		    .type      = ST_CONSOLE_EVENT_TEXT,
		    .data.text = {.codepoint = 0xe9,
				  .modifiers           = ST_CONSOLE_MODIFIER_SHIFT | ST_CONSOLE_MODIFIER_CAPS_LOCK},
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
		event =
		    (struct st_console_event) {.type = ST_CONSOLE_EVENT_CODEPAGE_BYTE, .data.codepage_byte = 0x82};
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		if (connection.output.count != (utf8 ? 2U : 1U))
			fprintf(stderr, "numeric entry mode %u (UTF-8 %d, RIP %d)\n", mode, utf8, rip);
		CHECK(expect_output(&connection, utf8 ? "\xc2\x82" : "\x82", utf8 ? 2 : 1) == 0);
		event = (struct st_console_event) {
		    .type     = ST_CONSOLE_EVENT_KEY_CODE,
		    .data.key = {.code = '\t', .modifiers = ST_CONSOLE_MODIFIER_SHIFT}
                };
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		CHECK(expect_output(&connection, "\x1b[Z", 3) == 0);
		event.data.key.code      = ST_CONSOLE_KEY_RIGHT;
		event.data.key.modifiers = ST_CONSOLE_MODIFIER_ALT;
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && !handled);
		CHECK(connection.output.count == 0);
		/* A local hangup shortcut must not be dispatched by forwarding. */
		event.data.key.code = 'x';
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && !handled);
		CHECK(connection.output.count == 0 && hook_calls == 0);
		event = (struct st_console_event) {.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		    .data.doorway_key.scan_code          = 0x48};
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
		CHECK(expect_output(&connection, "\x1b[A", 3) == 0);
		event = (struct st_console_event) {
		    .type              = ST_CONSOLE_EVENT_PHYSICAL_KEY,
		    .data.physical_key = {.evdev = 30,
					  .pressed                 = true,
					  .modifiers               = ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT}
                };
		CHECK(st_term_session_send_key(session, &event, &handled) == 0 && !handled);
		CHECK(connection.output.count == 0);
		event.type                = ST_CONSOLE_EVENT_TEXT;
		event.data.text.codepoint = 0xd800;
		CHECK(
		    st_term_session_send_key(session, &event, &handled) == IO_ERROR_INVALID_ENCODING && !handled);
		event.type = ST_CONSOLE_EVENT_MOUSE;
		CHECK(
		    st_term_session_send_key(session, &event, &handled) == IO_ERROR_INVALID_ARGUMENT && !handled);
		CHECK(st_term_session_send_key(NULL, &event, &handled) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_term_session_send_key(session, NULL, &handled) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_term_session_send_key(session, &event, NULL) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(connection.output.count == 0 && hook_calls == 0);
		if (mode >= 4) {
			/* No connection worker exists: feed one complete definition and
			 * fence through the actual receive pump, then disable only mouse
			 * input. Keyboard bindings remain independently enabled. */
			static const char program[] =
			    "!|1B00000200ZK000F000F080700000F07000000|1U0A0A1O1O1T00<><>H|1\0330000ready\r";
			memcpy(connection.input.data, program, sizeof(program) - 1);
			connection.input.head                = 0;
			connection.input.count               = sizeof(program) - 1;
			enum st_term_disposition disposition = ST_TERM_RETURN_TO_PICKER;
			bool                     finished    = false;
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
			CHECK(expect_output(&connection, "ready", 5) == 0);
			/* A missing native cursor must replace a prior wait cursor with
			 * arrow, without changing the requested protocol number or closing. */
			CHECK(st_mutex_lock(&console->display->backend_mutex) == ST_THREAD_SUCCESS);
			console->display->backend_ops = &limited_ops;
			CHECK(st_mutex_unlock(&console->display->backend_mutex) == ST_THREAD_SUCCESS);
			static const char pointer_program[] = "!|1c06|1c07|1\0330000pointer\r";
			unsigned          before_requests   = prohibited_requests;
			memcpy(connection.input.data, pointer_program, sizeof(pointer_program) - 1);
			connection.input.head  = 0;
			connection.input.count = sizeof(pointer_program) - 1;
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
			CHECK(expect_output(&connection, "pointer", 7) == 0);
			enum st_console_pointer_shape preference = ST_CONSOLE_POINTER_WAIT;
			CHECK(st_term_session_pointer_shape(session, &preference)
			      && preference == ST_CONSOLE_POINTER_DEFAULT
			      && console->pointer_shape == ST_CONSOLE_POINTER_DEFAULT
			      && prohibited_requests == before_requests + 1);
			CHECK(st_mutex_lock(&console->display->backend_mutex) == ST_THREAD_SUCCESS);
			console->display->backend_ops = original_ops;
			CHECK(st_mutex_unlock(&console->display->backend_mutex) == ST_THREAD_SUCCESS);
			st_term_session_set_mouse_disabled(session, true);
			event =
			    (struct st_console_event) {.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 'a'};
			CHECK(st_term_session_send_key(session, &event, &handled) == 0 && handled);
			CHECK(expect_output(&connection, "H", 1) == 0);
			st_term_session_set_mouse_disabled(session, false);
			graphical_prompt                   = (struct graphical_prompt) {0};
			static const char prompt_program[] = "!|T$*MISSING$|1\0330000after\r";
			memcpy(connection.input.data, prompt_program, sizeof(prompt_program) - 1);
			connection.input.head  = 0;
			connection.input.count = sizeof(prompt_program) - 1;
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished
			      && graphical_prompt.calls == 1);
			CHECK(connection.output.count == 0 && hook_calls == 0);
			event =
			    (struct st_console_event) {.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 'a'};
			CHECK(st_console_post_event(console, &event) == 0);
			event.data.text.codepoint = 'b';
			CHECK(st_console_post_event(console, &event) == 0);
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished
			      && hook_calls == 0);
			CHECK(connection.output.count == 0);
			graphical_prompt.ready = true;
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished
			      && hook_calls == 2);
			/* The text continuation paints once, then later wire bytes and the
			 * saved event resume through their original owners. */
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished
			      && hook_calls == 2);
			CHECK(expect_output(&connection, "after", 5) == 0 && memcmp(observed_order, "ab", 2) == 0);
			hook_calls = 0;
		}
		connection.output.stopped = true;
		event = (struct st_console_event) {.type = ST_CONSOLE_EVENT_KEY_CODE, .data.key.code = '\r'};
		CHECK(st_term_session_send_key(session, &event, &handled) == IO_ERROR_NOT_CONNECTED);
		CHECK(handled && connection.output.count == 0);
		st_term_session_destroy(&session);
		connection.output.stopped = false;
		if (mode >= 4) {
			CHECK(st_term_session_create(&session, &config) == 0);
			connection.output.stopped   = false;
			graphical_prompt            = (struct graphical_prompt) {0};
			static const char pending[] = "!|T$*SHUTDOWN$\r";
			memcpy(connection.input.data, pending, sizeof(pending) - 1);
			connection.input.head                = 0;
			connection.input.count               = sizeof(pending) - 1;
			enum st_term_disposition disposition = ST_TERM_RETURN_TO_PICKER;
			bool                     finished    = false;
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && !finished);
			event =
			    (struct st_console_event) {.type = ST_CONSOLE_EVENT_TEXT, .data.text.codepoint = 'c'};
			CHECK(st_console_post_event(console, &event) == 0);
			event = (struct st_console_event) {
			    .type     = ST_CONSOLE_EVENT_KEY_CODE,
			    .data.key = {.code = 'x', .modifiers = ST_CONSOLE_MODIFIER_ALT}
                        };
			CHECK(st_console_post_event(console, &event) == 0);
			CHECK(st_term_session_pump(session, &disposition, &finished) == 0 && finished
			      && disposition == ST_TERM_EXIT_APPLICATION && hook_calls == 0);
		}
		st_term_session_destroy(&session);
		connection.output.stopped = false;
	}
	st_font_catalog_destroy(fonts);
	st_console_destroy(console);
	st_condition_destroy(&connection.output.changed);
	st_mutex_destroy(&connection.output.mutex);
	st_event_destroy(connection.input.activity);
	st_condition_destroy(&connection.input.changed);
	st_mutex_destroy(&connection.input.mutex);
	return EXIT_SUCCESS;
}
