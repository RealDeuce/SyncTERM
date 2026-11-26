/* Minimal connected-session loop for the modern CTerm component. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "cterm_big.h"
#include "cterm_igs.h"
#include "cterm_skypix.h"
#include "io_error.h"
#include "st_ooii.h"
#include "st_rip154.h"
#include "st_term.h"
#include "st_term_rip3.h"
#include "st_threads.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_utf8.h"

/* Physical event order is retained while a graphical provider suspends.
 * Ordinary ANSI/RIP1 and completed RIP3 output do not use this queue. */
struct deferred_event {
	struct st_console_event event;
	struct deferred_event  *next;
	uint64_t                capture_token;
	bool                    embedded;
};

struct st_term_session {
	struct st_term_config  config;
	st_cterm_t            *cterm;
	struct cterm_big      *big;
	struct cterm_skypix   *skypix;
	struct cterm_igs      *igs;
	struct st_rip154      *rip154;
	st_ooii_t             *ooii;
	struct st_utf8_decoder wire_decoder;
	struct st_term_rip3   *rip3;
	struct deferred_event *deferred_head, *deferred_tail;
	struct deferred_event  modal_tail;
	bool                   modal_tail_queued;
	int                    output_error;
	bool                   end_requested;
	bool                   igs_dispatch_pending;
	bool                   did_work;
	bool                   exit_requested;
	bool                   remote_closed;
	bool                   history_finalized;
	bool                   auxiliary_active;
	bool                   suspended;
	bool                   mouse_disabled;
	uint32_t               throttle_bps;
	uint64_t               throttle_last_ms;
	uint64_t               throttle_credit;
	bool                   throttle_idle;
	const void            *wire_owner;
	bool                   input_dispatching;
	uint8_t                wire_pending[4096];
	size_t                 wire_position;
	size_t                 wire_size;
	st_transfer_t         *transfer;
	bool                   transfer_reserved, transfer_was_binary, wire_replaying;
	bool                   transfer_skip_lf;
	uint8_t               *returned_wire;
	size_t                 returned_position, returned_size;
	uint8_t                prestel_frame[65536];
	size_t                 prestel_frame_size;
};

static void
retain_prompt_tail(void *context, uint64_t token, const struct st_console_event *event)
{
	st_term_session_t *session = context;
	/* A single physical gesture has one terminal release/cancel. Later UI
	 * clicks are filtered by RIP3; queued input cannot start another capture
	 * until this node drains. Embedding it makes this handoff allocation-free. */
	if (session->modal_tail_queued)
		return;
	struct deferred_event *saved = &session->modal_tail;
	*saved = (struct deferred_event) {.event = *event, .capture_token = token, .embedded = true};
	if (session->deferred_tail != NULL)
		session->deferred_tail->next = saved;
	else
		session->deferred_head = saved;
	session->deferred_tail     = saved;
	session->modal_tail_queued = true;
}

void
st_term_session_observe_prompt_input(st_term_session_t *session, const struct st_console_event *event)
{
	if (session != NULL && event != NULL)
		st_term_rip3_observe_prompt_input(session->rip3, event);
}

bool
st_term_session_device_paused(st_term_session_t *session)
{
	return session != NULL
	       && (cterm_big_paused(session->big)
	           || (session->skypix != NULL && cterm_skypix_paused(session->skypix))
	           || cterm_igs_paused(session->igs));
}

int
st_term_session_poll_display(st_term_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = st_cterm_poll(st_term_session_cterm(session));
	if (status == 0 && session->igs != NULL)
		status = cterm_igs_poll(session->igs);
	if (status == 0 && session->big != NULL)
		status = cterm_big_poll(session->big);
	if (status == 0 && session->skypix != NULL)
		status = cterm_skypix_service(session->skypix);
	return status;
}

static int write_malformed(st_term_session_t *, const struct st_utf8_decode_event *);

static int
finish_wire_character(st_term_session_t *session)
{
	struct st_utf8_decode_event event;
	st_utf8_decoder_flush(&session->wire_decoder, &event);
	return event.kind == ST_UTF8_DECODE_EVENT_MALFORMED ? write_malformed(session, &event) : 0;
}

int
st_term_session_finish_display(st_term_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int pending = finish_wire_character(session);
	if (pending != 0)
		return pending;
	if (session->rip154 != NULL) {
		int status = st_rip154_finish(session->rip154);
		if (status != 0)
			return status;
	}
	if (session->igs != NULL)
		return cterm_igs_finish_display(session->igs);
	int status =
	    session->skypix != NULL ? cterm_skypix_finish(session->skypix) : cterm_big_finish(session->big);
	return status != 0 ? status : st_cterm_finish(st_term_session_cterm(session));
}

int
st_term_session_reset_display(st_term_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int pending = finish_wire_character(session);
	if (pending != 0)
		return pending;
	if (session->rip154 != NULL)
		return st_rip154_reset(session->rip154);
	if (session->igs != NULL)
		return cterm_igs_reset_display(session->igs);
	if (session->skypix != NULL) {
		int status = cterm_skypix_finish(session->skypix);
		if (status != 0)
			return status;
	}
	return session->big != NULL ? cterm_big_reset_terminal(session->big)
	                            : st_cterm_reset_terminal(st_term_session_cterm(session));
}

struct cterm_big *
st_term_session_big(st_term_session_t *session)
{
	return session == NULL ? NULL : session->big;
}

struct st_rip154 *
st_term_session_rip154(st_term_session_t *session)
{
	return session == NULL ? NULL : session->rip154;
}

struct cterm_igs *
st_term_session_igs(st_term_session_t *session)
{
	return session == NULL ? NULL : session->igs;
}

struct cterm_skypix *
st_term_session_skypix(st_term_session_t *session)
{
	return session == NULL ? NULL : session->skypix;
}

int
st_term_session_skypix_receive(st_term_session_t *session, const void *bytes, size_t size)
{
	return cterm_skypix_receive(st_term_session_skypix(session), bytes, size);
}

int
st_term_session_skypix_load_sample(st_term_session_t *session, const void *bytes, size_t size)
{
	return cterm_skypix_audio_load(st_term_session_skypix(session), bytes, size);
}

static bool
input_paused(st_term_session_t *session)
{
	return st_term_rip3_paused(session->rip3) || st_term_session_device_paused(session);
}

static int
finish(st_term_session_t *session)
{
	int status = finish_wire_character(session);
	if (status != 0)
		return status;
	st_term_rip3_cancel(session->rip3);
	return st_term_session_finish_display(session);
}

bool
st_term_session_inspect_transfer(st_term_session_t *session, struct st_transfer_inspection *state, bool *reserved)
{
	*reserved = session != NULL && session->transfer_reserved;
	if (session == NULL || session->transfer == NULL)
		return false;
	st_transfer_inspect(session->transfer, state);
	return true;
}

bool
st_term_session_transfer_active(const st_term_session_t *session)
{
	return session != NULL && (session->transfer != NULL || session->transfer_reserved);
}

void
st_term_session_set_rip3_transfer_protocols(st_term_session_t *session, uint32_t protocols)
{
	if (session != NULL && session->rip3 != NULL)
		st_term_rip3_set_transfer_protocols(session->rip3, protocols);
}

int
st_term_session_rip3_receive_asset(st_term_session_t *session, const char *directory, const char *cache_root,
    const char *name, unsigned file_type)
{
	if (session == NULL || session->rip3 == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	return st_term_rip3_receive_asset(session->rip3, directory, cache_root, name, file_type);
}

static int      ooii_render(void *context, const uint8_t *bytes, size_t size);
static int      ooii_reply(void *context, const uint8_t *bytes, size_t size);
static void     ooii_sound(void *context, const uint8_t *samples, size_t size);
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
	return profile != NULL && profile->encoding == ST_PROFILE_ENCODING_UTF8 ? ST_CTERM_ENCODING_UTF8
	                                                                        : ST_CTERM_ENCODING_CODEPAGE;
}

