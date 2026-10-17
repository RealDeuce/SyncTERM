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
#include "st_time.h"
#include "st_threads.h"

#include <stdlib.h>
#include <string.h>

struct st_term_session {
	struct st_term_config config;
	st_cterm_t *cterm;
	int output_error;
	bool end_requested;
	bool exit_requested;
	bool remote_closed;
	bool suspended;
	bool mouse_disabled;
	uint32_t throttle_bps;
	uint64_t throttle_last_ms;
	uint64_t throttle_credit;
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
	    && profile->encoding == ST_PROFILE_ENCODING_UTF8
	    ? ST_CTERM_ENCODING_UTF8 : ST_CTERM_ENCODING_CODEPAGE;
}

enum st_cterm_rip_version
st_term_profile_rip_version(const struct st_profile *profile)
{
	if (profile == NULL)
		return ST_CTERM_RIP_OFF;
	switch (profile->rip_version) {
	case ST_RIP_VERSION_1: return ST_CTERM_RIP_1;
	case ST_RIP_VERSION_3: return ST_CTERM_RIP_3;
	default: return ST_CTERM_RIP_OFF;
	}
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
send_all_count(st_connection_t *connection, const uint8_t *bytes, size_t size,
	int64_t timeout_ms, size_t *queued_wire)
{
	while (size != 0) {
		ssize_t sent = st_connection_send(connection, bytes, size, timeout_ms);

		if (sent < 0)
			return (int)sent;
		if (sent == 0)
			return IO_ERROR_NOT_CONNECTED;
		if (queued_wire != NULL)
			*queued_wire += (size_t)sent;
		bytes += (size_t)sent;
		size -= (size_t)sent;
	}
	return 0;
}

static int
send_all(st_connection_t *connection, const uint8_t *bytes, size_t size)
{
	return send_all_count(connection, bytes, size, -1, NULL);
}

static void
cterm_output(void *context, enum st_cterm_output_origin origin,
	const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
	(void)origin;

	if (session->output_error != 0)
		return;
	if (session->config.rip_version != ST_CTERM_RIP_OFF
	    && session->config.encoding == ST_CTERM_ENCODING_CODEPAGE)
		session->output_error = st_term_session_send_text(session,
		    (const char *)bytes, size);
	else
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

static int
update_connection_geometry(st_term_session_t *session)
{
	uint32_t cell_width;
	uint32_t cell_height;
	struct st_screen_geometry geometry;
	int status;

	status = st_cterm_size(session->cterm, &geometry.columns, &geometry.rows);
	if (status == 0)
		status = st_console_cell_pixel_size(session->config.console,
		    &cell_width, &cell_height);
	if (status != 0)
		return status;
	geometry.pixel_width = geometry.columns * cell_width;
	geometry.pixel_height = geometry.rows * cell_height;
	return st_connection_update_terminal_size(session->config.connection,
	    &geometry);
}

int
st_term_connect_profile(st_connection_t **result,
	const struct st_profile *profile, st_console_t *console,
	const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	struct st_screen_geometry geometry;

	if (result == NULL || profile == NULL || console == NULL
	    || !st_profile_encoding_valid(profile->encoding))
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
	case ST_CONNECTION_TELNET:
	case ST_CONNECTION_TELNET_TLS:
		return st_connection_connect_telnet(result, profile, &geometry, hooks, error);
	case ST_CONNECTION_SSH:
	case ST_CONNECTION_SSH_NO_AUTH:
		return st_connection_connect_ssh(result, profile, &geometry, hooks, error);
	case ST_CONNECTION_MBBS_GHOST:
		return st_connection_connect_ghost(result, profile, hooks, error);
	case ST_CONNECTION_MQTT:
		return st_connection_connect_mqtt(result, profile, hooks, error);
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
			.mm_charset = st_profile_encoding_name(profile->encoding),
		};
		return st_connection_connect_shell(result, profile, &geometry,
		    &environment, hooks, error);
	}
	default:
		return IO_ERROR_INVALID_ARGUMENT;
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
	if (config->throttle_supported)
		session->throttle_bps = config->initial_throttle_bps;
	session->throttle_credit = UINT64_C(10000);
	mode = st_screen_mode_get(config->screen_mode);
	if (mode != NULL)
		st_screen_mode_palette_mapping(mode, palette_mapping);
	const struct st_cterm_config cterm_config = {
		.console = config->console,
		.fonts = config->fonts,
		.audio = config->audio,
		.hyperlinks = config->hyperlinks,
		.emulation = profile_emulation(config->screen_mode),
		.encoding = config->rip_version != ST_CTERM_RIP_OFF
		    ? ST_CTERM_ENCODING_UTF8 : config->encoding,
		.rip_version = config->rip_version,
		.rip_directory = config->rip_directory,
		.codepage = config->codepage,
		.palette_mapping = mode == NULL ? NULL : palette_mapping,
		.petscii_machine = profile_petscii_machine(config->screen_mode),
		.font_selector = config->font_selector,
		.terminal_rows = config->hide_status ? rows : rows - 1,
		.status_line = !config->hide_status,
		.scrollback_lines = config->history == NULL ? 0
		    : st_session_history_capacity(config->history),
		.output = cterm_output,
		.output_context = session,
		.size_changed = cterm_size,
		.size_context = session,
		.rip_prompt = config->rip_prompt,
		.rip_prompt_context = config->rip_prompt_context,
		.input_filter = config->input_filter,
		.input_filter_active = config->input_filter_active,
		.input_filter_context = config->input_filter_context,
		.capture = config->capture,
		.capture_active = config->capture_active,
		.capture_context = config->capture_context,
	};
	status = st_cterm_create(&session->cterm, &cterm_config);
	if (status != 0) {
		free(session);
		return status;
	}
	status = update_connection_geometry(session);
	if (status != 0) {
		st_cterm_destroy(&session->cterm);
		free(session);
		return status;
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

st_cterm_t *
st_term_session_cterm(st_term_session_t *session)
{
	return session == NULL ? NULL : session->cterm;
}

bool
st_term_session_font_selector(const st_term_session_t *session,
	uint16_t identifier, uint8_t *selector)
{
	return session != NULL && st_font_catalog_selector(
	    session->config.fonts, identifier, selector);
}

void
st_term_session_request_end(st_term_session_t *session, bool exit_app)
{
	if (session == NULL)
		return;
	session->end_requested = true;
	session->exit_requested = exit_app;
}

bool
st_term_session_remote_closed(const st_term_session_t *session)
{
	return session != NULL && session->remote_closed;
}

int
st_term_session_prepare_disconnect(st_term_session_t *session)
{
	if (session == NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status = st_cterm_prepare_disconnect(session->cterm);
	if (status == 0) session->config.hide_status = true;
	return status;
}

void
st_term_session_set_suspended(st_term_session_t *session, bool suspended)
{
	if (session != NULL) {
		if (session->suspended != suspended)
			session->throttle_last_ms = 0;
		session->suspended = suspended;
	}
}

void
st_term_session_set_mouse_disabled(st_term_session_t *session, bool disabled)
{
	if (session != NULL)
		session->mouse_disabled = disabled;
}

uint32_t
st_term_session_throttle_speed(const st_term_session_t *session)
{
	return session != NULL ? session->throttle_bps : 0;
}

void
st_term_session_set_throttle_speed(st_term_session_t *session, uint32_t bps)
{
	if (session == NULL || !session->config.throttle_supported)
		return;
	session->throttle_bps = bps;
	session->throttle_last_ms = 0;
	session->throttle_credit = UINT64_C(10000);
}

static void
beep(st_term_session_t *session)
{
	audio_engine_t *engine = NULL;

	if (session->config.audio != NULL)
		(void)st_audio_runtime_acquire(session->config.audio, &engine);
	(void)audio_beep(engine, 440.0, 100);
}

static int
send_text(st_term_session_t *session, const char *utf8, size_t size,
	int64_t timeout_ms, size_t *queued_wire)
{
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	size_t consumed;
	int status;

	if (session == NULL || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (queued_wire != NULL)
		*queued_wire = 0;
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
			status = send_all_count(session->config.connection, item.bytes,
			    item.size, timeout_ms, queued_wire);
		else {
			uint32_t scalar;
			size_t scalar_size;
			uint8_t encoded;

			if (st_utf8_decode_one(&scalar, &scalar_size, item.bytes,
			    item.size) == 0 && scalar_size == item.size
			    && st_codepage_from_unicode(&encoded,
			    session->config.codepage, scalar))
				status = send_all_count(session->config.connection, &encoded, 1,
				    timeout_ms, queued_wire);
			else
				beep(session);
		}
		if (status == 0)
			status = st_unicode_text_consume(text);
	}
	st_unicode_text_destroy(&text);
	return status;
}

int
st_term_session_send_text(st_term_session_t *session,
	const char *utf8, size_t size)
{
	return send_text(session, utf8, size, -1, NULL);
}

int
st_term_session_send_text_poll(st_term_session_t *session,
	const char *utf8, size_t size, size_t *queued_wire)
{
	if (queued_wire == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return send_text(session, utf8, size, 0, queued_wire);
}

int
st_term_prepare_paste(char **result, size_t *result_size,
	const char *utf8, size_t size, bool bracketed)
{
	static const char begin[] = "\x1b[200~";
	static const char end[] = "\x1b[201~";
	char *prepared;
	size_t used = 0;

	if (result == NULL || result_size == NULL
	    || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size > SIZE_MAX - (bracketed ? 13 : 1))
		return IO_ERROR_OUT_OF_RANGE;
	prepared = malloc(size + (bracketed ? 13 : 1));
	if (prepared == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (bracketed) {
		memcpy(prepared, begin, 6);
		used = 6;
	}
	for (size_t i = 0; i < size; ++i) {
		if (bracketed && size - i >= 6
		    && memcmp(utf8 + i, end, 6) == 0) {
			i += 5;
			continue;
		}
		if (utf8[i] == '\n') {
			if (i == 0 || utf8[i - 1] != '\r')
				prepared[used++] = '\r';
		}
		else
			prepared[used++] = utf8[i];
	}
	if (bracketed) {
		memcpy(prepared + used, end, 6);
		used += 6;
	}
	prepared[used] = '\0';
	*result = prepared;
	*result_size = used;
	return 0;
}

static bool
local_command(st_console_t *console, const struct st_console_event *event,
	enum st_term_disposition *disposition)
{
	uint16_t code;
	uint32_t modifiers;
	uint32_t capabilities = 0;

	if (event->type == ST_CONSOLE_EVENT_CLOSE) {
		*disposition = ST_TERM_EXIT_APPLICATION;
		return true;
	}
	if (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY
	    && event->data.physical_key.pressed) {
		uint32_t physical_modifiers =
		    event->data.physical_key.modifiers;
		uint16_t evdev = event->data.physical_key.evdev;

		if ((physical_modifiers & (ST_CONSOLE_MODIFIER_ALT
		    | ST_CONSOLE_MODIFIER_GUI)) != 0
		    && (evdev == 35 || evdev == 45)) {
			*disposition = evdev == 45 ? ST_TERM_EXIT_APPLICATION
			    : ST_TERM_RETURN_TO_PICKER;
			return true;
		}
		if ((physical_modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0
		    && evdev == 16
		    && st_console_capabilities(console, &capabilities) == 0
		    && (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0) {
			*disposition = ST_TERM_RETURN_TO_PICKER;
			return true;
		}
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
	    && (code == 'q' || code == 'Q' || code == 17)
	    && st_console_capabilities(console, &capabilities) == 0
	    && (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0)) {
		*disposition = ST_TERM_RETURN_TO_PICKER;
		return true;
	}
	return false;
}

static int
write_wire(st_term_session_t *session, const uint8_t *bytes, size_t size)
{
	char unicode[4096 * 4];
	size_t converted = 0;
	size_t consumed;
	int status;

	if (session->config.rip_version == ST_CTERM_RIP_OFF
	    || session->config.encoding == ST_CTERM_ENCODING_UTF8) {
		status = st_cterm_write(session->cterm, bytes, size, &consumed);
		return status == 0 && consumed != size ? IO_ERROR_IO : status;
	}
	status = st_term_rip_wire_to_utf8(unicode, sizeof(unicode),
	    &converted, bytes, size, session->config.codepage);
	if (status != 0)
		return status;
	status = st_cterm_write(session->cterm, unicode, converted, &consumed);
	return status == 0 && consumed != converted ? IO_ERROR_IO : status;
}

static int
write_wire_filtered(st_term_session_t *session, const uint8_t *bytes,
	size_t size)
{
	uint8_t replacement[ST_TERM_WIRE_REPLACEMENT_CAPACITY];
	size_t run_start = 0;
	int status;

	if (session->config.wire_filter == NULL
	    || (session->config.wire_filter_active != NULL
	    && !session->config.wire_filter_active(
	    session->config.wire_filter_context)))
		return write_wire(session, bytes, size);
	for (size_t i = 0; i < size; ++i) {
		bool consumed = false;
		size_t replacement_size = 0;

		status = session->config.wire_filter(
		    session->config.wire_filter_context, bytes[i], &consumed,
		    replacement, sizeof(replacement), &replacement_size);
		if (status != 0)
			return status;
		if (replacement_size > sizeof(replacement)
		    || (!consumed && replacement_size != 0))
			return IO_ERROR_INVALID_FORMAT;
		if (!consumed)
			continue;
		if (i > run_start) {
			status = write_wire(session, bytes + run_start,
			    i - run_start);
			if (status != 0)
				return status;
		}
		if (replacement_size != 0) {
			status = write_wire(session, replacement,
			    replacement_size);
			if (status != 0)
				return status;
		}
		run_start = i + 1;
	}
	return run_start < size ? write_wire(session, bytes + run_start,
	    size - run_start) : 0;
}

int
st_term_rip_wire_to_utf8(char *result, size_t capacity,
	size_t *result_size, const uint8_t *wire, size_t size,
	enum st_codepage codepage)
{
	size_t converted = 0;

	if (result_size == NULL || (result == NULL && (capacity != 0 || size != 0))
	    || (wire == NULL && size != 0)
	    || codepage < ST_CODEPAGE_CP437 || codepage >= ST_CODEPAGE_COUNT)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < size; i++) {
		/* RIP's ASCII introducer and delimiters are protocol bytes.
		 * CP437's display mapping of '|' to U+00A6 must not alter them. */
		uint32_t scalar = wire[i] < 0x80 ? wire[i]
		    : st_codepage_to_unicode(codepage, wire[i], false);
		ptrdiff_t written = st_utf8_encode_one(result + converted,
		    capacity - converted, scalar);

		if (written < 0)
			return (int)written;
		converted += (size_t)written;
	}
	*result_size = converted;
	return 0;
}

static int
handle_event(st_term_session_t *session,
	const struct st_console_event *event,
	enum st_term_disposition *disposition, bool *finished)
{
	if (session->config.event_hook != NULL) {
		bool consumed;
		int status = session->config.event_hook(
		    session->config.event_hook_context, event, &consumed);

		if (status != 0 || consumed)
			return status;
	}
	if (local_command(session->config.console, event, disposition)) {
		*finished = true;
		return 0;
	}
	switch (event->type) {
	case ST_CONSOLE_EVENT_TEXT: {
		char utf8[4];
		ptrdiff_t size = st_utf8_encode_one(utf8, sizeof(utf8),
		    event->data.text.codepoint);

		if (st_cterm_handle_text_key(session->cterm,
		    event->data.text.codepoint))
			return session->output_error;
		return size < 0 ? IO_ERROR_INVALID_ENCODING
		    : st_term_session_send_text(session, utf8, (size_t)size);
	}
	case ST_CONSOLE_EVENT_KEY_CODE:
		(void)st_cterm_handle_key(session->cterm, event->data.key.code,
		    event->data.key.modifiers);
		return session->output_error;
	case ST_CONSOLE_EVENT_PHYSICAL_KEY:
		(void)st_cterm_handle_physical_keys(session->cterm, event, 1);
		return session->output_error;
	case ST_CONSOLE_EVENT_DOORWAY_KEY:
		(void)st_cterm_handle_doorway_key(session->cterm,
		    event->data.doorway_key.scan_code);
		return session->output_error;
	case ST_CONSOLE_EVENT_MOUSE:
		if (!session->mouse_disabled)
			(void)st_cterm_handle_mouse(session->cterm, event);
		return session->output_error;
	case ST_CONSOLE_EVENT_FOCUS:
		(void)st_cterm_handle_focus(session->cterm,
		    event->data.focus.focused);
		return session->output_error;
	case ST_CONSOLE_EVENT_RESIZE: {
		int status;

		status = st_cterm_resize(session->cterm,
		    event->data.resize.columns, event->data.resize.rows);
		if (status != 0)
			return status;
		return update_connection_geometry(session);
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
	ssize_t received = IO_ERROR_WOULD_BLOCK;
	size_t read_size = sizeof(bytes);
	int status;

	if (session == NULL || disposition == NULL || finished == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*finished = false;
	status = st_connection_process_requests(session->config.connection);
	if (status != 0) return status;
	if (session->end_requested) {
		*disposition = session->exit_requested
		    ? ST_TERM_EXIT_APPLICATION : ST_TERM_RETURN_TO_PICKER;
		*finished = true;
		return 0;
	}
	if (!session->suspended && session->throttle_bps != 0) {
		uint64_t now;
		/* Credits are BPS*milliseconds: ten bits per character makes
		 * 10,000 credits one received byte.  Retain at most 100 ms of
		 * catch-up after an occupied UI tick. */
		uint64_t cap = (uint64_t)session->throttle_bps * 100;

		status = st_time_monotonic_ms(&now);
		if (status != 0)
			return status;
		if (cap < UINT64_C(10000))
			cap = UINT64_C(10000);
		if (session->throttle_last_ms != 0
		    && now >= session->throttle_last_ms) {
			uint64_t elapsed = now - session->throttle_last_ms;

			if (elapsed > 100)
				elapsed = 100;
			session->throttle_credit += elapsed
			    * session->throttle_bps;
			if (session->throttle_credit > cap)
				session->throttle_credit = cap;
		}
		session->throttle_last_ms = now;
		/* Idling does not bank a burst for the next remote write. */
		if (st_connection_pending_raw(session->config.connection) == 0
		    && session->throttle_credit > UINT64_C(10000))
			session->throttle_credit = UINT64_C(10000);
		read_size = (size_t)(session->throttle_credit / UINT64_C(10000));
		if (read_size > sizeof(bytes))
			read_size = sizeof(bytes);
	}
	if (!session->suspended && read_size != 0) {
		received = st_connection_receive(session->config.connection, bytes,
		    read_size, 20);
		if (received > 0 && session->throttle_bps != 0)
			session->throttle_credit -= (uint64_t)received
			    * UINT64_C(10000);
	}
	else if (!session->suspended
	    && st_connection_is_connected(session->config.connection)) {
		const struct timespec pause = {.tv_nsec = 10000000L};

		(void)st_thread_sleep(&pause, NULL);
	}
	if (received < 0 && received != IO_ERROR_TIMED_OUT
	    && received != IO_ERROR_WOULD_BLOCK) {
		session->remote_closed = !st_connection_is_connected(session->config.connection);
		status = st_connection_process_requests(session->config.connection);
		if (status != 0) return status;
		return (int)received;
	}
	if (received > 0) {
		status = write_wire_filtered(session, bytes, (size_t)received);
		if (status != 0)
			return status;
	}
	while ((status = st_console_event_try(session->config.console,
	    &event)) == 0) {
		status = handle_event(session, &event, disposition, finished);
		if (status != 0 || *finished)
			return status;
		if (session->end_requested) {
			*disposition = session->exit_requested
			    ? ST_TERM_EXIT_APPLICATION : ST_TERM_RETURN_TO_PICKER;
			*finished = true;
			return 0;
		}
	}
	if (status != IO_ERROR_WOULD_BLOCK)
		return status;
	if (!session->suspended
	    && !st_connection_is_connected(session->config.connection)
	    && st_connection_pending_raw(session->config.connection) == 0) {
		session->remote_closed = true;
		status = st_connection_process_requests(session->config.connection);
		if (status != 0) return status;
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
