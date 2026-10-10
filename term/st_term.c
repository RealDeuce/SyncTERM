/* Minimal connected-session loop for the modern CTerm component. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"

#include "audio.h"
#include "io_error.h"
#include "st_unicode_text.h"
#include "st_utf8.h"

#include <stdlib.h>
#include <string.h>

struct st_term_session {
	struct st_term_config config;
	st_cterm_t *cterm;
	int output_error;
};

static enum st_cterm_emulation
profile_emulation(enum st_screen_mode mode)
{
	switch (mode) {
	case ST_SCREEN_MODE_C64:
	case ST_SCREEN_MODE_C128_40:
	case ST_SCREEN_MODE_C128_80:
		return ST_CTERM_PETSCII;
	case ST_SCREEN_MODE_ATARI:
	case ST_SCREEN_MODE_ATARI_XEP80:
		return ST_CTERM_ATASCII;
	case ST_SCREEN_MODE_PRESTEL:
		return ST_CTERM_PRESTEL;
	case ST_SCREEN_MODE_BEEB:
		return ST_CTERM_BBC;
	case ST_SCREEN_MODE_ATARIST_40X25:
	case ST_SCREEN_MODE_ATARIST_80X25:
	case ST_SCREEN_MODE_ATARIST_80X25_MONO:
		return ST_CTERM_ATARI_ST_VT52;
	default:
		return ST_CTERM_ANSI_BBS;
	}
}

enum st_cterm_encoding
st_term_profile_encoding(const struct st_profile *profile)
{
	return profile != NULL
	    && profile->connection_type == ST_CONNECTION_SHELL
	    ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE;
}

static enum st_cterm_petscii_machine
profile_petscii_machine(enum st_screen_mode mode)
{
	switch (mode) {
	case ST_SCREEN_MODE_C128_40: return ST_CTERM_PETSCII_C128_40;
	case ST_SCREEN_MODE_C128_80: return ST_CTERM_PETSCII_C128_80;
	default: return ST_CTERM_PETSCII_C64;
	}
}

static int
send_all(st_connection_t *connection, const uint8_t *bytes, size_t size)
{
	while (size != 0) {
		ssize_t sent = st_connection_send(connection, bytes, size, -1);

		if (sent < 0)
			return (int)sent;
		if (sent == 0)
			return IO_ERROR_NOT_CONNECTED;
		bytes += (size_t)sent;
		size -= (size_t)sent;
	}
	return 0;
}

static void
cterm_output(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
	(void)origin;

	if (session->output_error == 0)
		session->output_error = send_all(session->config.connection,
		    bytes, size);
}

static void
cterm_size(void *context, uint32_t columns, uint32_t rows,
	uint32_t pixel_width, uint32_t pixel_height)
{
	st_term_session_t *session = context;
	const struct st_screen_geometry geometry = {
		.columns = columns,
		.rows = rows,
		.pixel_width = pixel_width,
		.pixel_height = pixel_height,
	};

	if (session->output_error == 0)
		session->output_error = st_connection_update_terminal_size(
		    session->config.connection, &geometry);
}

int
st_term_connect_profile(st_connection_t **result,
	const struct st_profile *profile, st_console_t *console,
	const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	struct st_screen_geometry geometry;

	if (result == NULL || profile == NULL || console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_console_size(console, &geometry.columns, &geometry.rows) != 0)
		return IO_ERROR_IO;
	if (st_console_pixel_size(console, &geometry.pixel_width,
	    &geometry.pixel_height) != 0) {
		geometry.pixel_width = 0;
		geometry.pixel_height = 0;
	}
	switch ((enum st_connection_type)profile->connection_type) {
	case ST_CONNECTION_RAW:
		return st_connection_connect_raw(result, profile, hooks, error);
	case ST_CONNECTION_RLOGIN:
	case ST_CONNECTION_RLOGIN_REVERSED:
		return st_connection_connect_rlogin(result, profile, &geometry,
		    hooks, error);
	case ST_CONNECTION_SERIAL:
	case ST_CONNECTION_SERIAL_3WIRE:
		return st_connection_connect_serial(result, profile, hooks, error);
	case ST_CONNECTION_MODEM:
		return st_connection_connect_modem(result, profile, hooks, error);
	case ST_CONNECTION_SHELL: {
		const struct st_connection_shell_environment environment = {
			.mm_charset = "UTF-8",
		};
		return st_connection_connect_shell(result, profile, &geometry,
		    &environment, hooks, error);
	}
	default:
		return IO_ERROR_NOT_SUPPORTED;
	}
}

int
st_term_session_create(st_term_session_t **result,
	const struct st_term_config *config)
{
	st_term_session_t *session;
	uint32_t columns;
	uint32_t rows;
	uint16_t palette_mapping[16];
	const struct st_screen_mode_descriptor *mode;
	int status;

	if (result == NULL || config == NULL || config->console == NULL
	    || config->connection == NULL || config->fonts == NULL
	    || st_console_size(config->console, &columns, &rows) != 0
	    || (!config->hide_status && rows < 2))
		return IO_ERROR_INVALID_ARGUMENT;
	session = calloc(1, sizeof(*session));
	if (session == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	session->config = *config;
	mode = st_screen_mode_get(config->screen_mode);
	if (mode != NULL)
		st_screen_mode_palette_mapping(mode, palette_mapping);
	const struct st_cterm_config cterm_config = {
		.console = config->console,
		.fonts = config->fonts,
		.audio = config->audio,
		.hyperlinks = config->hyperlinks,
		.emulation = profile_emulation(config->screen_mode),
		.encoding = config->encoding,
		.codepage = config->codepage,
		.palette_mapping = mode == NULL ? NULL : palette_mapping,
		.petscii_machine = profile_petscii_machine(config->screen_mode),
		.font_selector = config->font_selector,
		.terminal_rows = config->hide_status ? rows : rows - 1,
		.scrollback_lines = config->history == NULL ? 0
		    : st_session_history_capacity(config->history),
		.output = cterm_output,
		.output_context = session,
		.size_changed = cterm_size,
		.size_context = session,
	};
	status = st_cterm_create(&session->cterm, &cterm_config);
	if (status != 0) {
		free(session);
		return status;
	}
	if (!config->hide_status) {
		struct st_console_rect status_row = {0, rows - 1, columns, 1};
		struct st_console_cell blank = {0};
		struct st_console_style style;
		uint8_t width;

		status = st_console_get_style(config->console, &style);
		if (status == 0) {
			blank.foreground = style.foreground;
			blank.background = style.background;
			blank.flags = style.flags;
			blank.font = style.font;
			blank.glyph_font = style.font;
			status = st_console_font_lookup(config->console, blank.font, ' ',
			    &blank.glyph_font, &blank.glyph, &width);
			if (status == 0)
				status = st_console_fill(config->console, &status_row, &blank);
		}
		if (status != 0) {
			st_cterm_destroy(&session->cterm);
			free(session);
			return status;
		}
	}
	*result = session;
	return 0;
}

static void
publish_history(st_term_session_t *session)
{
	struct st_console_cell *cells = NULL;
	uint32_t columns;
	size_t rows;

	if (session->config.history == NULL)
		return;
	if (st_cterm_copy_history(session->cterm, &cells, &columns, &rows) == 0
	    && rows <= UINT32_MAX)
		(void)st_session_history_publish(session->config.history, cells,
		    columns, (uint32_t)rows, session->config.screen_mode,
		    !session->config.hide_status);
	free(cells);
}

void
st_term_session_destroy(st_term_session_t **session)
{
	if (session == NULL || *session == NULL)
		return;
	(void)st_cterm_finish((*session)->cterm);
	publish_history(*session);
	st_cterm_destroy(&(*session)->cterm);
	free(*session);
	*session = NULL;
}

static void
beep(st_term_session_t *session)
{
	audio_engine_t *engine = NULL;

	if (session->config.audio != NULL)
		(void)st_audio_runtime_acquire(session->config.audio, &engine);
	(void)audio_beep(engine, 800.0, 100);
}

int
st_term_session_send_text(st_term_session_t *session,
	const char *utf8, size_t size)
{
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	size_t consumed;
	int status;

	if (session == NULL || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_text_create(&text, ST_UNICODE_TEXT_GRAPHEMES);
	if (status == 0)
		status = st_unicode_text_feed(text, utf8, size, &consumed);
	if (status == 0 && consumed != size)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0)
		status = st_unicode_text_finish(text);
	while (status == 0 && st_unicode_text_peek(text, &item) == 0) {
		if (item.kind == ST_UNICODE_TEXT_ITEM_DECODE_ERROR) {
			beep(session);
		}
		else if (session->config.encoding == ST_CTERM_ENCODING_UTF8)
			status = send_all(session->config.connection, item.bytes,
			    item.size);
		else {
			uint32_t scalar;
			size_t scalar_size;
			uint8_t encoded;

			if (st_utf8_decode_one(&scalar, &scalar_size, item.bytes,
			    item.size) == 0 && scalar_size == item.size
			    && st_codepage_from_unicode(&encoded,
			    session->config.codepage, scalar))
				status = send_all(session->config.connection, &encoded, 1);
			else
				beep(session);
		}
		if (status == 0)
			status = st_unicode_text_consume(text);
	}
	st_unicode_text_destroy(&text);
	return status;
}

static bool
local_command(const struct st_console_event *event,
	enum st_term_disposition *disposition)
{
	uint16_t code;
	uint32_t modifiers;

	if (event->type == ST_CONSOLE_EVENT_CLOSE) {
		*disposition = ST_TERM_EXIT_APPLICATION;
		return true;
	}
	if (event->type != ST_CONSOLE_EVENT_KEY_CODE)
		return false;
	code = event->data.key.code;
	modifiers = event->data.key.modifiers;
	if ((modifiers & ST_CONSOLE_MODIFIER_ALT) != 0
	    && (code == 'x' || code == 'X')) {
		*disposition = ST_TERM_EXIT_APPLICATION;
		return true;
	}
	if (((modifiers & ST_CONSOLE_MODIFIER_ALT) != 0
	    && (code == 'h' || code == 'H'))
	    || ((modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0
	    && (code == 'q' || code == 'Q' || code == 17))) {
		*disposition = ST_TERM_RETURN_TO_PICKER;
		return true;
	}
	return false;
}

static int
handle_event(st_term_session_t *session,
	const struct st_console_event *event,
	enum st_term_disposition *disposition, bool *finished)
{
	if (local_command(event, disposition)) {
		*finished = true;
		return 0;
	}
	switch (event->type) {
	case ST_CONSOLE_EVENT_TEXT: {
		char utf8[4];
		ptrdiff_t size = st_utf8_encode_one(utf8, sizeof(utf8),
		    event->data.text.codepoint);

		return size < 0 ? IO_ERROR_INVALID_ENCODING
		    : st_term_session_send_text(session, utf8, (size_t)size);
	}
	case ST_CONSOLE_EVENT_KEY_CODE:
		(void)st_cterm_handle_key(session->cterm, event->data.key.code);
		return session->output_error;
	case ST_CONSOLE_EVENT_PHYSICAL_KEY:
		(void)st_cterm_handle_physical_keys(session->cterm, event, 1);
		return session->output_error;
	case ST_CONSOLE_EVENT_DOORWAY_KEY:
		(void)st_cterm_handle_doorway_key(session->cterm,
		    event->data.doorway_key.scan_code);
		return session->output_error;
	case ST_CONSOLE_EVENT_MOUSE:
		(void)st_cterm_handle_mouse(session->cterm, event);
		return session->output_error;
	case ST_CONSOLE_EVENT_FOCUS:
		(void)st_cterm_handle_focus(session->cterm,
		    event->data.focus.focused);
		return session->output_error;
	case ST_CONSOLE_EVENT_RESIZE: {
		uint32_t pixel_width = 0;
		uint32_t pixel_height = 0;
		struct st_screen_geometry geometry;

		(void)st_console_pixel_size(session->config.console, &pixel_width,
		    &pixel_height);
		geometry = (struct st_screen_geometry){
			.columns = event->data.resize.columns,
			.rows = event->data.resize.rows
			    - (!session->config.hide_status
			    && event->data.resize.rows != 0),
			.pixel_width = pixel_width,
			.pixel_height = pixel_height,
		};
		return st_connection_update_terminal_size(
		    session->config.connection, &geometry);
	}
	default:
		return 0;
	}
}

int
st_term_session_pump(st_term_session_t *session,
	enum st_term_disposition *disposition, bool *finished)
{
	uint8_t bytes[4096];
	struct st_console_event event;
	ssize_t received;
	int status;

	if (session == NULL || disposition == NULL || finished == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*finished = false;
	received = st_connection_receive(session->config.connection, bytes,
	    sizeof(bytes), 20);
	if (received < 0 && received != IO_ERROR_TIMED_OUT
	    && received != IO_ERROR_WOULD_BLOCK)
		return (int)received;
	if (received > 0) {
		size_t consumed;

		status = st_cterm_write(session->cterm, bytes, (size_t)received,
		    &consumed);
		if (status != 0 || consumed != (size_t)received)
			return status == 0 ? IO_ERROR_IO : status;
	}
	while ((status = st_console_event_try(session->config.console,
	    &event)) == 0) {
		status = handle_event(session, &event, disposition, finished);
		if (status != 0 || *finished)
			return status;
	}
	if (status != IO_ERROR_WOULD_BLOCK)
		return status;
	if (!st_connection_is_connected(session->config.connection)
	    && received == 0) {
		*disposition = ST_TERM_RETURN_TO_PICKER;
		*finished = true;
	}
	return session->output_error;
}

int
st_term_session_run(st_term_session_t *session,
	enum st_term_disposition *disposition)
{
	bool finished = false;
	int status;

	if (session == NULL || disposition == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*disposition = ST_TERM_RETURN_TO_PICKER;
	while (!finished) {
		status = st_term_session_pump(session, disposition, &finished);
		if (status != 0)
			return status;
	}
	return st_cterm_finish(session->cterm);
}