enum st_cterm_graphics_mode
st_term_profile_graphics_mode(const struct st_profile *profile)
{
	if (profile == NULL)
		return ST_CTERM_GRAPHICS_OFF;
	switch (profile->graphics_mode) {
		case ST_GRAPHICS_RIP_1:
			return ST_CTERM_GRAPHICS_RIP_1;
		case ST_GRAPHICS_RIP_3:
			return ST_CTERM_GRAPHICS_RIP_3;
		case ST_GRAPHICS_SKYPIX_13:
			return ST_CTERM_GRAPHICS_SKYPIX_13;
		case ST_GRAPHICS_SKYPIX_31:
			return ST_CTERM_GRAPHICS_SKYPIX_31;
		case ST_GRAPHICS_IGS_221:
			return ST_CTERM_GRAPHICS_IGS_221;
		case ST_GRAPHICS_BIG_103:
			return ST_CTERM_GRAPHICS_BIG_103;
		default:
			return ST_CTERM_GRAPHICS_OFF;
	}
}

enum st_screen_mode
st_term_profile_screen_mode(const struct st_profile *profile)
{
	if (profile == NULL)
		return ST_SCREEN_MODE_CURRENT;
	enum st_screen_mode mode = (enum st_screen_mode)profile->screen_mode;
	if (profile->graphics_mode != ST_GRAPHICS_BIG_103)
		return mode;
	switch (mode) {
		case ST_SCREEN_MODE_CGA_80X25:
		case ST_SCREEN_MODE_EGA_80X25:
		case ST_SCREEN_MODE_80X30:
			return mode;
		default:
			return ST_SCREEN_MODE_EGA_80X25;
	}
}

