/* Minimal connected-session loop for the modern CTerm component. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_term.h"

#include "audio.h"
#include "io_error.h"
#include "st_ooii.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "st_time.h"
#include "st_threads.h"

#include <stdlib.h>
#include <string.h>

struct st_term_session {
	struct st_term_config config;
	st_cterm_t *cterm;
	st_ooii_t *ooii;
	int output_error;
	bool end_requested;
	bool exit_requested;
	bool remote_closed;
	bool history_finalized;
	bool auxiliary_active;
	bool suspended;
	bool mouse_disabled;
	uint32_t throttle_bps;
	uint64_t throttle_last_ms;
	uint64_t throttle_credit;
	const void *wire_owner;
	bool input_dispatching;
	uint8_t wire_pending[4096];
	size_t wire_position;
	size_t wire_size;
	st_transfer_t *transfer;
	bool transfer_reserved, transfer_was_binary, wire_replaying;
	bool transfer_skip_lf;
	uint8_t *returned_wire;
	size_t returned_position, returned_size;
	uint8_t prestel_frame[65536];
	size_t prestel_frame_size;
};

bool st_term_session_inspect_transfer(st_term_session_t *session,
	struct st_transfer_inspection *state, bool *reserved)
{
	*reserved = session != NULL && session->transfer_reserved;
	if (session == NULL || session->transfer == NULL) return false;
	st_transfer_inspect(session->transfer, state);
	return true;
}

static int write_cterm_wire(void *context, const uint8_t *bytes, size_t size);
static int ooii_render(void *context, const uint8_t *bytes, size_t size);
static int ooii_reply(void *context, const uint8_t *bytes, size_t size);
static void ooii_sound(void *context, const uint8_t *samples, size_t size);
static unsigned ooii_attributes(void *context);

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
	if (session->wire_owner != NULL) return;
	if (origin == ST_CTERM_OUTPUT_RESPONSE && (session->config.suppress_responses
	    || session->transfer != NULL || session->transfer_reserved))
		return;

	if (session->output_error != 0
	    || !st_connection_is_connected(session->config.connection))
		return;
	if (session->config.rip_version != ST_CTERM_RIP_OFF
	    && session->config.encoding == ST_CTERM_ENCODING_CODEPAGE)
		session->output_error = st_term_session_send_text(session,
		    (const char *)bytes, size);
	else
		session->output_error = send_all(session->config.connection,
		    bytes, size);
	if (!st_connection_is_connected(session->config.connection)
	    || (session->output_error == IO_ERROR_NOT_CONNECTED
	    && st_connection_auxiliary_alive(session->config.connection)))
		session->output_error = 0;
}

static uint32_t
cterm_speed_query(void *context)
{
	return st_term_session_throttle_speed(context);
}

static void
cterm_speed_changed(void *context, uint32_t bps)
{
	st_term_session_set_throttle_speed(context, bps);
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

	if (session->output_error == 0
	    && st_connection_is_connected(session->config.connection)) {
		session->output_error = st_connection_update_terminal_size(
		    session->config.connection, &geometry);
		if (!st_connection_is_connected(session->config.connection))
			session->output_error = 0;
	}
}

static int
update_connection_geometry(st_term_session_t *session)
{
	uint32_t cell_width = 0;
	uint32_t cell_height = 0;
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
	uint32_t palette_override[16];
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
	if (config->palette_size > 16 || (config->palette_size != 0 && config->palette == NULL)) {
		free(session);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	st_screen_mode_profile_palette(mode, config->palette, config->palette_size,
	    palette_override);
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
		.apc_directory = config->apc_directory,
		.application_version = config->application_version,
		.codepage = config->codepage,
		.palette_mapping = mode == NULL ? NULL : palette_mapping,
		.petscii_machine = profile_petscii_machine(config->screen_mode),
		.font_selector = config->font_selector,
		.terminal_rows = config->hide_status ? rows : rows - 1,
		.status_line = !config->hide_status,
		.expand_lf = config->expand_lf,
		.force_lcf = config->force_lcf,
		.yellow_is_yellow = config->yellow_is_yellow,
		.palette_override = config->palette_size == 0 ? NULL : palette_override,
		.scrollback_lines = config->history == NULL ? 0
		    : st_session_history_capacity(config->history),
		.output = cterm_output,
		.output_context = session,
		.size_changed = cterm_size,
		.size_context = session,
		.speed_query = cterm_speed_query,
		.speed_changed = cterm_speed_changed,
		.speed_context = session,
		.rip_prompt = config->rip_prompt,
		.rip_prompt_context = config->rip_prompt_context,
		.rip_transfer = config->rip_transfer,
		.rip_transfer_context = config->rip_transfer_context,
		.input_filter = config->input_filter,
		.input_filter_active = config->input_filter_active,
		.input_filter_context = config->input_filter_context,
		.capture = config->capture,
		.capture_active = config->capture_active,
		.capture_context = config->capture_context,
		.log_level = config->cterm_log_level,
		.diagnostic = config->cterm_diagnostic,
		.diagnostic_context = config->cterm_diagnostic_context,
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
	const struct st_ooii_config ooii_config = {
		.pass = write_cterm_wire, .render = ooii_render,
		.reply = ooii_reply, .sound = ooii_sound,
		.attributes = ooii_attributes, .context = session,
	};
	status = st_ooii_create(&session->ooii, &ooii_config);
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

	if (session->config.history == NULL || session->history_finalized)
		return;
	/* Freeze the completed session before a disconnected notice or viewer
	 * can draw on its console. Do not capture those UI overlays at destroy. */
	session->history_finalized = true;
	st_session_history_clear(session->config.history);
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
	(void)st_term_session_end_transfer(*session);
	(void)st_cterm_finish((*session)->cterm);
	publish_history(*session);
	st_ooii_destroy((*session)->ooii);
	st_cterm_destroy(&(*session)->cterm);
	free((*session)->returned_wire);
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
	int status = st_cterm_finish(session->cterm);
	if (status != 0) return status;
	/* Retain the completed screen before making room for the notice. */
	publish_history(session);
	return st_cterm_prepare_disconnect(session->cterm);
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

int
st_term_session_send_text(st_term_session_t *session,
	const char *utf8, size_t size)
{
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	size_t consumed = 0;
	int status;

	if (session == NULL || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (session->wire_owner != NULL) return IO_ERROR_BUSY;
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
			status = send_all(session->config.connection, item.bytes, item.size);
		else if (item.size == 2 && item.bytes[0] == '\r'
		    && item.bytes[1] == '\n')
			/* Unicode grapheme rule GB3 groups CRLF; both controls are
			 * nevertheless distinct bytes in a codepage connection. */
			status = send_all(session->config.connection, item.bytes, item.size);
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

int
st_term_session_encode_text(st_term_session_t *session,
	const char *utf8, size_t size, struct st_term_encoded_text *result)
{
	char *normalized = NULL;
	size_t normalized_size;
	int status;

	if (session == NULL || result == NULL || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_text_normalize(&normalized, &normalized_size,
	    utf8, size, NULL);
	if (status != 0)
		return status;
	if (session->config.encoding == ST_CTERM_ENCODING_UTF8) {
		*result = (struct st_term_encoded_text){
			.bytes = (uint8_t *)normalized, .size = normalized_size,
			.storage_size = normalized_size + 1, .utf8 = true,
		};
		return 0;
	}
	/* A codepage byte never expands its UTF-8 scalar. Convert in place;
	 * reject the complete request before the caller submits any output. */
	size_t used = 0;
	for (size_t position = 0; position < normalized_size;) {
		uint32_t scalar;
		size_t scalar_size;
		uint8_t encoded;

		if (st_utf8_decode_one(&scalar, &scalar_size,
		    (const uint8_t *)normalized + position,
		    normalized_size - position) != 0
		    || !st_codepage_from_unicode(&encoded, session->config.codepage, scalar)) {
			free(normalized);
			return IO_ERROR_INVALID_ENCODING;
		}
		normalized[used++] = (char)encoded;
		position += scalar_size;
	}
	*result = (struct st_term_encoded_text){
		.bytes = (uint8_t *)normalized, .size = used,
		.storage_size = normalized_size + 1,
	};
	return 0;
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
write_cterm_wire(void *context, const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
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
ooii_render(void *context, const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
	char unicode[4096];

	/* Resource data is CP437 even when the connection uses another encoding. */
	while (size != 0) {
		size_t chunk = size > sizeof(unicode) / 4 ? sizeof(unicode) / 4 : size;
		size_t converted = 0, consumed;
		int status = st_term_rip_wire_to_utf8(unicode, sizeof(unicode),
		    &converted, bytes, chunk, ST_CODEPAGE_CP437);
		if (status != 0) return status;
		status = st_cterm_write_utf8_input(session->cterm, unicode, converted, &consumed);
		if (status != 0 || consumed != converted)
			return status == 0 ? IO_ERROR_IO : status;
		bytes += chunk;
		size -= chunk;
	}
	return 0;
}

static int
ooii_reply(void *context, const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
	if (session->config.suppress_responses) return 0;
	return send_all(session->config.connection, bytes, size);
}

static void
ooii_sound(void *context, const uint8_t *samples, size_t size)
{
	st_term_session_t *session = context;
	(void)st_cterm_play_fx_u8(session->cterm, samples, size);
}

static unsigned
ooii_attributes(void *context)
{
	struct st_cterm_state state;
	st_term_session_t *session = context;
	return st_cterm_state(session->cterm, &state) == 0 ? state.attr : 7;
}

int
st_term_session_ooii_mode(const st_term_session_t *session)
{
	return session == NULL ? 0 : st_ooii_mode(session->ooii);
}

void
st_term_session_set_ooii_mode(st_term_session_t *session, int mode)
{
	if (session != NULL) st_ooii_set_mode(session->ooii, mode);
}

static int
write_wire(st_term_session_t *session, const uint8_t *bytes, size_t size)
{
	return st_ooii_write(session->ooii, bytes, size);
}

static int
write_wire_filtered(st_term_session_t *session, const uint8_t *bytes,
	size_t size)
{
	uint8_t replacement[ST_TERM_WIRE_REPLACEMENT_CAPACITY];
	size_t run_start = 0;
	int status;

	if (!session->wire_replaying && session->config.wire_capture != NULL
	    && (session->config.capture_active == NULL
	    || session->config.capture_active(session->config.capture_context))) {
		status = session->config.wire_capture(session->config.capture_context,
		    bytes, size);
		if (status != 0)
			return status;
	}
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

int
st_term_session_send_key(st_term_session_t *session,
	const struct st_console_event *event, bool *handled)
{
	if (session == NULL || event == NULL || handled == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*handled = false;
	if (session->wire_owner != NULL) return 0;
	switch (event->type) {
	case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
		if (session->config.encoding == ST_CTERM_ENCODING_CODEPAGE
		    && st_cterm_encoding(session->cterm) == ST_CTERM_ENCODING_CODEPAGE) {
			*handled = st_cterm_handle_codepage_key(session->cterm,
			    event->data.codepage_byte);
			return session->output_error;
		}
		/* FALLTHROUGH */
	case ST_CONSOLE_EVENT_TEXT: {
		uint32_t codepoint = event->type == ST_CONSOLE_EVENT_TEXT
		    ? event->data.text.codepoint : event->data.codepage_byte;
		char utf8[4];

		/* RIP's renderer uses UTF-8 internally even on a codepage link.
		 * Resolve numeric entry at the wire boundary before RIP hotkeys or
		 * text encoding; the display's encoding cannot supply its meaning. */
		if (event->type == ST_CONSOLE_EVENT_CODEPAGE_BYTE
		    && session->config.encoding == ST_CTERM_ENCODING_CODEPAGE)
			codepoint = st_codepage_to_unicode(session->config.codepage,
			    event->data.codepage_byte, false);
		if (!st_unicode_scalar_valid(codepoint))
			return IO_ERROR_INVALID_ENCODING;
		ptrdiff_t size = st_utf8_encode_one(utf8, sizeof(utf8), codepoint);
		*handled = true;
		if (st_cterm_handle_text_key(session->cterm, codepoint))
			return session->output_error;
		return size < 0 ? IO_ERROR_INVALID_ENCODING
		    : st_term_session_send_text(session, utf8, (size_t)size);
	}
	case ST_CONSOLE_EVENT_KEY_CODE:
		*handled = st_cterm_handle_key(session->cterm, event->data.key.code,
		    event->data.key.modifiers);
		return session->output_error;
	case ST_CONSOLE_EVENT_PHYSICAL_KEY:
		*handled = st_cterm_handle_physical_keys(session->cterm, event, 1);
		return session->output_error;
	case ST_CONSOLE_EVENT_DOORWAY_KEY:
		*handled = st_cterm_handle_doorway_key(session->cterm,
		    event->data.doorway_key.scan_code);
		return session->output_error;
	default:
		return IO_ERROR_INVALID_ARGUMENT;
	}
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
	case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
	case ST_CONSOLE_EVENT_TEXT:
	case ST_CONSOLE_EVENT_KEY_CODE:
	case ST_CONSOLE_EVENT_PHYSICAL_KEY:
	case ST_CONSOLE_EVENT_DOORWAY_KEY: {
		bool handled;
		return st_term_session_send_key(session, event, &handled);
	}
	case ST_CONSOLE_EVENT_MOUSE:
		if (!session->mouse_disabled)
			(void)st_cterm_handle_mouse(session->cterm, event);
		return session->output_error;
	case ST_CONSOLE_EVENT_FOCUS:
		(void)st_cterm_handle_focus(session->cterm,
		    event->data.focus.focused);
		return session->output_error;
	case ST_CONSOLE_EVENT_RESIZE: {
		uint32_t capabilities;
		int status;

		status = st_console_capabilities(session->config.console,
		    &capabilities);
		if (status != 0)
			return status;
		/* A host text-terminal resize moves a fixed BBS screen within the
		 * surround; it does not change the remote terminal's selected mode. */
		if ((capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0
		    && session->config.screen_mode != ST_SCREEN_MODE_CURRENT)
			return 0;
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
	struct st_console_event event;
	ssize_t received = IO_ERROR_WOULD_BLOCK;
	size_t read_size = sizeof(session->wire_pending);
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
	/* A stopped transport has no recipient for asynchronous reports.
	 * Still drain its received bytes and take the normal disconnect path. */
	if (st_connection_is_connected(session->config.connection)) {
		status = st_cterm_poll(session->cterm);
		if (status != 0 || session->output_error != 0)
			return status != 0 ? status : session->output_error;
	}
	if (!session->suspended && session->transfer == NULL && session->throttle_bps != 0) {
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
		if (session->wire_position == session->wire_size
		    && st_connection_pending_raw(session->config.connection) == 0
		    && session->throttle_credit > UINT64_C(10000))
			session->throttle_credit = UINT64_C(10000);
		read_size = (size_t)(session->throttle_credit / UINT64_C(10000));
		if (read_size > sizeof(session->wire_pending))
			read_size = sizeof(session->wire_pending);
	}
	bool protocol_input = session->transfer != NULL && st_transfer_needs_input(session->transfer);
	if (session->transfer != NULL && st_transfer_done(session->transfer)) read_size = 0;
	if (session->transfer_reserved || session->wire_owner != NULL) read_size = 0;
	if (protocol_input) {
		size_t space = st_transfer_input_space(session->transfer);
		if (read_size > space) read_size = space;
	}
	if ((!session->suspended || protocol_input) && read_size != 0
	    && session->returned_position == session->returned_size
	    && session->wire_position == session->wire_size) {
		received = st_connection_receive(session->config.connection, session->wire_pending,
		    read_size, 20);
	}
	else if ((!session->suspended || session->transfer != NULL || session->transfer_reserved)
	    && read_size == 0
	    && (st_connection_is_connected(session->config.connection)
	    || session->wire_position < session->wire_size)) {
		const struct timespec pause = {.tv_nsec = 10000000L};

		(void)st_thread_sleep(&pause, NULL);
	}
	if (received < 0 && received != IO_ERROR_TIMED_OUT
	    && received != IO_ERROR_WOULD_BLOCK) {
		if (session->transfer != NULL) {
			st_transfer_eof(session->transfer);
			received = IO_ERROR_WOULD_BLOCK;
		} else {
		session->remote_closed = !st_connection_is_connected(session->config.connection);
		status = st_connection_process_requests(session->config.connection);
		if (status != 0) return status;
		return (int)received;
		}
	}
	if (received > 0) {
		session->wire_position = 0;
		session->wire_size = (size_t)received;
	}
	if (protocol_input && received == 0) st_transfer_eof(session->transfer);
	/* A detector can claim the stream midway through this read. Recheck
	 * ownership after each ordinary byte, retaining any suffix on backpressure. */
	while (session->wire_owner == NULL && (!session->suspended || protocol_input) && !session->transfer_reserved
	    && (session->returned_position < session->returned_size
	    || session->wire_position < session->wire_size)) {
		bool replay = session->returned_position < session->returned_size;
		size_t *position = replay ? &session->returned_position : &session->wire_position;
		size_t size = replay ? session->returned_size : session->wire_size;
		uint8_t *bytes = replay ? session->returned_wire : session->wire_pending;
		protocol_input = session->transfer != NULL && st_transfer_needs_input(session->transfer);
		if (session->transfer != NULL && st_transfer_done(session->transfer)) break;
		if (protocol_input) {
			size_t count = size - *position, space = st_transfer_input_space(session->transfer);
			bool skip = session->transfer_skip_lf && bytes[*position] == '\n';
			if (skip) count = 1;
			if (!skip && count > space) count = space;
			if (count == 0) break;
			session->transfer_skip_lf = false;
			if (!replay && session->config.wire_capture != NULL
			    && (session->config.capture_active == NULL
			    || session->config.capture_active(session->config.capture_context))) {
				status = session->config.wire_capture(session->config.capture_context, bytes + *position, count);
				if (status != 0) return status;
			}
			*position += skip ? count : st_transfer_feed(session->transfer, bytes + *position, count);
			continue;
		}
		if (session->transfer == NULL && session->throttle_bps != 0) {
			if (session->throttle_credit < UINT64_C(10000)) break;
			session->throttle_credit -= UINT64_C(10000);
		}
		size_t count = 1;
		bool filtering = session->config.wire_filter != NULL
		    && (session->config.wire_filter_active == NULL
		    || session->config.wire_filter_active(session->config.wire_filter_context));
		if (!filtering && !session->config.throttle_supported && session->throttle_bps == 0
		    && session->config.rip_version == ST_CTERM_RIP_OFF
		    && session->config.screen_mode != ST_SCREEN_MODE_PRESTEL)
			count = size - *position;
		if (!replay && session->config.screen_mode == ST_SCREEN_MODE_PRESTEL) {
			if (bytes[*position] == 12) session->prestel_frame_size = 0;
			if (session->prestel_frame_size < sizeof(session->prestel_frame))
				session->prestel_frame[session->prestel_frame_size++] = bytes[*position];
		}
		session->wire_replaying = replay;
		session->input_dispatching = true;
		status = write_wire_filtered(session, bytes + *position, count);
		session->input_dispatching = false;
		session->wire_replaying = false;
		if (status != 0) return status;
		*position += count;
		protocol_input = session->transfer != NULL && st_transfer_needs_input(session->transfer);
	}
	if (received == 0 && session->auxiliary_active) {
		const struct timespec pause = {.tv_nsec = 10000000L};
		(void)st_thread_sleep(&pause, NULL);
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
	    && session->returned_position == session->returned_size
	    && !st_connection_is_connected(session->config.connection)
	    && session->wire_position == session->wire_size
	    && st_connection_pending_raw(session->config.connection) == 0) {
		session->remote_closed = true;
		status = st_connection_process_requests(session->config.connection);
		if (status != 0) return status;
		*disposition = ST_TERM_RETURN_TO_PICKER;
		*finished = !session->auxiliary_active && session->transfer == NULL && !session->transfer_reserved;
	}
	return session->output_error;
}

void
st_term_session_set_auxiliary_active(st_term_session_t *session, bool active)
{
	if (session != NULL) session->auxiliary_active = active;
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

int
st_term_session_begin_transfer(st_term_session_t *session, st_transfer_t *transfer)
{
	if (session == NULL || transfer == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (session->transfer != NULL || session->wire_owner != NULL) return IO_ERROR_BUSY;
	session->transfer_was_binary = st_connection_binary_mode(session->config.connection);
	int status = st_transfer_needs_input(transfer)
	    ? st_connection_set_binary_mode(session->config.connection, true) : 0;
	if (status != 0) return status;
	session->transfer = transfer;
	session->transfer_reserved = false;
	return 0;
}
int
st_term_session_end_transfer(st_term_session_t *session)
{
	if (session == NULL || session->transfer == NULL) return 0;
	st_transfer_t *transfer = session->transfer;
	(void)st_transfer_join(transfer);
	int status = 0;
	if (st_transfer_needs_input(transfer)) {
		if (st_transfer_result(transfer) == 0) {
			/* Queued bytes precede any suffix not yet fed to the worker.
			 * A second transfer can begin within returned terminal output. */
			size_t remaining = session->returned_size - session->returned_position;
			uint8_t *returned = malloc(65536 + remaining);
			if (returned == NULL) status = IO_ERROR_OUT_OF_MEMORY;
			else {
				size_t count = st_transfer_take_input(transfer, returned, 65536);
				if (remaining != 0) memcpy(returned + count,
				    session->returned_wire + session->returned_position, remaining);
				free(session->returned_wire);
				session->returned_wire = returned;
				session->returned_position = 0;
				session->returned_size = count + remaining;
			}
		} else {
			/* The unread suffix has not passed the capture callback yet.
			 * Discarding protocol residue must not truncate ANSI capture. */
			if (session->wire_position < session->wire_size
			    && session->config.wire_capture != NULL
			    && (session->config.capture_active == NULL
			    || session->config.capture_active(session->config.capture_context)))
				status = session->config.wire_capture(session->config.capture_context,
				    session->wire_pending + session->wire_position,
				    session->wire_size - session->wire_position);
			session->wire_position = session->wire_size;
			session->returned_position = session->returned_size;
		}
	}
	session->transfer = NULL;
	session->transfer_reserved = false;
	session->transfer_skip_lf = false;
	int restored = st_connection_set_binary_mode(session->config.connection,
	    session->transfer_was_binary);
	return status != 0 ? status : restored;
}
int
st_term_session_reserve_transfer(st_term_session_t *session, bool reserved)
{
	if (session != NULL) {
		if (reserved && session->wire_owner != NULL) return IO_ERROR_BUSY;
		session->transfer_reserved = reserved;
		if (!reserved) session->transfer_skip_lf = false;
	}
	return 0;
}
int
st_term_session_reserve_rip_transfer(st_term_session_t *session)
{
	if (session != NULL) {
		if (session->wire_owner != NULL) return IO_ERROR_BUSY;
		session->transfer_reserved = true;
		session->transfer_skip_lf = true;
	}
	return 0;
}
const uint8_t *
st_term_session_prestel_frame(st_term_session_t *session, size_t *size)
{
	*size = session == NULL ? 0 : session->prestel_frame_size;
	return session == NULL ? NULL : session->prestel_frame;
}

int
st_term_session_claim_wire(st_term_session_t *session, const void *owner)
{
	if (session == NULL || owner == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (session->wire_owner != NULL || session->transfer != NULL
	    || session->transfer_reserved || session->input_dispatching) return IO_ERROR_BUSY;
	session->wire_owner = owner;
	return 0;
}
bool
st_term_session_release_wire(st_term_session_t *session, const void *owner)
{
	if (session == NULL || owner == NULL || session->wire_owner != owner) return false;
	session->wire_owner = NULL;
	return true;
}
bool
st_term_session_wire_owned(st_term_session_t *session)
{
	return session != NULL && session->wire_owner != NULL;
}
int
st_term_session_wire_available(st_term_session_t *session, const void *owner, size_t *size)
{
	if (session == NULL || owner == NULL || session->wire_owner != owner || size == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*size = session->returned_size - session->returned_position
	    + session->wire_size - session->wire_position
	    + st_connection_pending_raw(session->config.connection);
	return 0;
}
ssize_t
st_term_session_read_wire(st_term_session_t *session, const void *owner,
	void *bytes, size_t size, bool consume)
{
	size_t available;
	int status = st_term_session_wire_available(session, owner, &available);
	if (status != 0 || (bytes == NULL && size != 0)) return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0) return 0;
	if (available == 0) return st_connection_is_connected(session->config.connection)
	    ? IO_ERROR_WOULD_BLOCK : 0;
	if (size > available) size = available;
	size_t replay = session->returned_size - session->returned_position;
	if (replay > size) replay = size;
	size_t pending = session->wire_size - session->wire_position;
	if (pending > size - replay) pending = size - replay;
	uint8_t *destination = bytes;
	if (replay != 0) memcpy(destination, session->returned_wire + session->returned_position, replay);
	if (pending != 0) memcpy(destination + replay, session->wire_pending + session->wire_position, pending);
	size_t queued = size - replay - pending;
	if (queued != 0) {
		/* Read once: a transport worker can flush its queue between calls
		 * (rlogin urgent FLUSHWRITE). Never capture a peek then consume a
		 * potentially different suffix. */
		ssize_t count = consume
		    ? st_connection_receive_raw(session->config.connection,
		    destination + replay + pending, queued, 0)
		    : st_connection_peek_raw(session->config.connection,
		    destination + replay + pending, queued);
		if (count == IO_ERROR_WOULD_BLOCK) count = 0;
		if (count < 0) return count;
		queued = (size_t)count;
	}
	if (consume) {
		session->returned_position += replay;
		session->wire_position += pending;
		/* Returned input was already captured on entering its transfer.
		 * A capture failure consumes this read and ends owned input. */
		if (pending + queued != 0 && session->config.wire_capture != NULL
		    && (session->config.capture_active == NULL
		    || session->config.capture_active(session->config.capture_context))) {
			status = session->config.wire_capture(session->config.capture_context,
			    destination + replay, pending + queued);
			if (status != 0) return status;
		}
	}
	if (replay + pending + queued == 0 && st_connection_is_connected(session->config.connection))
		return IO_ERROR_WOULD_BLOCK;
	return (ssize_t)(replay + pending + queued);
}