static enum st_cterm_petscii_machine
profile_petscii_machine(enum st_screen_mode mode)
{
	switch (mode) {
		case ST_SCREEN_MODE_C128_40:
			return ST_CTERM_PETSCII_C128_40;
		case ST_SCREEN_MODE_C128_80:
			return ST_CTERM_PETSCII_C128_80;
		default:
			return ST_CTERM_PETSCII_C64;
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
cterm_output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
	if (session->wire_owner != NULL)
		return;
	if (origin == ST_CTERM_OUTPUT_RESPONSE
	    && (session->config.suppress_responses || session->transfer != NULL || session->transfer_reserved))
		return;

	if (session->output_error != 0 || !st_connection_is_connected(session->config.connection))
		return;
	if (size != 0)
		session->did_work = true;
	if ((session->config.graphics_mode == ST_CTERM_GRAPHICS_RIP_1
	        || session->config.graphics_mode == ST_CTERM_GRAPHICS_RIP_3)
	    && session->config.encoding == ST_CTERM_ENCODING_CODEPAGE)
		session->output_error = st_term_session_send_text(session, (const char *)bytes, size);
	else
		session->output_error = send_all(session->config.connection, bytes, size);
	if (!st_connection_is_connected(session->config.connection)
	    || (session->output_error == IO_ERROR_NOT_CONNECTED
	        && st_connection_auxiliary_alive(session->config.connection)))
		session->output_error = 0;
}

static bool
igs_loop_service_pending(void *context)
{
	st_term_session_t *session    = context;
	session->igs_dispatch_pending = session->end_requested
	                                || !st_connection_is_connected(session->config.connection)
	                                || st_console_events_pending(session->config.console);
	return session->igs_dispatch_pending;
}

static void
rip3_autosense(void *context, uint16_t selector)
{
	st_term_session_t *session = context;
	st_term_rip3_autosense(session->rip3, selector);
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
cterm_size(void *context, uint32_t columns, uint32_t rows, uint32_t pixel_width, uint32_t pixel_height)
{
	st_term_session_t              *session  = context;
	const struct st_screen_geometry geometry = {
	    .columns      = columns,
	    .rows         = rows,
	    .pixel_width  = pixel_width,
	    .pixel_height = pixel_height,
	};

	if (session->output_error == 0 && st_connection_is_connected(session->config.connection)) {
		session->output_error = st_connection_update_terminal_size(session->config.connection, &geometry);
		if (!st_connection_is_connected(session->config.connection))
			session->output_error = 0;
	}
}

static int
update_connection_geometry(st_term_session_t *session)
{
	uint32_t                  cell_width  = 0;
	uint32_t                  cell_height = 0;
	struct st_screen_geometry geometry;
	int                       status;

	status = st_cterm_size(session->cterm, &geometry.columns, &geometry.rows);
	if (status == 0)
		status = st_console_cell_pixel_size(session->config.console, &cell_width, &cell_height);
	if (status != 0)
		return status;
	geometry.pixel_width  = geometry.columns * cell_width;
	geometry.pixel_height = geometry.rows * cell_height;
	return st_connection_update_terminal_size(session->config.connection, &geometry);
}

int
st_term_connect_profile(st_connection_t **result, const struct st_profile *profile, st_console_t *console,
    const struct st_connection_hooks *hooks, struct st_connection_error *error)
{
	struct st_screen_geometry geometry;

	if (result == NULL || profile == NULL || console == NULL || !st_profile_encoding_valid(profile->encoding))
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_profile effective;
	if (profile->graphics_mode == ST_GRAPHICS_SKYPIX_13 || profile->graphics_mode == ST_GRAPHICS_SKYPIX_31
	    || profile->graphics_mode == ST_GRAPHICS_BIG_103) {
		/* Advertise the active mode's system grid. Native graphics text
		 * metrics are independent of that grid and the wire encoding. */
		effective             = *profile;
		effective.screen_mode = ST_SCREEN_MODE_CURRENT;
		profile               = &effective;
	}
	if (st_console_size(console, &geometry.columns, &geometry.rows) != 0)
		return IO_ERROR_IO;
	if (st_console_pixel_size(console, &geometry.pixel_width, &geometry.pixel_height) != 0) {
		geometry.pixel_width  = 0;
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
			return st_connection_connect_rlogin(result, profile, &geometry, hooks, error);
		case ST_CONNECTION_SERIAL:
		case ST_CONNECTION_SERIAL_3WIRE:
			return st_connection_connect_serial(result, profile, hooks, error);
		case ST_CONNECTION_MODEM:
			return st_connection_connect_modem(result, profile, hooks, error);
		case ST_CONNECTION_SHELL:
		{
			const struct st_connection_shell_environment environment = {
			    .mm_charset = st_profile_encoding_name(profile->encoding),
			};
			return st_connection_connect_shell(result, profile, &geometry, &environment, hooks, error);
		}
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
}

int
st_term_session_create(st_term_session_t **result, const struct st_term_config *config)
{
	st_term_session_t                      *session;
	uint32_t                                columns;
	uint32_t                                rows;
	uint16_t                                palette_mapping[16];
	uint32_t                                palette_override[16];
	const struct st_screen_mode_descriptor *mode;
	bool                                    reserve_status;
	int                                     status;

	if (result == NULL || config == NULL || config->console == NULL || config->connection == NULL
	    || config->fonts == NULL || st_console_size(config->console, &columns, &rows) != 0)
		return IO_ERROR_INVALID_ARGUMENT;
	reserve_status = !config->hide_status && config->graphics_mode != ST_CTERM_GRAPHICS_RIP_3;
	if (reserve_status && rows < 2)
		return IO_ERROR_INVALID_ARGUMENT;
	session = calloc(1, sizeof(*session));
	if (session == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	session->config = *config;
	if (config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3) {
		status = st_term_rip3_prepare_console(config->console);
		if (status != 0) {
			free(session);
			return status;
		}
		status = st_console_size(config->console, &columns, &rows);
		if (status != 0) {
			free(session);
			return status;
		}
	}
	if (config->throttle_supported)
		session->throttle_bps = config->initial_throttle_bps;
	session->throttle_credit = UINT64_C(10000);
	mode                     = st_screen_mode_get(config->screen_mode);
	if (mode != NULL)
		st_screen_mode_palette_mapping(mode, palette_mapping);
	if (config->palette_size > 16 || (config->palette_size != 0 && config->palette == NULL)) {
		free(session);
		return IO_ERROR_INVALID_ARGUMENT;
	}
	st_screen_mode_profile_palette(mode, config->palette, config->palette_size, palette_override);
	const struct st_cterm_config cterm_config = {
	    .console                = config->console,
	    .fonts                  = config->fonts,
	    .audio                  = config->audio,
	    .hyperlinks             = config->hyperlinks,
	    .emulation              = (config->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_13
	                                  || config->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_31
	                                  || config->graphics_mode == ST_CTERM_GRAPHICS_BIG_103)
	                                  ? ST_CTERM_ANSI_BBS
	                                  : profile_emulation(config->screen_mode),
	    .encoding               = (config->graphics_mode == ST_CTERM_GRAPHICS_RIP_1
	                                  || config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3)
	                                  ? ST_CTERM_ENCODING_UTF8
	                                  : config->encoding,
	    .graphics_mode          = config->graphics_mode,
	    .rip3_session_owner     = config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3,
	    .rip3_autosense         = rip3_autosense,
	    .rip3_autosense_context = session,

	    .apc_directory       = config->apc_directory,
	    .application_version = config->application_version,
	    .codepage            = config->codepage,
	    .palette_mapping     = mode == NULL ? NULL : palette_mapping,
	    .petscii_machine     = profile_petscii_machine(config->screen_mode),
	    .font_selector       = config->font_selector,
	    .terminal_rows       = reserve_status ? rows - 1 : rows,
	    .status_line         = reserve_status,
	    .expand_lf           = config->expand_lf,
	    .force_lcf           = config->force_lcf,
	    .yellow_is_yellow    = config->yellow_is_yellow,
	    .palette_override    = config->palette_size == 0 ? NULL : palette_override,
	    .scrollback_lines    = config->history == NULL ? 0 : st_session_history_capacity(config->history),
	    .output              = cterm_output,
	    .output_context      = session,
	    .size_changed        = cterm_size,
	    .size_context        = session,
	    .speed_query                 = cterm_speed_query,
	    .speed_changed               = cterm_speed_changed,
	    .speed_context               = session,
	    .input_filter                = config->input_filter,
	    .input_filter_active         = config->input_filter_active,
	    .input_filter_context        = config->input_filter_context,
	    .capture                     = config->capture,
	    .capture_active              = config->capture_active,
	    .capture_context             = config->capture_context,
	    .log_level                   = config->cterm_log_level,
	    .diagnostic                  = config->cterm_diagnostic,
	    .diagnostic_context          = config->cterm_diagnostic_context,
	};
	if (config->graphics_mode == ST_CTERM_GRAPHICS_BIG_103)
		status = cterm_big_open(&session->big, &session->cterm, &cterm_config);
	else if (config->graphics_mode == ST_CTERM_GRAPHICS_RIP_1)
		status = st_rip154_open(&session->rip154, &session->cterm, &cterm_config,
		    &(struct st_rip154_services) {.rip_directory = config->rip_directory,
			.rip_prompt                              = config->rip_prompt,
			.rip_prompt_context                      = config->rip_prompt_context,
			.rip_choice                              = config->rip_choice,
			.rip_choice_context                      = config->rip_choice_context,
			.rip_transfer                            = config->rip_transfer,
			.rip_transfer_context                    = config->rip_transfer_context});
	else if (config->graphics_mode == ST_CTERM_GRAPHICS_IGS_221)
		status = cterm_igs_open(&session->igs, &session->cterm, &cterm_config, true,
		    igs_loop_service_pending, session);
	else if (config->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_13
	         || config->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_31)
		status = cterm_skypix_open(&session->skypix, &session->cterm, &cterm_config,
		    &(struct st_skypix_config) {.kick13 = config->graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_13,
			.width                          = config->skypix_width,
			.height                         = config->skypix_height,
			.font                           = config->amiga_font,
			.font_directory                 = config->amiga_font_directory,
			.transfer                       = config->skypix_transfer,
			.transfer_context               = config->skypix_transfer_context});
	else
		status = st_cterm_create(&session->cterm, &cterm_config);
	if (status != 0) {
		free(session);
		return status;
	}
	status = update_connection_geometry(session);
	if (status != 0) {
		st_rip154_destroy(session->rip154);
		cterm_igs_destroy(session->igs);
		cterm_skypix_destroy(session->skypix);
		cterm_big_destroy(session->big);
		st_cterm_destroy(&session->cterm);
		free(session);
		return status;
	}
	const struct st_ooii_config ooii_config = {
	    .render     = ooii_render,
	    .reply      = ooii_reply,
	    .sound      = ooii_sound,
	    .attributes = ooii_attributes,
	    .context    = session,
	};
	status = st_ooii_create(&session->ooii, &ooii_config);
	if (status != 0) {
		st_rip154_destroy(session->rip154);
		cterm_igs_destroy(session->igs);
		cterm_skypix_destroy(session->skypix);
		cterm_big_destroy(session->big);
		st_cterm_destroy(&session->cterm);
		free(session);
		return status;
	}
	if (config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3) {
		status = st_term_rip3_create(&session->rip3, config, session->cterm, &cterm_config, session);
		if (status != 0) {
			st_term_session_destroy(&session);
			return status;
		}
	}
	st_term_rip3_bind_prompt_input(session->rip3, retain_prompt_tail, session);
	*result = session;
	return 0;
}

static void
publish_history(st_term_session_t *session)
{
	struct st_console_cell  *cells  = NULL;
	struct st_pixel_buffer **pixels = NULL;
	uint32_t                 columns;
	size_t                   rows = 0;

	if (session->config.history == NULL || session->history_finalized)
		return;
	/* Freeze the session before disconnected UI can draw on its console. */
	session->history_finalized = true;
	st_session_history_clear(session->config.history);
	int status = st_cterm_copy_history(session->cterm, &cells, &columns, &rows);
	for (size_t row = 0; status == 0 && row < rows; ++row) {
		bool graphics = false;
		for (uint32_t x = 0; x < columns; ++x)
			graphics |= (cells[row * columns + x].flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0;
		if (!graphics)
			continue;
		if (pixels == NULL) {
			pixels = calloc(rows, sizeof(*pixels));
			if (pixels == NULL) {
				status = IO_ERROR_OUT_OF_MEMORY;
				break;
			}
		}
		status = st_cterm_copy_history_pixels(session->cterm, row, &pixels[row]);
	}
	if (status == 0 && rows <= UINT32_MAX) {
		status = st_session_history_publish(session->config.history, cells, columns, rows,
		    session->config.screen_mode, !session->config.hide_status, pixels);
		if (status == 0)
			pixels = NULL;
	}
	st_history_free_pixels(pixels, rows);
	free(cells);
}

void
st_term_session_destroy(st_term_session_t **session)
{
	if (session == NULL || *session == NULL)
		return;
	(void)st_term_session_end_transfer(*session);
	(void)finish(*session);
	publish_history(*session);
	st_ooii_destroy((*session)->ooii);
	st_term_rip3_destroy((*session)->rip3);
	st_rip154_destroy((*session)->rip154);
	cterm_igs_destroy((*session)->igs);
	cterm_skypix_destroy((*session)->skypix);
	cterm_big_destroy((*session)->big);
	st_cterm_destroy(&(*session)->cterm);
	free((*session)->returned_wire);
	while ((*session)->deferred_head != NULL) {
		struct deferred_event *event = (*session)->deferred_head;
		(*session)->deferred_head    = event->next;
		if (!event->embedded)
			free(event);
	}
	free(*session);
	*session = NULL;
}

st_cterm_t *
st_term_session_cterm(st_term_session_t *session)
{
	return session == NULL ? NULL : session->rip3 == NULL ? session->cterm : st_term_rip3_text(session->rip3);
}

st_cterm_t *
st_term_session_history_terminal(st_term_session_t *session)
{
	return session == NULL ? NULL : session->cterm;
}

bool
st_term_session_has_status_overlay(const st_term_session_t *session)
{
	return session != NULL && session->rip3 != NULL;
}

bool
st_term_session_status_visible(const st_term_session_t *session)
{
	if (session == NULL)
		return false;
	return session->rip3 != NULL ? st_term_rip3_status_visible(session->rip3)
	                             : st_cterm_status_display(session->cterm) == 1;
}

bool
st_term_session_text_view(st_term_session_t *session, st_console_t *display, struct st_term_text_view *view)
{
	return session != NULL && session->config.console == display
	       && st_term_rip3_text_view(session->rip3, view);
}

int
st_term_session_begin_local_text(st_term_session_t *session)
{
	return session == NULL ? IO_ERROR_INVALID_ARGUMENT : st_term_rip3_begin_local_text(session->rip3);
}

int
st_term_session_end_local_text(st_term_session_t *session)
{
	return session == NULL ? IO_ERROR_INVALID_ARGUMENT : st_term_rip3_end_local_text(session->rip3);
}

bool
st_term_session_font_selector(const st_term_session_t *session, uint16_t identifier, uint8_t *selector)
{
	return session != NULL && st_font_catalog_selector(session->config.fonts, identifier, selector);
}

void
st_term_session_request_end(st_term_session_t *session, bool exit_app)
{
	if (session == NULL)
		return;
	session->end_requested  = true;
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
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = finish(session);
	if (status != 0)
		return status;
	/* Retain the completed screen before making room for the notice. */
	publish_history(session);
	/* The disconnected viewer may reconfigure the display. Release all
	 * borrowed RIP raster views once the last live frame is archived. */
	st_term_rip3_destroy(session->rip3);
	session->rip3 = NULL;
	return st_cterm_prepare_disconnect(session->cterm);
}

void
st_term_session_set_suspended(st_term_session_t *session, bool suspended)
{
	if (session != NULL) {
		if (session->suspended != suspended)
			session->throttle_last_ms = 0;
		session->suspended = suspended;
		st_term_rip3_suspend_status(session->rip3, suspended);
	}
}

void
st_term_session_set_mouse_disabled(st_term_session_t *session, bool disabled)
{
	if (session != NULL) {
		session->mouse_disabled = disabled;
		if (disabled)
			st_term_rip3_mouse_cancel(session->rip3, false);
	}
}

int
st_term_session_mouse_mode(st_term_session_t *session)
{
	if (session == NULL)
		return 0;
	if (st_term_rip3_mouse_active(session->rip3))
		return 1;
	return st_cterm_mouse_mode(st_term_session_cterm(session));
}

bool
st_term_session_pointer_shape(const st_term_session_t *session, enum st_console_pointer_shape *result)
{
	return session != NULL && st_term_rip3_pointer_shape(session->rip3, result);
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
	session->throttle_bps     = bps;
	session->throttle_last_ms = 0;
	session->throttle_credit  = UINT64_C(10000);
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
st_term_session_send_text(st_term_session_t *session, const char *utf8, size_t size)
{
	st_unicode_text_t          *text = NULL;
	struct st_unicode_text_item item;
	size_t                      consumed = 0;
	int                         status;

	if (session == NULL || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (session->wire_owner != NULL)
		return IO_ERROR_BUSY;
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
		else if (item.size == 2 && item.bytes[0] == '\r' && item.bytes[1] == '\n')
			/* Unicode grapheme rule GB3 groups CRLF; both controls are
			 * nevertheless distinct bytes in a codepage connection. */
			status = send_all(session->config.connection, item.bytes, item.size);
		else {
			uint32_t scalar;
			size_t   scalar_size;
			uint8_t  encoded;

			if (st_utf8_decode_one(&scalar, &scalar_size, item.bytes, item.size) == 0
			    && scalar_size == item.size
			    && st_codepage_from_unicode(&encoded, session->config.codepage, scalar))
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
st_term_session_encode_text(st_term_session_t *session, const char *utf8, size_t size,
    struct st_term_encoded_text *result)
{
	char  *normalized = NULL;
	size_t normalized_size;
	int    status;

	if (session == NULL || result == NULL || (utf8 == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_unicode_text_normalize(&normalized, &normalized_size, utf8, size, NULL);
	if (status != 0)
		return status;
	if (session->config.encoding == ST_CTERM_ENCODING_UTF8) {
		*result = (struct st_term_encoded_text) {
		    .bytes        = (uint8_t *)normalized,
		    .size         = normalized_size,
		    .storage_size = normalized_size + 1,
		    .utf8         = true,
		};
		return 0;
	}
	/* A codepage byte never expands its UTF-8 scalar. Convert in place;
	 * reject the complete request before the caller submits any output. */
	size_t used = 0;
	for (size_t position = 0; position < normalized_size;) {
		uint32_t scalar;
		size_t   scalar_size;
		uint8_t  encoded;

		if (st_utf8_decode_one(&scalar, &scalar_size, (const uint8_t *)normalized + position,
			normalized_size - position)
		        != 0
		    || !st_codepage_from_unicode(&encoded, session->config.codepage, scalar)) {
			free(normalized);
			return IO_ERROR_INVALID_ENCODING;
		}
		normalized[used++] = (char)encoded;
		position += scalar_size;
	}
	*result = (struct st_term_encoded_text) {
	    .bytes        = (uint8_t *)normalized,
	    .size         = used,
	    .storage_size = normalized_size + 1,
	};
	return 0;
}

int
st_term_prepare_paste(char **result, size_t *result_size, const char *utf8, size_t size, bool bracketed)
{
	static const char begin[] = "\x1b[200~";
	static const char end[]   = "\x1b[201~";
	char             *prepared;
	size_t            used = 0;

	if (result == NULL || result_size == NULL || (utf8 == NULL && size != 0))
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
		if (bracketed && size - i >= 6 && memcmp(utf8 + i, end, 6) == 0) {
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
	*result        = prepared;
	*result_size   = used;
	return 0;
}

static bool
local_command(st_console_t *console, const struct st_console_event *event, enum st_term_disposition *disposition)
{
	uint16_t code;
	uint32_t modifiers;
	uint32_t capabilities = 0;

	if (event->type == ST_CONSOLE_EVENT_CLOSE) {
		*disposition = ST_TERM_EXIT_APPLICATION;
		return true;
	}
	if (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY && event->data.physical_key.pressed) {
		uint32_t physical_modifiers = event->data.physical_key.modifiers;
		uint16_t evdev              = event->data.physical_key.evdev;

		if ((physical_modifiers & (ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)) != 0
		    && (evdev == 35 || evdev == 45)) {
			*disposition = evdev == 45 ? ST_TERM_EXIT_APPLICATION : ST_TERM_RETURN_TO_PICKER;
			return true;
		}
		if ((physical_modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0 && evdev == 16
		    && st_console_capabilities(console, &capabilities) == 0
		    && (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0) {
			*disposition = ST_TERM_RETURN_TO_PICKER;
			return true;
		}
	}
	if (event->type != ST_CONSOLE_EVENT_KEY_CODE)
		return false;
	code      = event->data.key.code;
	modifiers = event->data.key.modifiers;
	if ((modifiers & ST_CONSOLE_MODIFIER_ALT) != 0 && (code == 'x' || code == 'X')) {
		*disposition = ST_TERM_EXIT_APPLICATION;
		return true;
	}
	if (((modifiers & ST_CONSOLE_MODIFIER_ALT) != 0 && (code == 'h' || code == 'H'))
	    || ((modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0 && (code == 'q' || code == 'Q' || code == 17)
	        && st_console_capabilities(console, &capabilities) == 0
	        && (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0)) {
		*disposition = ST_TERM_RETURN_TO_PICKER;
		return true;
	}
	return false;
}

static int
write_protocol(st_term_session_t *session, const void *bytes, size_t size, size_t *consumed)
{
	if (session->rip3 != NULL) {
		st_term_rip3_write(session->rip3, bytes, size);
		*consumed = size;
		return 0;
	}
	if (st_cterm_musicomm_state(session->cterm) == ST_CTERM_MUSICOMM_OFF) {
		if (session->rip154 != NULL)
			return st_rip154_feed(session->rip154, bytes, size, consumed);
		if (session->igs != NULL)
			return cterm_igs_feed(session->igs, bytes, size, consumed);
		if (session->big != NULL)
			return cterm_big_feed(session->big, bytes, size, consumed);
		if (session->skypix != NULL)
			return cterm_skypix_feed(session->skypix, bytes, size, consumed);
	}
	return st_cterm_write(session->cterm, bytes, size, consumed);
}

static int
write_terminal_wire(st_term_session_t *session, const uint8_t *bytes, size_t size)
{
	char   unicode[4096 * 4];
	size_t converted = 0;
	size_t consumed;
	int    status;

	if (st_cterm_musicomm_state(session->cterm) != ST_CTERM_MUSICOMM_OFF
	    || (session->config.graphics_mode != ST_CTERM_GRAPHICS_RIP_1
	        && session->config.graphics_mode != ST_CTERM_GRAPHICS_RIP_3)
	    || session->config.encoding == ST_CTERM_ENCODING_UTF8) {
		status = write_protocol(session, bytes, size, &consumed);
		/* An accepted asynchronous command can pause after this byte.
		 * The pump retains the following bytes and polls its owner. */
		if (status == IO_ERROR_IN_PROGRESS && consumed == size && st_term_session_device_paused(session))
			return 0;
		return status == 0 && consumed != size ? IO_ERROR_IO : status;
	}
	status =
	    st_term_rip_wire_to_utf8(unicode, sizeof(unicode), &converted, bytes, size, session->config.codepage);
	if (status != 0)
		return status;
	status = write_protocol(session, unicode, converted, &consumed);
	return status == 0 && consumed != converted ? IO_ERROR_IO : status;
}

static int
ooii_render(void *context, const uint8_t *bytes, size_t size)
{
	st_term_session_t *session = context;
	char               unicode[4096];

	/* Resource data is CP437 even when the connection uses another encoding. */
	while (size != 0) {
		size_t chunk     = size > sizeof(unicode) / 4 ? sizeof(unicode) / 4 : size;
		size_t converted = 0, consumed;
		int    status    = st_term_rip_wire_to_utf8(unicode, sizeof(unicode), &converted, bytes, chunk,
		    ST_CODEPAGE_CP437);
		if (status != 0)
			return status;
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
	if (session->config.suppress_responses)
		return 0;
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
	st_term_session_t    *session = context;
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
	if (session != NULL)
		st_ooii_set_mode(session->ooii, mode);
}

static int
write_character(st_term_session_t *session, const struct st_utf8_decode_event *event, const uint8_t *original,
    size_t length)
{
	bool consumed;
	bool     malformed = event->kind == ST_UTF8_DECODE_EVENT_MALFORMED;
	uint32_t scalar    = malformed ? UINT32_C(0xfffd) : event->data.scalar;
	int      status    = st_ooii_character(session->ooii, scalar, &consumed);
	if (status != 0 || consumed)
		return status;

	if (session->config.graphics_mode == ST_CTERM_GRAPHICS_OFF
	    && session->config.encoding == ST_CTERM_ENCODING_UTF8
	    && st_cterm_musicomm_state(session->cterm) == ST_CTERM_MUSICOMM_OFF) {
		size_t used;
		status = st_cterm_write_decoded(session->cterm, event, original, length, &used);
		return status == 0 && used != length ? IO_ERROR_IO : status;
	}
	/* Byte-oriented graphic command grammars own the original spelling. */
	status = write_terminal_wire(session, original, length);
	if (status == 0 && malformed) {
		st_term_rip3_flush(session->rip3);
		status = st_cterm_text_boundary(st_term_session_cterm(session));
	}
	return status;
}

static int
write_malformed(st_term_session_t *session, const struct st_utf8_decode_event *event)
{
	return write_character(session, event, event->data.malformed.bytes, event->data.malformed.size);
}

static int
write_graphics(st_term_session_t *session, const uint8_t *bytes, size_t size)
{
	while (size != 0) {
		bool utf8  = session->config.encoding == ST_CTERM_ENCODING_UTF8;
		bool split = st_utf8_decoder_incomplete(&session->wire_decoder);
		bool ooii  = st_ooii_mode(session->ooii) != 0;
		if (!split && !utf8 && !ooii)
			return write_terminal_wire(session, bytes, size);
		if (!split && !ooii && *bytes < 0x80) {
			/* ASCII has the same wire spelling and scalar value. */
			size_t ascii = 1;
			while (ascii < size && bytes[ascii] < 0x80)
				++ascii;
			int status = write_terminal_wire(session, bytes, ascii);
			if (status != 0)
				return status;
			bytes += ascii;
			size -= ascii;
			continue;
		}
		/* Once an octet grammar owns the input, even an apparent OO][
		 * marker or an incomplete UTF-8 lead is part of its payload. */
		if (!split
		    && (cterm_big_owns_bytes(session->big) || cterm_igs_owns_bytes(session->igs)
		        || (session->skypix != NULL && cterm_skypix_parser_owns_bytes(&session->skypix->parser))
		        || st_cterm_input_bytes_pending(st_term_session_cterm(session)))) {
			int status = write_terminal_wire(session, bytes, 1);
			if (status != 0)
				return status;
			++bytes;
			--size;
			continue;
		}
		const uint8_t *original = bytes;
		char                        encoded[4];
		size_t                      used = 1, length = 1;
		struct st_utf8_decode_event event = {.kind = ST_UTF8_DECODE_EVENT_SCALAR};
		if (!utf8)
			event.data.scalar = st_codepage_to_unicode(session->config.codepage, *bytes, false);
		else if (*bytes < 0x80 && !split)
			event.data.scalar = *bytes;
		else {
			int status = st_utf8_decoder_feed(&session->wire_decoder, bytes, size, &used, &event);
			if (status != 0 || event.kind == ST_UTF8_DECODE_EVENT_NONE)
				return status;
			length = used;
			if (event.kind == ST_UTF8_DECODE_EVENT_MALFORMED) {
				original = event.data.malformed.bytes;
				length   = event.data.malformed.size;
			}
			else if (split) {
				ptrdiff_t written =
				    st_utf8_encode_one(encoded, sizeof(encoded), event.data.scalar);
				if (written < 0)
					return (int)written;
				original = (const uint8_t *)encoded;
				length   = (size_t)written;
			}
		}
		int status = write_character(session, &event, original, length);
		if (status != 0)
			return status;
		bytes += used;
		size -= used;
	}
	return 0;
}

static int
write_wire_filtered(st_term_session_t *session, const uint8_t *bytes, size_t size)
{
	uint8_t replacement[ST_TERM_WIRE_REPLACEMENT_CAPACITY];
	size_t  run_start = 0;
	int     status;

	if (!session->wire_replaying && session->config.wire_capture != NULL
	    && (session->config.capture_active == NULL
	        || session->config.capture_active(session->config.capture_context))) {
		status = session->config.wire_capture(session->config.capture_context, bytes, size);
		if (status != 0)
			return status;
	}
	if (st_cterm_musicomm_state(session->cterm) != ST_CTERM_MUSICOMM_OFF)
		return write_terminal_wire(session, bytes, size);
	if (session->config.wire_filter == NULL
	    || (session->config.wire_filter_active != NULL
	        && !session->config.wire_filter_active(session->config.wire_filter_context)))
		return write_graphics(session, bytes, size);
	for (size_t i = 0; i < size; ++i) {
		bool   consumed         = false;
		size_t replacement_size = 0;

		status = session->config.wire_filter(session->config.wire_filter_context, bytes[i], &consumed,
		    replacement, sizeof(replacement), &replacement_size);
		if (status != 0)
			return status;
		if (replacement_size > sizeof(replacement) || (!consumed && replacement_size != 0))
			return IO_ERROR_INVALID_FORMAT;
		if (!consumed)
			continue;
		if (i > run_start) {
			status = write_graphics(session, bytes + run_start, i - run_start);
			if (status != 0)
				return status;
		}
		if (replacement_size != 0) {
			status = write_graphics(session, replacement, replacement_size);
			if (status != 0)
				return status;
		}
		run_start = i + 1;
	}
	return run_start < size ? write_graphics(session, bytes + run_start, size - run_start) : 0;
}

int
st_term_rip_wire_to_utf8(char *result, size_t capacity, size_t *result_size, const uint8_t *wire, size_t size,
    enum st_codepage codepage)
{
	size_t converted = 0;

	if (result_size == NULL || (result == NULL && (capacity != 0 || size != 0)) || (wire == NULL && size != 0)
	    || codepage < ST_CODEPAGE_CP437 || codepage >= ST_CODEPAGE_COUNT)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < size; i++) {
		/* RIP's ASCII introducer and delimiters are protocol bytes.
		 * CP437's display mapping of '|' to U+00A6 must not alter them. */
		uint32_t  scalar  = wire[i] < 0x80 ? wire[i] : st_codepage_to_unicode(codepage, wire[i], false);
		ptrdiff_t written = st_utf8_encode_one(result + converted, capacity - converted, scalar);

		if (written < 0)
			return (int)written;
		converted += (size_t)written;
	}
	*result_size = converted;
	return 0;
}

bool
st_term_session_keyboard_locked(const st_term_session_t *session)
{
	return session != NULL && st_term_rip3_keyboard_locked(session->rip3);
}

int
st_term_session_send_key(st_term_session_t *session, const struct st_console_event *event, bool *handled)
{
	if (session == NULL || event == NULL || handled == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*handled = false;
	if (session->wire_owner != NULL)
		return 0;
	if (st_term_rip3_key(session->rip3, event)) {
		*handled = true;
		return session->output_error;
	}
	switch (event->type) {
		case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
			if (session->config.encoding == ST_CTERM_ENCODING_CODEPAGE
			    && st_cterm_encoding(st_term_session_cterm(session)) == ST_CTERM_ENCODING_CODEPAGE) {
				*handled = st_cterm_handle_codepage_key(st_term_session_cterm(session),
				    event->data.codepage_byte);
				return session->output_error;
			}
			/* FALLTHROUGH */
		case ST_CONSOLE_EVENT_TEXT:
		{
			uint32_t codepoint = event->type == ST_CONSOLE_EVENT_TEXT ? event->data.text.codepoint
			                                                          : event->data.codepage_byte;
			char     utf8[4];

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
			*handled       = true;
			if (st_cterm_handle_text_key(st_term_session_cterm(session), codepoint))
				return session->output_error;
			return size < 0 ? IO_ERROR_INVALID_ENCODING
			                : st_term_session_send_text(session, utf8, (size_t)size);
		}
		case ST_CONSOLE_EVENT_KEY_CODE:
			*handled = st_cterm_handle_key(st_term_session_cterm(session), event->data.key.code,
			    event->data.key.modifiers);
			return session->output_error;
		case ST_CONSOLE_EVENT_PHYSICAL_KEY:
			*handled = st_cterm_handle_physical_keys(st_term_session_cterm(session), event, 1);
			return session->output_error;
		case ST_CONSOLE_EVENT_DOORWAY_KEY:
			*handled = st_cterm_handle_doorway_key(st_term_session_cterm(session),
			    event->data.doorway_key.scan_code);
			return session->output_error;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
}

static int
handle_event(st_term_session_t *session, const struct st_console_event *event,
    enum st_term_disposition *disposition, bool *finished)
{
	if (st_cterm_musicomm_state(st_term_session_cterm(session)) != ST_CTERM_MUSICOMM_OFF
	    && (event->type == ST_CONSOLE_EVENT_CODEPAGE_BYTE || event->type == ST_CONSOLE_EVENT_TEXT
	        || event->type == ST_CONSOLE_EVENT_KEY_CODE || event->type == ST_CONSOLE_EVENT_DOORWAY_KEY
	        || (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY && event->data.physical_key.pressed))) {
		if (st_cterm_musicomm_stop(st_term_session_cterm(session)))
			return 0;
	}
	st_cterm_observe_mouse(st_term_session_cterm(session), event);
	if (event->type == ST_CONSOLE_EVENT_FOCUS && !event->data.focus.focused)
		st_term_rip3_mouse_cancel(session->rip3, true);
	if (session->config.event_hook != NULL) {
		bool consumed;
		int  status = session->config.event_hook(session->config.event_hook_context, event, &consumed);

		if (status != 0 || consumed) {
			st_term_rip3_mouse_ignore(session->rip3, event);
			return status;
		}
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
		case ST_CONSOLE_EVENT_DOORWAY_KEY:
		{
			bool handled;
			return st_term_session_send_key(session, event, &handled);
		}
		case ST_CONSOLE_EVENT_MOUSE:
			if (!session->mouse_disabled) {
				if (!st_term_rip3_mouse(session->rip3, event))
					(void)st_cterm_handle_mouse(st_term_session_cterm(session), event);
				else
					st_cterm_sync_mouse_buttons(st_term_session_cterm(session),
					    event->data.mouse.buttons);
			}
			else {
				st_term_rip3_mouse_ignore(session->rip3, event);
				st_cterm_sync_mouse_buttons(st_term_session_cterm(session),
				    event->data.mouse.buttons);
			}
			return session->output_error;
		case ST_CONSOLE_EVENT_FOCUS:
			(void)st_cterm_handle_focus(st_term_session_cterm(session), event->data.focus.focused);
			return session->output_error;
		case ST_CONSOLE_EVENT_RESIZE:
		{
			if (session->config.graphics_mode == ST_CTERM_GRAPHICS_RIP_3
			    || session->config.graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_13
			    || session->config.graphics_mode == ST_CTERM_GRAPHICS_SKYPIX_31
			    || session->config.graphics_mode == ST_CTERM_GRAPHICS_IGS_221
			    || session->config.graphics_mode == ST_CTERM_GRAPHICS_BIG_103)
				return 0;
			uint32_t capabilities;
			int      status;

			status = st_console_capabilities(session->config.console, &capabilities);
			if (status != 0)
				return status;
			/* A host text-terminal resize moves a fixed BBS screen within the
		 * surround; it does not change the remote terminal's selected mode. */
			if ((capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0
			    && session->config.screen_mode != ST_SCREEN_MODE_CURRENT)
				return 0;
			status = st_cterm_resize(st_term_session_cterm(session), event->data.resize.columns,
			    event->data.resize.rows);
			if (status != 0)
				return status;
			return update_connection_geometry(session);
		}
		default:
			return 0;
	}
}

int
st_term_session_service(st_term_session_t *session, enum st_term_disposition *disposition, bool *finished)
{
	struct st_console_event event;
	ssize_t                 received  = IO_ERROR_WOULD_BLOCK;
	size_t                  read_size = sizeof(session->wire_pending);
	int                     status;

	if (session == NULL || disposition == NULL || finished == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*finished         = false;
	session->did_work = false;
	status            = st_connection_process_requests(session->config.connection);
	if (status != 0)
		return status;
	if (session->end_requested) {
		*disposition = session->exit_requested ? ST_TERM_EXIT_APPLICATION : ST_TERM_RETURN_TO_PICKER;
		*finished    = true;
		return 0;
	}
	/* Local event dispatch leaves the wire suffix buffered and the
	 * emulated serial clock running. */
	bool parser_was_waiting       = !session->igs_dispatch_pending && input_paused(session);
	session->igs_dispatch_pending = false;
	/* A stopped transport has no recipient for asynchronous reports.
	 * Still drain its received bytes and take the normal disconnect path. */
	if (st_connection_is_connected(session->config.connection)) {
		st_term_rip3_poll(session->rip3);
		status = st_term_session_poll_display(session);
		if (status != 0 || session->output_error != 0)
			return status != 0 ? status : session->output_error;
	}
	if (!session->suspended && session->transfer == NULL && session->throttle_bps != 0) {
		uint64_t now;
		bool parser_wait = parser_was_waiting || (!session->igs_dispatch_pending && input_paused(session));
		/* Credits are BPS*milliseconds: ten bits per character makes
		 * 10,000 credits one received byte.  Retain at most 100 ms of
		 * catch-up after an occupied UI tick. */
		uint64_t cap = (uint64_t)session->throttle_bps * 100;

		status = st_time_monotonic_ms(&now);
		if (status != 0)
			return status;
		bool idle = session->returned_position == session->returned_size
		            && session->wire_position == session->wire_size
		            && !st_connection_input_ready(session->config.connection);
		/* An idle wait can last arbitrarily long. Only one byte of credit
		 * carries into the first pass after the remote starts sending. */
		if (idle || session->throttle_idle || parser_wait || cap < UINT64_C(10000))
			cap = UINT64_C(10000);
		session->throttle_idle = idle;
		if (!parser_wait && session->throttle_last_ms != 0 && now >= session->throttle_last_ms) {
			uint64_t elapsed = now - session->throttle_last_ms;
			if (elapsed >= (cap + session->throttle_bps - 1) / session->throttle_bps)
				session->throttle_credit = cap;
			else
				session->throttle_credit += elapsed * session->throttle_bps;
		}
		session->throttle_last_ms = now;
		if (session->throttle_credit > cap)
			session->throttle_credit = cap;
		read_size = (size_t)(session->throttle_credit / UINT64_C(10000));
		if (read_size > sizeof(session->wire_pending))
			read_size = sizeof(session->wire_pending);
	}
	bool protocol_input = session->transfer != NULL && st_transfer_needs_input(session->transfer);
	if (session->transfer != NULL && st_transfer_done(session->transfer))
		read_size = 0;
	if (!protocol_input && !st_connection_is_connected(session->config.connection) && input_paused(session)) {
		status = finish(session);
		if (status != 0)
			return status;
	}
	bool paused = !protocol_input && input_paused(session);
	if (parser_was_waiting && !paused)
		session->did_work = true;
	if (session->transfer_reserved || session->wire_owner != NULL || paused)
		read_size = 0;
	if (protocol_input) {
		size_t space = st_transfer_input_space(session->transfer);
		if (read_size > space)
			read_size = space;
	}
	if ((!session->suspended || protocol_input) && read_size != 0
	    && session->returned_position == session->returned_size
	    && session->wire_position == session->wire_size) {
		received = st_connection_receive(session->config.connection, session->wire_pending, read_size, 0);
	}
	if (received < 0 && received != IO_ERROR_TIMED_OUT && received != IO_ERROR_WOULD_BLOCK) {
		if (session->transfer != NULL) {
			st_transfer_eof(session->transfer);
			received = IO_ERROR_WOULD_BLOCK;
		}
		else {
			session->remote_closed = !st_connection_is_connected(session->config.connection);
			status                 = st_connection_process_requests(session->config.connection);
			if (status != 0)
				return status;
			return (int)received;
		}
	}
	if (received > 0) {
		session->did_work      = true;
		session->wire_position = 0;
		session->wire_size     = (size_t)received;
	}
	if (protocol_input && received == 0)
		st_transfer_eof(session->transfer);
	/* A detector can claim the stream midway through this read. Recheck
	 * ownership after each ordinary byte, retaining any suffix on backpressure. */
	while (session->wire_owner == NULL && (!input_paused(session) || protocol_input)
	       && (!session->suspended || protocol_input) && !session->transfer_reserved
	       && (session->returned_position < session->returned_size
	           || session->wire_position < session->wire_size)) {
		bool     replay   = session->returned_position < session->returned_size;
		size_t  *position = replay ? &session->returned_position : &session->wire_position;
		size_t   size     = replay ? session->returned_size : session->wire_size;
		uint8_t *bytes    = replay ? session->returned_wire : session->wire_pending;
		protocol_input    = session->transfer != NULL && st_transfer_needs_input(session->transfer);
		if (session->transfer != NULL && st_transfer_done(session->transfer))
			break;
		if (protocol_input) {
			size_t count = size - *position, space = st_transfer_input_space(session->transfer);
			bool   skip = session->transfer_skip_lf && bytes[*position] == '\n';
			if (skip)
				count = 1;
			if (!skip && count > space)
				count = space;
			if (count == 0)
				break;
			session->transfer_skip_lf = false;
			if (!replay && session->config.wire_capture != NULL
			    && (session->config.capture_active == NULL
			        || session->config.capture_active(session->config.capture_context))) {
				status = session->config.wire_capture(session->config.capture_context,
				    bytes + *position, count);
				if (status != 0)
					return status;
			}
			size_t consumed =
			    skip ? count : st_transfer_feed(session->transfer, bytes + *position, count);
			if (consumed == 0)
				break;
			*position += consumed;
			session->did_work = true;
			continue;
		}
		if (session->rip3 != NULL && session->transfer_skip_lf) {
			/* A declined upload leaves the bytes following its CR in
			 * the ordinary wire queue. Preserve the CRLF boundary. */
			session->transfer_skip_lf = false;
			if (bytes[*position] == '\n') {
				if (!replay && session->config.wire_capture != NULL
				    && (session->config.capture_active == NULL
				        || session->config.capture_active(session->config.capture_context))) {
					status = session->config.wire_capture(session->config.capture_context,
					    bytes + *position, 1);
					if (status != 0)
						return status;
				}
				++*position;
				session->did_work = true;
				continue;
			}
		}
		if (session->transfer == NULL && session->throttle_bps != 0) {
			if (session->throttle_credit < UINT64_C(10000))
				break;
			session->throttle_credit -= UINT64_C(10000);
		}
		size_t count     = 1;
		bool   filtering = session->config.wire_filter != NULL
		                   && (session->config.wire_filter_active == NULL
		                       || session->config.wire_filter_active(session->config.wire_filter_context));
		if (!filtering && !session->config.throttle_supported && session->throttle_bps == 0
		    && session->config.graphics_mode == ST_CTERM_GRAPHICS_OFF
		    && session->config.screen_mode != ST_SCREEN_MODE_PRESTEL && st_ooii_mode(session->ooii) == 0)
			count = size - *position;
		if (!replay && session->config.screen_mode == ST_SCREEN_MODE_PRESTEL) {
			if (bytes[*position] == 12)
				session->prestel_frame_size = 0;
			if (session->prestel_frame_size < sizeof(session->prestel_frame))
				session->prestel_frame[session->prestel_frame_size++] = bytes[*position];
		}
		/* Inspect only original bytes. A banner can end mid-read; deliver its
		 * last byte through the hooks before the next byte enters OO][ mode. */
		bool banner = false;
		if (st_ooii_mode(session->ooii) == 0
		    && st_cterm_musicomm_state(session->cterm) == ST_CTERM_MUSICOMM_OFF) {
			for (size_t i = 0; i < count; ++i) {
				if (st_ooii_detect(session->ooii, bytes[*position + i])) {
					count  = i + 1;
					banner = true;
					break;
				}
			}
		}
		session->wire_replaying    = replay;
		session->input_dispatching = true;
		status                     = write_wire_filtered(session, bytes + *position, count);
		session->input_dispatching = false;
		session->wire_replaying    = false;
		if (status != 0)
			return status;
		if (banner)
			st_ooii_finish_banner(session->ooii);
		*position += count;
		session->did_work = true;
		protocol_input    = session->transfer != NULL && st_transfer_needs_input(session->transfer);
	}
	if (!session->suspended && session->wire_owner == NULL && session->transfer == NULL
	    && !session->transfer_reserved && session->wire_position == session->wire_size
	    && session->returned_position == session->returned_size
	    && st_connection_pending_raw(session->config.connection) == 0)
		st_cterm_musicomm_idle(session->cterm);
	/* Publish the consumed text batch before local keys/scripts inspect it. */
	st_term_rip3_flush(session->rip3);
	for (;;) {
		if (st_term_rip3_graphical_pending(session->rip3)) {
			/* Reserve before removing the next event. Allocation failure can
			 * abandon rendering and drain the original FIFO without losing a
			 * release/cancel or turning resource failure into disconnection. */
			struct deferred_event *saved = malloc(sizeof(*saved));
			if (saved == NULL) {
				st_term_rip3_graphical_failure(session->rip3);
				if (st_term_rip3_graphical_pending(session->rip3))
					return session->output_error;
				continue;
			}
			status = st_console_event_try(session->config.console, &saved->event);
			if (status != 0) {
				free(saved);
				break;
			}
			session->did_work = true;
			if (saved->event.type == ST_CONSOLE_EVENT_CLOSE
			    || local_command(session->config.console, &saved->event, disposition)) {
				event = saved->event;
				free(saved);
				if (event.type != ST_CONSOLE_EVENT_CLOSE) {
					*finished = true;
					return 0;
				}
			}
			else {
				saved->next          = NULL;
				saved->capture_token = 0;
				saved->embedded      = false;
				st_term_rip3_note_queued_input(session->rip3, &saved->event);
				if (session->deferred_tail != NULL)
					session->deferred_tail->next = saved;
				else
					session->deferred_head = saved;
				session->deferred_tail = saved;
				continue;
			}
		}
		else if (session->deferred_head != NULL) {
			struct deferred_event *saved = session->deferred_head;
			session->did_work            = true;
			event                        = saved->event;
			uint64_t token               = saved->capture_token;
			session->deferred_head       = saved->next;
			if (session->deferred_head == NULL)
				session->deferred_tail = NULL;
			if (saved->embedded)
				session->modal_tail_queued = false;
			else
				free(saved);
			if (token != 0) {
				st_term_rip3_captured_input(session->rip3, token, &event);
				continue;
			}
		}
		else {
			status = st_console_event_try(session->config.console, &event);
			if (status != 0)
				break;
		}
		session->did_work = true;
		status            = handle_event(session, &event, disposition, finished);
		if (status != 0 || *finished)
			return status;
		if (session->end_requested) {
			*disposition =
			    session->exit_requested ? ST_TERM_EXIT_APPLICATION : ST_TERM_RETURN_TO_PICKER;
			*finished = true;
			return 0;
		}
	}
	if (status != IO_ERROR_WOULD_BLOCK)
		return status;
	if (!session->suspended && session->returned_position == session->returned_size
	    && !st_connection_is_connected(session->config.connection)
	    && session->wire_position == session->wire_size
	    && st_connection_pending_raw(session->config.connection) == 0) {
		session->remote_closed = true;
		status                 = st_connection_process_requests(session->config.connection);
		if (status != 0)
			return status;
		*disposition = ST_TERM_RETURN_TO_PICKER;
		*finished = !session->auxiliary_active && session->transfer == NULL && !session->transfer_reserved;
	}
	return session->output_error;
}

int
st_term_session_wait(st_term_session_t *session, uint32_t timeout_ms)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (session->did_work || session->end_requested || session->igs_dispatch_pending || timeout_ms == 0)
		return 0;

	/* Some emulators and transfer workers expose progress only by polling.
	 * A ready console or connection interrupts even this short wait. */
	bool paused = input_paused(session);
	if (!paused && !session->suspended && session->wire_owner == NULL && session->transfer == NULL
	    && !session->transfer_reserved) {
		bool pending = session->wire_position < session->wire_size
		               || session->returned_position < session->returned_size
		               || st_connection_input_ready(session->config.connection);
		if (pending) {
			if (session->throttle_bps == 0 || session->throttle_credit >= UINT64_C(10000))
				return 0;
			uint64_t credit = UINT64_C(10000) - session->throttle_credit;
			uint64_t delay  = (credit + session->throttle_bps - 1) / session->throttle_bps;
			uint64_t now;
			int      status = st_time_monotonic_ms(&now);
			if (status != 0)
				return status;
			uint64_t elapsed = now >= session->throttle_last_ms ? now - session->throttle_last_ms : 0;
			if (elapsed >= delay)
				return 0;
			delay -= elapsed;
			if (delay < timeout_ms)
				timeout_ms = (uint32_t)delay;
		}
	}
	if (timeout_ms > 1
	    && (session->transfer != NULL || session->transfer_reserved || paused
	        || cterm_big_needs_poll(session->big) || cterm_igs_needs_poll(session->igs)
	        || st_cterm_needs_poll(st_term_session_cterm(session))))
		timeout_ms = 1;
	struct timespec deadline;
	int status = timeout_ms == UINT32_MAX ? 0 : st_time_utc_deadline_after_ms(&deadline, timeout_ms);
	if (status == 0)
		status = st_console_wait_activity(session->config.console,
		    st_connection_activity(session->config.connection),
		    timeout_ms == UINT32_MAX ? NULL : &deadline);
	return status == IO_ERROR_TIMED_OUT ? 0 : status;
}

int
st_term_session_pump(st_term_session_t *session, enum st_term_disposition *disposition, bool *finished)
{
	int status = st_term_session_service(session, disposition, finished);
	if (status == 0 && !*finished)
		status = st_term_session_wait(session, 1);
	return status;
}

void
st_term_session_set_auxiliary_active(st_term_session_t *session, bool active)
{
	if (session != NULL)
		session->auxiliary_active = active;
}

int
st_term_session_run(st_term_session_t *session, enum st_term_disposition *disposition)
{
	bool finished = false;
	int  status;

	if (session == NULL || disposition == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*disposition = ST_TERM_RETURN_TO_PICKER;
	while (!finished) {
		status = st_term_session_service(session, disposition, &finished);
		if (status == 0 && !finished)
			status = st_term_session_wait(session, UINT32_MAX);
		if (status != 0)
			return status;
	}
	return finish(session);
}

int
st_term_session_begin_transfer(st_term_session_t *session, st_transfer_t *transfer)
{
	if (session == NULL || transfer == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (session->transfer != NULL || session->wire_owner != NULL)
		return IO_ERROR_BUSY;
	session->transfer_was_binary = st_connection_binary_mode(session->config.connection);
	int status                   = st_transfer_needs_input(transfer)
	                                   ? st_connection_set_binary_mode(session->config.connection, true)
	                                   : 0;
	if (status != 0)
		return status;
	session->transfer          = transfer;
	session->transfer_reserved = false;
	return 0;
}

int
st_term_session_end_transfer(st_term_session_t *session)
{
	if (session == NULL || session->transfer == NULL)
		return 0;
	st_transfer_t *transfer = session->transfer;
	(void)st_transfer_join(transfer);
	int status = 0;
	if (st_transfer_needs_input(transfer)) {
		if (st_transfer_result(transfer) == 0) {
			/* Queued bytes precede any suffix not yet fed to the worker.
			 * A second transfer can begin within returned terminal output. */
			size_t   remaining = session->returned_size - session->returned_position;
			uint8_t *returned  = malloc(65536 + remaining);
			if (returned == NULL)
				status = IO_ERROR_OUT_OF_MEMORY;
			else {
				size_t count = st_transfer_take_input(transfer, returned, 65536);
				if (remaining != 0)
					memcpy(returned + count,
					    session->returned_wire + session->returned_position, remaining);
				free(session->returned_wire);
				session->returned_wire     = returned;
				session->returned_position = 0;
				session->returned_size     = count + remaining;
			}
		}
		else {
			/* The unread suffix has not passed the capture callback yet.
			 * Discarding protocol residue must not truncate ANSI capture. */
			if (session->wire_position < session->wire_size && session->config.wire_capture != NULL
			    && (session->config.capture_active == NULL
			        || session->config.capture_active(session->config.capture_context)))
				status = session->config.wire_capture(session->config.capture_context,
				    session->wire_pending + session->wire_position,
				    session->wire_size - session->wire_position);
			session->wire_position     = session->wire_size;
			session->returned_position = session->returned_size;
		}
	}
	session->transfer          = NULL;
	session->transfer_reserved = false;
	session->transfer_skip_lf  = false;
	int restored = st_connection_set_binary_mode(session->config.connection, session->transfer_was_binary);
	return status != 0 ? status : restored;
}

int
st_term_session_reserve_transfer(st_term_session_t *session, bool reserved)
{
	if (session != NULL) {
		if (reserved && session->wire_owner != NULL)
			return IO_ERROR_BUSY;
		session->transfer_reserved = reserved;
		if (!reserved)
			session->transfer_skip_lf = false;
	}
	return 0;
}

int
st_term_session_reserve_rip_transfer(st_term_session_t *session)
{
	if (session != NULL) {
		if (session->wire_owner != NULL)
			return IO_ERROR_BUSY;
		session->transfer_reserved = true;
		session->transfer_skip_lf  = true;
	}
	return 0;
}

void
st_term_session_cancel_rip_transfer(st_term_session_t *session)
{
	if (session != NULL) {
		session->transfer_reserved = false;
		if (session->rip3 == NULL)
			session->transfer_skip_lf = false;
	}
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
	if (session == NULL || owner == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (session->wire_owner != NULL || session->transfer != NULL || session->transfer_reserved
	    || session->input_dispatching)
		return IO_ERROR_BUSY;
	session->wire_owner = owner;
	return 0;
}

bool
st_term_session_release_wire(st_term_session_t *session, const void *owner)
{
	if (session == NULL || owner == NULL || session->wire_owner != owner)
		return false;
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
	*size = session->returned_size - session->returned_position + session->wire_size - session->wire_position
	        + st_connection_pending_raw(session->config.connection);
	return 0;
}

ssize_t
st_term_session_read_wire(st_term_session_t *session, const void *owner, void *bytes, size_t size, bool consume)
{
	size_t available;
	int    status = st_term_session_wire_available(session, owner, &available);
	if (status != 0 || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return 0;
	if (available == 0)
		return st_connection_is_connected(session->config.connection) ? IO_ERROR_WOULD_BLOCK : 0;
	if (size > available)
		size = available;
	size_t replay = session->returned_size - session->returned_position;
	if (replay > size)
		replay = size;
	size_t pending = session->wire_size - session->wire_position;
	if (pending > size - replay)
		pending = size - replay;
	uint8_t *destination = bytes;
	if (replay != 0)
		memcpy(destination, session->returned_wire + session->returned_position, replay);
	if (pending != 0)
		memcpy(destination + replay, session->wire_pending + session->wire_position, pending);
	size_t queued = size - replay - pending;
	if (queued != 0) {
		/* Read once: a transport worker can flush its queue between calls
		 * (rlogin urgent FLUSHWRITE). Never capture a peek then consume a
		 * potentially different suffix. */
		ssize_t count = consume ? st_connection_receive_raw(session->config.connection,
					      destination + replay + pending, queued, 0)
		                        : st_connection_peek_raw(session->config.connection,
					      destination + replay + pending, queued);
		if (count == IO_ERROR_WOULD_BLOCK)
			count = 0;
		if (count < 0)
			return count;
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
			if (status != 0)
				return status;
		}
	}
	if (replay + pending + queued == 0 && st_connection_is_connected(session->config.connection))
		return IO_ERROR_WOULD_BLOCK;
	return (ssize_t)(replay + pending + queued);
}
