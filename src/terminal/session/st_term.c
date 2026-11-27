/* Minimal connected-session loop for the modern CTerm component. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "cterm.h"
#include "cterm_big.h"
#include "cterm_igs.h"
#include "cterm_rip_asset.h"
#include "cterm_rip_asset_lookup.h"
#include "cterm_skypix.h"
#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_controller.h"
#include "st_ddofx_graphics.h"
#include "st_ddofx_package.h"
#include "st_ddofx_samples.h"
#include "st_graphics_input.h"
#include "st_ooii.h"
#include "st_rip154.h"
#include "st_term.h"
#include "st_term_rip3.h"
#include "st_threads.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_utf8.h"

/* Preserve post-hook input order while a graphical provider suspends.
 * Ordinary ANSI/RIP1 and completed RIP3 output do not use this queue. */
struct local_input {
	struct st_console_event event;
	uint64_t                capture_token;
	bool                    buttons_only, controller_consumed;
};

struct deferred_event {
	struct local_input     input;
	bool                   during_background_download;
	struct deferred_event *next;
};

struct st_term_session {
	struct st_term_config  config;
	struct st_ddofx        ddofx;
	int                           ddofx_saved_status;
	struct st_ddofx_package_batch packages;
	struct st_cterm_config ddofx_text_config;
	st_cterm_t            *cterm;
	struct cterm_big      *big;
	struct cterm_skypix   *skypix;
	struct cterm_igs      *igs;
	struct st_rip154      *rip154;
	st_ooii_t             *ooii;
	struct st_utf8_decoder wire_decoder;
	struct st_term_rip3   *rip3;
	struct deferred_event *deferred_head, *deferred_tail;
	struct local_input     modal_tail;
	int                    output_error;
	bool                   end_requested;
	bool                   igs_dispatch_pending;
	bool                   did_work;
	bool                   exit_requested;
	bool                   remote_closed;
	bool                   history_finalized;
	bool                   auxiliary_active;
	bool                   unfocused, controller_hook, controller_refresh;
	struct st_cterm_pads   pads;
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
	struct st_musicomm    *musicomm;
	bool                   musicomm_was_binary;
	st_transfer_t         *transfer;
	bool                   transfer_reserved, transfer_was_binary, wire_replaying;
	bool                   transfer_skip_lf;
	uint64_t               download_started_ms, download_row_due;
	char                   download_row[81];
	uint8_t               *returned_wire;
	size_t                 returned_position, returned_size;
	uint8_t                prestel_frame[65536];
	size_t                 prestel_frame_size;
};

static void
retain_prompt_tail(void *context, uint64_t token, const struct st_console_event *event)
{
	st_term_session_t *session = context;
	/* The modal editor can read the original gesture's release while Wren
	 * is on the stack. Offer it to the connected hooks as soon as they
	 * can run again, ahead of the next console event. */
	if (session->modal_tail.capture_token == 0)
		session->modal_tail = (struct local_input) {.event = *event, .capture_token = token};
}

void
st_term_session_observe_prompt_input(st_term_session_t *session, const struct st_console_event *event)
{
	if (session != NULL && event != NULL)
		st_term_rip3_observe_prompt_input(session->rip3, event);
}

static int ddofx_pass_key(void *, const struct st_console_event *);
static struct st_ddofx_sink ddofx_sink(st_term_session_t *session);
static int render_cp437_resource(void *, const uint8_t *, size_t, size_t *);

bool
st_term_session_device_paused(st_term_session_t *session)
{
	return session != NULL
	       && (st_ddofx_busy(&session->ddofx) || cterm_big_paused(session->big)
	           || (session->skypix != NULL && cterm_skypix_paused(session->skypix))
	           || cterm_igs_paused(session->igs));
}

bool
st_term_session_background_download(const st_term_session_t *session)
{
	return session != NULL && session->ddofx.background_download;
}

static void
download_status_row(st_term_session_t *session)
{
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0 || now < session->download_row_due)
		return;
	session->download_row_due = now + 100;

	struct st_transfer_inspection state;
	st_transfer_inspect(session->transfer, &state);
	int64_t total = state.progress.bytes_total, current = state.progress.bytes_current;
	double fraction = total > 0 && current > 0 ? (double)current / total : 0;
	unsigned filled = fraction >= 1 ? 20 : (unsigned)(20 * fraction);
	uint64_t elapsed = (now - session->download_started_ms) / 1000;
	char estimate[24] = "?", bar[21], line[81];
	if (fraction > 0)
		snprintf(estimate, sizeof(estimate), "%.0f", elapsed / fraction);
	memset(bar, 178, filled);
	memset(bar + filled, 176, 20 - filled);
	bar[20] = 0;
	snprintf(line, sizeof(line), "DOWNLOADING <PLEASE WAIT>:%s SIZE %" PRId64 " TIME %" PRIu64 " of %s",
	    bar, total, elapsed, estimate);
	if (!strcmp(line, session->download_row))
		return;
	struct st_console_cell cells[80];
	size_t length = strlen(line);
	for (size_t i = 0; i < 80; ++i)
		cells[i] = (struct st_console_cell){
		    .glyph = i < length ? (unsigned char)line[i] : ' ',
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 255),
		    .background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 168),
		};
	if (st_console_write_status_row(session->config.console, cells, 80) == 0) {
		(void)st_console_show_status_row(session->config.console, true);
		strcpy(session->download_row, line);
	}
}

static bool
ddofx_can_run(const st_term_session_t *session)
{
	return session->config.graphics_mode == ST_GRAPHICS_DDOFX_20 && session->wire_owner == NULL
	       && ((session->transfer == NULL && !session->transfer_reserved)
	           || (session->ddofx.background_download && !session->suspended));
}

int
st_term_session_poll_display(st_term_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (session->musicomm != NULL) {
		st_musicomm_poll(session->musicomm);
		return 0;
	}
	int status = session->ddofx.sleeper.warning ? 0 : st_cterm_poll(st_term_session_cterm(session));
	if (status == 0 && ddofx_can_run(session)) {
		const struct st_ddofx_sink sink = ddofx_sink(session);
		status = st_ddofx_poll(&session->ddofx, &sink);
		if (session->ddofx.background_download) {
			if (session->ddofx.graphics)
				download_status_row(session);
			else if (session->download_row[0]) {
				(void)st_console_write_status_row(session->config.console, NULL, 0);
				session->download_row[0] = 0;
			}
		}
	}
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

static int
release_musicomm(st_term_session_t *session)
{
	if (session->musicomm == NULL)
		return 0;
	st_musicomm_destroy(&session->musicomm);
	if (!st_connection_is_connected(session->config.connection))
		return 0;
	return st_connection_set_binary_mode(session->config.connection, session->musicomm_was_binary);
}

/* Lite uses the same indicator row and resize notification as DECSSDT.
 * Keep the first displaced layout, including a host-writable status row. */
static void
ddofx_lite_layout(st_term_session_t *session, unsigned mode, bool clear)
{
	st_cterm_t *term   = session->cterm;
	int         wanted = mode == 0 ? session->ddofx_saved_status : mode == 1 ? 1 : 0;
	if (wanted < 0 || !term)
		return;
	int old = st_cterm_status_display(term);
	if (old != wanted) {
		if ((!old && wanted && term->height < 2) || st_cterm_start_text(term)
		    || st_cterm_text_boundary(term))
			return;
		int active                  = term->status_display_active;
		term->status_display_active = 0;
		term->status_display_type   = wanted;
		term->status_display_cb(term, old, wanted, term->status_display_cbdata);
		(void)cterm_render_take_error(term);
		if (term->status_display_type != wanted) {
			term->status_display_active = active;
			return;
		}
		if (mode && session->ddofx_saved_status < 0)
			session->ddofx_saved_status = old;
		if (clear) {
			struct st_console_cell blank;
			uint32_t               fg, bg;
			cterm_render_attribute_colours(term, 0x0f, &fg, &bg);
			if (!cterm_render_make_cell(term, &blank, ' ', 0x0f, fg, bg,
				cterm_render_attribute_font(term, 0x0f), 0, 0)) {
				struct st_console_rect area = {term->x - 1, term->y - 1, term->width,
				    term->height};
				(void)st_console_fill(session->config.console, &area, &blank);
			}
			(void)st_cterm_position_text(term, 0, 0);
		}
	}
	if (!mode)
		session->ddofx_saved_status = -1;
}

static void
ddofx_lite_screen_mode(void *context, unsigned mode)
{
	ddofx_lite_layout(context, mode, true);
}

int
st_term_session_finish_display(st_term_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int pending = release_musicomm(session);
	if (pending == 0)
		pending = finish_wire_character(session);
	if (pending != 0)
		return pending;
	if (session->config.graphics_mode == ST_GRAPHICS_DDOFX_20)
		st_ddofx_reset(&session->ddofx);
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
	int pending = release_musicomm(session);
	if (pending == 0)
		pending = finish_wire_character(session);
	if (pending != 0)
		return pending;
	if (session->config.graphics_mode == ST_GRAPHICS_DDOFX_20) {
		st_ddofx_reset(&session->ddofx);
		ddofx_lite_layout(session, 0, false);
	}
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
	return session->musicomm == NULL
	       && (st_term_rip3_paused(session->rip3) || st_term_session_device_paused(session));
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

enum st_graphics_mode
st_term_profile_graphics_mode(const struct st_profile *profile)
{
	if (profile == NULL || profile->graphics_mode < ST_GRAPHICS_NONE
	    || profile->graphics_mode > ST_GRAPHICS_DDOFX_20)
		return ST_GRAPHICS_NONE;
	return (enum st_graphics_mode)profile->graphics_mode;
}

static enum st_cterm_graphics_mode
cterm_graphics_mode(enum st_graphics_mode mode)
{
	switch (mode) {
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
session_output(st_term_session_t *session, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size,
    bool text)
{
	if (session->wire_owner != NULL || session->ddofx.background_download)
		return;
	if (origin == ST_CTERM_OUTPUT_RESPONSE
	    && (session->config.suppress_responses || session->transfer != NULL || session->transfer_reserved))
		return;

	if (session->output_error != 0 || !st_connection_is_connected(session->config.connection))
		return;
	if (size != 0)
		session->did_work = true;
	if (text
	    && (session->config.graphics_mode == ST_GRAPHICS_RIP_1
	        || session->config.graphics_mode == ST_GRAPHICS_RIP_3)
	    && session->config.encoding == ST_CTERM_ENCODING_CODEPAGE)
		session->output_error = st_term_session_send_text(session, (const char *)bytes, size);
	else
		session->output_error = send_all(session->config.connection, bytes, size);
	if (!st_connection_is_connected(session->config.connection)
	    || (session->output_error == IO_ERROR_NOT_CONNECTED
	        && st_connection_auxiliary_alive(session->config.connection)))
		session->output_error = 0;
}

static void
cterm_output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	session_output(context, origin, bytes, size, true);
}

static void
protocol_reply(void *context, const uint8_t *bytes, size_t size)
{
	session_output(context, ST_CTERM_OUTPUT_RESPONSE, bytes, size, false);
}

static void
musicomm_warning(void *context, const char *message)
{
	st_term_session_t *session = context;
	if (session->config.cterm_diagnostic != NULL && session->config.cterm_log_level >= ST_CTERM_LOG_WARNING)
		session->config.cterm_diagnostic(session->config.cterm_diagnostic_context, ST_CTERM_LOG_WARNING,
		    message);
}

enum st_musicomm_state
st_term_session_musicomm_state(const st_term_session_t *session)
{
	return session == NULL ? ST_MUSICOMM_OFF : st_musicomm_state(session->musicomm);
}

int
st_term_session_start_musicomm(st_term_session_t *session)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (st_term_session_wire_owned(session) || session->transfer != NULL || session->transfer_reserved
	    || session->input_dispatching)
		return IO_ERROR_BUSY;
	int status = finish_wire_character(session);
	if (status != 0)
		return status;
	st_term_rip3_flush(session->rip3);
	st_cterm_t *display = st_term_session_cterm(session);
	status = st_cterm_finish(display);
	if (status != 0)
		return status;
	session->musicomm_was_binary = st_connection_binary_mode(session->config.connection);
	status                       = st_connection_set_binary_mode(session->config.connection, true);
	if (status != 0)
		return status;
	/* A RIP text window can be replaced while the connection is live. Stop
	 * any music it was playing and borrow the session root's channel 0. */
	if (display != session->cterm)
		(void)st_music_flush(st_cterm_music_player(display, false), 0);
	const struct st_musicomm_config config = {.audio = session->config.audio,
	    .player                                      = st_cterm_music_player(session->cterm, true),
	    .reply                                       = protocol_reply,
	    .warning                                     = musicomm_warning,
	    .context                                     = session};
	status                                 = st_musicomm_start(&session->musicomm, &config);
	if (status != 0)
		(void)st_connection_set_binary_mode(session->config.connection, session->musicomm_was_binary);
	return status;
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

static bool
rip3_escape(void *context, st_cterm_t *target, uint8_t byte)
{
	st_term_session_t *session = context;
	return st_term_rip3_escape(session->rip3, target, byte);
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

	if (!session->history_finalized && session->output_error == 0
	    && st_connection_is_connected(session->config.connection)) {
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

static bool
controller_available(void *context)
{
	st_term_session_t *session = context;
	return st_controllers_available(session->config.controllers);
}

static void sample_controllers(void *context);

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
	reserve_status = !config->hide_status && config->graphics_mode != ST_GRAPHICS_RIP_3;
	if (reserve_status && rows < 2)
		return IO_ERROR_INVALID_ARGUMENT;
	session = calloc(1, sizeof(*session));
	if (session == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	session->config = *config;
	session->ddofx_saved_status = -1;
	if (config->graphics_mode == ST_GRAPHICS_DDOFX_20)
		st_ddofx_reset(&session->ddofx);
	if (config->graphics_mode == ST_GRAPHICS_RIP_3) {
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
	session->pads = (struct st_cterm_pads) {
	    .next_id = 1, .context = session, .available = controller_available, .sample = sample_controllers};
	session->controller_refresh = true;
	session->ddofx.sample_controllers = sample_controllers;
	session->ddofx.controller_context = session;
	const struct st_cterm_config cterm_config = {
	    .pads             = config->controllers ? &session->pads : NULL,
	    .console          = config->console,
	    .fonts            = config->fonts,
	    .audio            = config->audio,
	    .hyperlinks       = config->hyperlinks,
	    .emulation        = (config->graphics_mode == ST_GRAPHICS_SKYPIX_13
	                            || config->graphics_mode == ST_GRAPHICS_SKYPIX_31
	                            || config->graphics_mode == ST_GRAPHICS_BIG_103)
	                            ? ST_CTERM_ANSI_BBS
	                            : profile_emulation(config->screen_mode),
	    .encoding         = (config->graphics_mode == ST_GRAPHICS_RIP_1
	                            || config->graphics_mode == ST_GRAPHICS_RIP_3)
	                            ? ST_CTERM_ENCODING_UTF8
	                            : config->encoding,
	    .graphics_mode    = cterm_graphics_mode(config->graphics_mode),
	    .escape_extension = config->graphics_mode == ST_GRAPHICS_RIP_3 ? rip3_escape : NULL,
	    .escape_context   = session,

	    .apc_directory        = config->apc_directory,
	    .application_version  = config->application_version,
	    .codepage             = config->codepage,
	    .palette_mapping      = mode == NULL ? NULL : palette_mapping,
	    .petscii_machine      = profile_petscii_machine(config->screen_mode),
	    .font_selector        = config->font_selector,
	    .terminal_rows        = reserve_status ? rows - 1 : rows,
	    .status_line          = reserve_status,
	    .expand_lf            = config->expand_lf,
	    .force_lcf            = config->force_lcf,
	    .yellow_is_yellow     = config->yellow_is_yellow,
	    .palette_override     = config->palette_size == 0 ? NULL : palette_override,
	    .scrollback_lines     = config->history == NULL ? 0 : st_session_history_capacity(config->history),
	    .output               = cterm_output,
	    .output_context       = session,
	    .size_changed         = cterm_size,
	    .size_context         = session,
	    .speed_query          = cterm_speed_query,
	    .speed_changed        = cterm_speed_changed,
	    .speed_context        = session,
	    .input_filter         = config->input_filter,
	    .input_filter_active  = config->input_filter_active,
	    .input_filter_context = config->input_filter_context,
	    .capture              = config->capture,
	    .capture_active       = config->capture_active,
	    .capture_context      = config->capture_context,
	    .log_level            = config->cterm_log_level,
	    .diagnostic           = config->cterm_diagnostic,
	    .diagnostic_context   = config->cterm_diagnostic_context,
	};
	if (config->graphics_mode == ST_GRAPHICS_DDOFX_20) {
		session->ddofx_text_config = cterm_config;
		/* Both overrides above are stack arrays belonging to the Lite mode. */
		session->ddofx_text_config.palette_mapping  = NULL;
		session->ddofx_text_config.palette_override = NULL;
	}
	if (config->graphics_mode == ST_GRAPHICS_BIG_103)
		status = cterm_big_open(&session->big, &session->cterm, &cterm_config);
	else if (config->graphics_mode == ST_GRAPHICS_RIP_1)
		status = st_rip154_open(&session->rip154, &session->cterm, &cterm_config,
		    &(struct st_rip154_services) {.rip_directory = config->rip_directory,
			.rip_prompt                              = config->rip_prompt,
			.rip_prompt_context                      = config->rip_prompt_context,
			.rip_choice                              = config->rip_choice,
			.rip_choice_context                      = config->rip_choice_context,
			.rip_transfer                            = config->rip_transfer,
			.rip_transfer_context                    = config->rip_transfer_context});
	else if (config->graphics_mode == ST_GRAPHICS_IGS_221)
		status = cterm_igs_open(&session->igs, &session->cterm, &cterm_config, true,
		    igs_loop_service_pending, session);
	else if (config->graphics_mode == ST_GRAPHICS_SKYPIX_13
	         || config->graphics_mode == ST_GRAPHICS_SKYPIX_31)
		status = cterm_skypix_open(&session->skypix, &session->cterm, &cterm_config,
		    &(struct st_skypix_config) {.kick13 = config->graphics_mode == ST_GRAPHICS_SKYPIX_13,
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
	if (config->graphics_mode == ST_GRAPHICS_RIP_3) {
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

	if (session->history_finalized)
		return;
	/* Freeze the session before disconnected UI can draw on its console. */
	session->history_finalized = true;
	if (!session->config.history)
		return;
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
		bool has_status = session->ddofx_saved_status >= 0 ? st_cterm_status_display(session->cterm) != 0
		                                                   : !session->config.hide_status;
		status          = st_session_history_publish(session->config.history, cells, columns, rows,
		    session->config.screen_mode, has_status, pixels);
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
	ddofx_lite_layout(*session, 0, false);
	st_ddofx_reset(&(*session)->ddofx);
	st_ooii_destroy((*session)->ooii);
	st_term_rip3_destroy((*session)->rip3);
	st_rip154_destroy((*session)->rip154);
	cterm_igs_destroy((*session)->igs);
	cterm_skypix_destroy((*session)->skypix);
	cterm_big_destroy((*session)->big);
	st_cterm_destroy(&(*session)->cterm);
	st_cterm_pads_clear(&(*session)->pads);
	st_controllers_forget_listener((*session)->config.controllers);
	free((*session)->returned_wire);
	while ((*session)->deferred_head != NULL) {
		struct deferred_event *event = (*session)->deferred_head;
		(*session)->deferred_head    = event->next;
		free(event);
	}
	free(*session);
	*session = NULL;
}

st_cterm_t *
st_term_session_cterm(st_term_session_t *session)
{
	if (session != NULL && session->ddofx.graphics != NULL)
		return st_ddofx_graphics_text(session->ddofx.graphics);
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
	if (session == NULL || session->ddofx.graphics != NULL)
		return false;
	return session->rip3 != NULL ? st_term_rip3_status_visible(session->rip3)
	                             : st_cterm_status_display(session->cterm) == 1;
}

bool
st_term_session_text_view(st_term_session_t *session, st_console_t *display, struct st_term_text_view *view)
{
	if (session != NULL && session->config.console == display && view != NULL
	    && session->ddofx.graphics != NULL) {
		view->console = st_ddofx_graphics_view(session->ddofx.graphics, &view->x, &view->y,
		    &view->cell_width, &view->cell_height, &view->source_cells);
		return view->console != NULL;
	}
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
	ddofx_lite_layout(session, 0, false);
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
		if (session->suspended != suspended && session->ddofx.background_download) {
			(void)st_console_show_status_row(session->config.console, false);
			session->download_row[0] = 0;
			session->download_row_due = 0;
		}
		if (session->suspended != suspended) {
			st_ddofx_slide_suspend(&session->ddofx, suspended);
			st_ddofx_sleep_close(&session->ddofx);
			session->ddofx.sleeper.suspended = suspended;
			st_ddofx_present_origin(&session->ddofx);
			if (!suspended)
				st_ddofx_sleep_arm(&session->ddofx);
		}
		session->suspended = suspended;
		session->ddofx.controller_paused = suspended || session->unfocused;
		st_ddofx_pointer_pause(&session->ddofx,
		    session->suspended || session->unfocused || session->mouse_disabled);
		if (suspended) {
			st_ddofx_mouse_cancel(&session->ddofx, 0);
			session->controller_refresh = true;
			st_cterm_pad_pause(st_term_session_cterm(session), true);
		}
		st_term_rip3_suspend_status(session->rip3, suspended);
	}
}

void
st_term_session_set_mouse_disabled(st_term_session_t *session, bool disabled)
{
	if (session != NULL) {
		session->mouse_disabled = disabled;
		st_ddofx_pointer_pause(&session->ddofx,
		    session->suspended || session->unfocused || session->mouse_disabled);
		if (disabled) {
			st_ddofx_mouse_cancel(&session->ddofx, 0);
			st_term_rip3_mouse_cancel(session->rip3, false);
		}
	}
}

int
st_term_session_mouse_mode(st_term_session_t *session)
{
	if (session == NULL)
		return 0;
	if ((session->ddofx.graphics && ddofx_can_run(session)) || st_term_rip3_mouse_active(session->rip3))
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
	if (session->wire_owner != NULL || session->ddofx.background_download)
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

static char *
ddofx_directory(const char *root, const char *name)
{
	struct fs_info info;
	if (root == NULL || fs_get_link_info(&info, root) != 0 || info.kind != FS_ENTRY_DIRECTORY)
		return NULL;
	char *path = cterm_rip_asset_path(root, name, false);
	if (path == NULL)
		return NULL;
	int status = fs_get_link_info(&info, path);
	if (status == IO_ERROR_NOT_FOUND) {
		free(path);
		path = cterm_rip_asset_case_path(root, name);
		if (path == NULL)
			return NULL;
		status = fs_get_link_info(&info, path);
	}
	if (status == 0 && info.kind == FS_ENTRY_DIRECTORY)
		return path;
	free(path);
	return NULL;
}

static int
ensure_package_directory(const char *path)
{
	int status = fs_create_directory(path);
	if (status == IO_ERROR_ALREADY_EXISTS) {
		struct fs_info info;
		status = fs_get_link_info(&info, path);
		if (!status && info.kind != FS_ENTRY_DIRECTORY)
			status = IO_ERROR_PERMISSION_DENIED;
	}
	return status;
}

static int
package_member_path(char **result, const char *directory, const char *member)
{
	return cterm_rip_asset_transfer_path(result, directory, member, false);
}

static int
package_directory(char **result, const char *root, const char *category)
{
	int status = ensure_package_directory(root);
	if (status)
		return status;
	*result = ddofx_directory(root, category);
	if (*result)
		return 0;
	status = path_join_alloc(result, root, category);
	return status ? status : ensure_package_directory(*result);
}

void
st_term_session_configure_download(st_term_session_t *session, struct st_transfer_config *config)
{
	if (session && session->config.graphics_mode == ST_GRAPHICS_DDOFX_20 && session->config.ddofx_directory) {
		session->packages = (struct st_ddofx_package_batch) {
		    .root      = session->config.ddofx_directory,
		    .directory = package_directory,
		    .resolve   = package_member_path,
		};
		config->downloaded         = st_ddofx_remember_package;
		config->finish_downloads   = st_ddofx_install_packages;
		config->downloaded_context = &session->packages;
	}
}

static char *
ddofx_asset_directory(st_term_session_t *session, const char *category, const char *client)
{
	char *directory = ddofx_directory(session->config.ddofx_directory, category);
	if (client == NULL || directory == NULL)
		return directory;
	char *subdirectory = ddofx_directory(directory, client);
	free(directory);
	return subdirectory;
}

static bool
ddofx_client_exists(void *context, const char *name)
{
	char *directory = ddofx_asset_directory(context, "TERMS", name);
	if (directory == NULL)
		return false;
	free(directory);
	return true;
}

static FILE *
ddofx_open_asset(void *context, const char *category, const char *client, const char *filename)
{
	char *directory = ddofx_asset_directory(context, category, client);
	FILE *file      = cterm_rip_asset_open_read(directory, filename, false);
	free(directory);
	return file;
}

static char *
ddofx_theme_path(void *context, const char *filename)
{
	st_term_session_t *session = context;
	char *directory = NULL, *path = NULL;
	if (session->config.ddofx_directory
	    && package_directory(&directory, session->config.ddofx_directory, "BBSTHEME") == 0) {
		if (!filename)
			return directory;
		(void)package_member_path(&path, directory, filename);
	}
	free(directory);
	return path;
}

static int
ddofx_text(void *context, const uint8_t *bytes, size_t size, size_t *consumed)
{
	st_term_session_t *session = context;
	return st_cterm_write(st_term_session_cterm(session), bytes, size, consumed);
}

static void
ddofx_disconnect(void *context)
{
	st_term_session_request_end(context, false);
}

static struct st_ddofx_sink
ddofx_sink(st_term_session_t *session)
{
	return (struct st_ddofx_sink) {
	    .context          = session,
	    .text             = ddofx_text,
	    .resource_text    = render_cp437_resource,
	    .disconnect       = ddofx_disconnect,
	    .cache_directory  = session->config.ddofx_directory,
	    .available_memory = st_ddofx_available_memory,
	    .alert            = session->config.graphics_alert,
	    .edit_text        = session->config.graphics_edit_text,
	    .pick_save_path   = session->config.graphics_pick_save_path,
	    .ui_context       = session->config.graphics_ui_context,
	    .pass_key         = ddofx_pass_key,
	    .mouse_disabled   = session->mouse_disabled,
	    .text_config      = &session->ddofx_text_config,
	    .lite_terminal    = session->cterm,
	    .lite_screen_mode = ddofx_lite_screen_mode,
	    .reply            = protocol_reply,
	    .open_asset       = ddofx_open_asset,
	    .theme_path       = ddofx_theme_path,
	    .client_exists    = ddofx_client_exists,
	    .audio            = session->config.audio,
	};
}

static int
write_protocol(st_term_session_t *session, const void *bytes, size_t size, size_t *consumed)
{
	if (session->config.graphics_mode == ST_GRAPHICS_DDOFX_20) {
		const struct st_ddofx_sink sink = ddofx_sink(session);
		return st_ddofx_feed(&session->ddofx, &sink, bytes, size, consumed);
	}
	if (session->rip3 != NULL) {
		st_term_rip3_write(session->rip3, bytes, size);
		*consumed = size;
		return 0;
	}
	if (session->rip154 != NULL)
		return st_rip154_feed(session->rip154, bytes, size, consumed);
	if (session->igs != NULL)
		return cterm_igs_feed(session->igs, bytes, size, consumed);
	if (session->big != NULL)
		return cterm_big_feed(session->big, bytes, size, consumed);
	if (session->skypix != NULL)
		return cterm_skypix_feed(session->skypix, bytes, size, consumed);
	return st_cterm_write(session->cterm, bytes, size, consumed);
}

static int
write_terminal_wire(st_term_session_t *session, const uint8_t *bytes, size_t size)
{
	char   unicode[4096 * 4];
	size_t converted = 0;
	size_t consumed;
	int    status;

	if ((session->config.graphics_mode != ST_GRAPHICS_RIP_1
	        && session->config.graphics_mode != ST_GRAPHICS_RIP_3)
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
render_cp437_resource(void *context, const uint8_t *bytes, size_t size, size_t *used)
{
	st_term_session_t *session = context;
	char               unicode[4096];

	*used = 0;
	while (*used < size) {
		size_t chunk = size - *used;
		if (chunk > sizeof(unicode) / 4)
			chunk = sizeof(unicode) / 4;
		size_t converted = 0, consumed;
		int status = st_term_rip_wire_to_utf8(unicode, sizeof(unicode), &converted, bytes + *used, chunk,
		    ST_CODEPAGE_CP437);
		if (status != 0)
			return status;
		status = st_cterm_write_utf8_input(session->ddofx.graphics
		                                       ? st_ddofx_graphics_text(session->ddofx.graphics)
		                                       : session->cterm,
		    unicode, converted, &consumed);
		if (consumed == converted)
			*used += chunk;
		else {
			/* Every source byte produced one scalar. Preserve the byte
			 * position if the emulator yielded at a control boundary. */
			for (size_t i = 0; i < consumed; ++i)
				if (((uint8_t)unicode[i] & 0xc0) != 0x80)
					++*used;
			return status;
		}
		if (status != 0)
			return status;
	}
	return 0;
}

static int
ooii_render(void *context, const uint8_t *bytes, size_t size)
{
	size_t used;
	int    status = render_cp437_resource(context, bytes, size, &used);
	return status == 0 && used != size ? IO_ERROR_IO : status;
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

	if (session->config.graphics_mode == ST_GRAPHICS_NONE
	    && session->config.encoding == ST_CTERM_ENCODING_UTF8) {
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
		    && (session->ddofx.wire.phase != ST_DDO_TEXT || st_rip154_owns_bytes(session->rip154)
		        || st_term_rip3_owns_bytes(session->rip3) || cterm_big_owns_bytes(session->big)
		        || cterm_igs_owns_bytes(session->igs)
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
	return session != NULL && (session->ddofx.keys_locked || st_term_rip3_keyboard_locked(session->rip3));
}

static bool
musicomm_stop_key(st_term_session_t *session, const struct st_console_event *event)
{
	return session->musicomm != NULL
	       && (event->type == ST_CONSOLE_EVENT_CODEPAGE_BYTE || event->type == ST_CONSOLE_EVENT_TEXT
	           || event->type == ST_CONSOLE_EVENT_KEY_CODE || event->type == ST_CONSOLE_EVENT_DOORWAY_KEY
	           || (event->type == ST_CONSOLE_EVENT_PHYSICAL_KEY && event->data.physical_key.pressed))
	       && st_musicomm_stop(session->musicomm);
}

static int
deliver_terminal_key(st_term_session_t *session, const struct st_console_event *event, bool *handled)
{
	if (session->wire_owner != NULL)
		return 0;
	switch (event->type) {
		case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
			if (session->config.encoding == ST_CTERM_ENCODING_CODEPAGE
			    && st_cterm_encoding(st_term_session_cterm(session)) == ST_CTERM_ENCODING_CODEPAGE) {
				*handled = st_graphics_handle_codepage_key(st_term_session_cterm(session),
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
			if (st_graphics_handle_text_key(st_term_session_cterm(session), codepoint))
				return session->output_error;
			return size < 0 ? IO_ERROR_INVALID_ENCODING
			                : st_term_session_send_text(session, utf8, (size_t)size);
		}
		case ST_CONSOLE_EVENT_KEY_CODE:
			*handled = st_graphics_handle_key(st_term_session_cterm(session), event->data.key.code,
			    event->data.key.modifiers);
			return session->output_error;
		case ST_CONSOLE_EVENT_PHYSICAL_KEY:
			*handled = st_cterm_handle_physical_keys(st_term_session_cterm(session), event, 1);
			return session->output_error;
		case ST_CONSOLE_EVENT_DOORWAY_KEY:
			*handled = st_graphics_handle_doorway_key(st_term_session_cterm(session),
			    event->data.doorway_key.scan_code);
			return session->output_error;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
}

static int
ddofx_pass_key(void *context, const struct st_console_event *event)
{
	bool handled = false;
	if (st_term_session_background_download(context))
		return 0;
	return deliver_terminal_key(context, event, &handled);
}

static int
deliver_key(st_term_session_t *session, const struct st_console_event *event, bool *handled)
{
	if (session->wire_owner != NULL)
		return 0;
	if ((ddofx_can_run(session) && st_ddofx_key(&session->ddofx, event)) || session->ddofx.background_download
	    || st_term_rip3_key(session->rip3, event)) {
		*handled = true;
		return session->output_error;
	}
	return deliver_terminal_key(session, event, handled);
}

static void
sync_buttons(st_term_session_t *session, uint32_t buttons)
{
	st_ddofx_mouse_cancel(&session->ddofx, buttons);
	st_term_rip3_sync_mouse_buttons(session->rip3, buttons);
	st_graphics_sync_mouse_buttons(st_term_session_cterm(session), buttons);
}

static void
deliver_controller(st_term_session_t *session, const struct st_controller_event *event, bool consumed)
{
	if (session->config.graphics_mode == ST_GRAPHICS_DDOFX_20)
		st_ddofx_controller(&session->ddofx, event, consumed);
	st_graphics_handle_controller(st_term_session_cterm(session), event, consumed);
}

static int
deliver_input(st_term_session_t *session, const struct local_input *input, bool *handled)
{
	const struct st_console_event *event = &input->event;
	if (input->buttons_only) {
		sync_buttons(session, event->data.mouse.buttons);
		return session->output_error;
	}
	if (input->capture_token != 0) {
		st_term_rip3_captured_input(session->rip3, input->capture_token, event);
		return session->output_error;
	}
	switch (event->type) {
		case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
		case ST_CONSOLE_EVENT_TEXT:
		case ST_CONSOLE_EVENT_KEY_CODE:
		case ST_CONSOLE_EVENT_PHYSICAL_KEY:
		case ST_CONSOLE_EVENT_DOORWAY_KEY:
			return deliver_key(session, event, handled);
		case ST_CONSOLE_EVENT_MOUSE:
			if (!session->mouse_disabled) {
				if (session->ddofx.graphics && ddofx_can_run(session)) {
					st_ddofx_mouse(&session->ddofx, event);
					st_cterm_sync_mouse_buttons(st_term_session_cterm(session), event->data.mouse.buttons);
				}
				else if (!st_term_rip3_mouse(session->rip3, event))
					(void)st_graphics_handle_mouse(st_term_session_cterm(session), event);
				else
					st_cterm_sync_mouse_buttons(st_term_session_cterm(session),
					    event->data.mouse.buttons);
			}
			else
				sync_buttons(session, event->data.mouse.buttons);
			return session->output_error;
		case ST_CONSOLE_EVENT_CONTROLLER:
			deliver_controller(session, &event->data.controller, input->controller_consumed);
			return session->output_error;
		case ST_CONSOLE_EVENT_FOCUS:
			session->unfocused = !event->data.focus.focused;
			session->ddofx.controller_paused = session->unfocused || session->suspended;
			st_ddofx_pointer_pause(&session->ddofx,
			    session->suspended || session->unfocused || session->mouse_disabled);
			if (session->unfocused) {
				session->controller_refresh = true;
				st_ddofx_mouse_cancel(&session->ddofx, 0);
				st_cterm_pad_pause(st_term_session_cterm(session), true);
				st_term_rip3_mouse_cancel(session->rip3, true);
			}
			(void)st_cterm_handle_focus(st_term_session_cterm(session), event->data.focus.focused);
			return session->output_error;
		case ST_CONSOLE_EVENT_RESIZE:
		{
			if (session->ddofx.graphics != NULL || session->config.graphics_mode == ST_GRAPHICS_RIP_3
			    || session->config.graphics_mode == ST_GRAPHICS_SKYPIX_13
			    || session->config.graphics_mode == ST_GRAPHICS_SKYPIX_31
			    || session->config.graphics_mode == ST_GRAPHICS_IGS_221
			    || session->config.graphics_mode == ST_GRAPHICS_BIG_103)
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

static int
deliver_deferred(st_term_session_t *session)
{
	struct deferred_event *saved = session->deferred_head;
	struct local_input     input = saved->input;
	session->deferred_head       = saved->next;
	if (session->deferred_head == NULL)
		session->deferred_tail = NULL;
	bool expired = saved->during_background_download && !session->ddofx.background_download;
	free(saved);
	if (expired)
		return 0;

	bool handled = false;
	return deliver_input(session, &input, &handled);
}

static bool
graphics_input_pending(const st_term_session_t *session)
{
	return session->ddofx.in_key || session->ddofx.in_mouse || st_term_rip3_graphical_pending(session->rip3);
}

static int
forward_input(st_term_session_t *session, const struct local_input *input, bool *handled)
{
	if (session->deferred_head == NULL && !graphics_input_pending(session))
		return deliver_input(session, input, handled);

	struct deferred_event *saved = malloc(sizeof(*saved));
	if (saved == NULL) {
		/* Preserve queued input order. A running DDO callback may be
		 * waiting on a timer, so it cannot be drained synchronously. */
		for (;;) {
			if (session->ddofx.in_key) {
				*handled = true;
				return 0;
			}
			if (st_term_rip3_graphical_pending(session->rip3))
				st_term_rip3_graphical_failure(session->rip3);
			if (!session->deferred_head)
				break;
			int status = deliver_deferred(session);
			if (status != 0)
				return status;
		}
		return deliver_input(session, input, handled);
	}
	*saved = (struct deferred_event) {
	    .input = *input, .during_background_download = session->ddofx.background_download
	};
	st_term_rip3_note_queued_input(session->rip3, &input->event);
	if (session->deferred_tail != NULL)
		session->deferred_tail->next = saved;
	else
		session->deferred_head = saved;
	session->deferred_tail = saved;
	*handled = true;
	return 0;
}

int
st_term_session_send_key(st_term_session_t *session, const struct st_console_event *event, bool *handled)
{
	if (session == NULL || event == NULL || handled == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*handled = false;
	switch (event->type) {
		case ST_CONSOLE_EVENT_TEXT:
			if (!st_unicode_scalar_valid(event->data.text.codepoint))
				return IO_ERROR_INVALID_ENCODING;
			break;
		case ST_CONSOLE_EVENT_KEY_CODE:
		case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
		case ST_CONSOLE_EVENT_DOORWAY_KEY:
		case ST_CONSOLE_EVENT_PHYSICAL_KEY:
			break;
		default:
			return IO_ERROR_INVALID_ARGUMENT;
	}
	if (session->wire_owner != NULL)
		return 0;
	if (musicomm_stop_key(session, event)) {
		*handled = true;
		return session->output_error;
	}
	return forward_input(session, &(struct local_input) {.event = *event}, handled);
}

int
st_term_session_sync_mouse_buttons(st_term_session_t *session, uint32_t buttons)
{
	if (session == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	const struct local_input input = {
	    .buttons_only = true,
	    .event        = {.type = ST_CONSOLE_EVENT_MOUSE, .data.mouse.buttons = buttons}
        };
	bool handled = false;
	return forward_input(session, &input, &handled);
}

static int
handle_event(st_term_session_t *session, const struct local_input *input, enum st_term_disposition *disposition,
    bool *finished)
{
	const struct st_console_event *event = &input->event;
	if (session->config.event_hook != NULL) {
		struct st_term_event_result result = {0};
		if (event->type == ST_CONSOLE_EVENT_MOUSE)
			result.mouse_buttons = event->data.mouse.buttons;
		int status = session->config.event_hook(session->config.event_hook_context, event, &result);
		if (status != 0)
			return status;
		if (result.consumed && event->type == ST_CONSOLE_EVENT_CONTROLLER) {
			struct local_input filtered  = *input;
			filtered.controller_consumed = true;
			bool handled                 = false;
			return forward_input(session, &filtered, &handled);
		}
		if (result.consumed)
			return event->type == ST_CONSOLE_EVENT_MOUSE
				   ? st_term_session_sync_mouse_buttons(session, result.mouse_buttons)
				   : 0;
	}
	if (musicomm_stop_key(session, event))
		return session->output_error;
	if (local_command(session->config.console, event, disposition)) {
		*finished = true;
		return 0;
	}
	bool handled = false;
	return forward_input(session, input, &handled);
}

/* Snapshot a poll before invoking user code. A Wren hook can enter the
 * controller mapper and poll the same devices again. */
struct controller_batch {
	struct st_controller_event *records;
	size_t                      count, capacity;
	bool                        failed;
};

static void
collect_controller(void *context, const struct st_controller_event *record)
{
	struct controller_batch *batch = context;
	if (batch->failed)
		return;
	if (batch->count == batch->capacity) {
		size_t                      capacity = batch->capacity ? batch->capacity * 2 : 32;
		struct st_controller_event *records  = realloc(batch->records, capacity * sizeof(*records));
		if (!records) {
			batch->failed = true;
			return;
		}
		batch->records  = records;
		batch->capacity = capacity;
	}
	batch->records[batch->count++] = *record;
}

static bool
controller_input_owned(const st_term_session_t *session)
{
	return !session->suspended && !session->unfocused && !session->wire_owner
	       && (!session->transfer || session->ddofx.background_download) && !session->transfer_reserved
	       && !session->musicomm && st_connection_is_connected(session->config.connection);
}

static bool
controller_subscribed(const st_term_session_t *session)
{
	return session->pads.enabled || session->controller_hook || st_ddofx_joystick_active(&session->ddofx);
}

static void
controller_snapshot_event(void *context, const struct st_controller_event *event)
{
	deliver_controller(context, event, false);
}

static void
sample_controllers(void *context)
{
	st_term_session_t *session = context;
	/* A subscribed session already maintains filtered state. Never call into
	 * Wren from the parser, or bypass a hook by delivering its pending values. */
	if (!controller_input_owned(session) || controller_subscribed(session))
		return;
	st_controllers_poll(session->config.controllers, NULL, NULL);
	st_controllers_poll(session->config.controllers, controller_snapshot_event, session);
}

static int
poll_controllers(st_term_session_t *session, enum st_term_disposition *disposition, bool *finished)
{
	if (session->controller_refresh) {
		/* Coalesce local activity while the session did not own input. */
		st_controllers_poll(session->config.controllers, NULL, NULL);
		session->controller_refresh = false;
	}
	struct controller_batch batch = {0};
	st_controllers_poll(session->config.controllers, collect_controller, &batch);
	int status = batch.failed ? IO_ERROR_OUT_OF_MEMORY : 0;
	for (size_t i = 0; !status && !*finished && i < batch.count; i++) {
		const struct local_input input = {
		    .event = {.type = ST_CONSOLE_EVENT_CONTROLLER, .data.controller = batch.records[i]}
                };
		status = handle_event(session, &input, disposition, finished);
	}
	if (!status && !session->suspended && !session->unfocused && !session->deferred_head)
		st_cterm_pad_pause(st_term_session_cterm(session), false);
	session->did_work |= batch.count != 0;
	free(batch.records);
	return status;
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
	/* Apply a newly installed local hook to held values before the first
	 * remote request can inspect the inventory. */
	if (session->controller_refresh && session->controller_hook && controller_input_owned(session)) {
		status = poll_controllers(session, disposition, finished);
		if (status || *finished)
			return status;
	}
	/* Local event dispatch leaves the wire suffix buffered and the
	 * emulated serial clock running. */
	bool parser_was_waiting       = !session->igs_dispatch_pending && input_paused(session);
	session->igs_dispatch_pending = false;
	/* A stopped transport has no recipient for asynchronous reports.
	 * Still drain its received bytes and take the normal disconnect path. */
	if (st_connection_is_connected(session->config.connection)) {
		if (session->musicomm == NULL)
			st_term_rip3_poll(session->rip3);
		status = st_term_session_poll_display(session);
		if (status != 0 || session->output_error != 0)
			return status != 0 ? status : session->output_error;
		if (session->end_requested) {
			*disposition =
			    session->exit_requested ? ST_TERM_EXIT_APPLICATION : ST_TERM_RETURN_TO_PICKER;
			*finished = true;
			return 0;
		}
	}
	if (!session->suspended && session->transfer == NULL && session->musicomm == NULL
	    && session->throttle_bps != 0) {
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
	if (parser_was_waiting && !protocol_input && !paused)
		session->did_work = true;
	if (session->transfer_reserved || session->wire_owner != NULL || paused)
		read_size = 0;
	if (protocol_input) {
		size_t space = st_transfer_input_space(session->transfer);
		if (read_size > space)
			read_size = space;
	}
	if ((!session->suspended || protocol_input || session->musicomm != NULL) && read_size != 0
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
	       && (!session->suspended || protocol_input || session->musicomm != NULL)
	       && !session->transfer_reserved
	       && (session->returned_position < session->returned_size
	           || session->wire_position < session->wire_size)) {
		bool     replay   = session->returned_position < session->returned_size;
		size_t  *position = replay ? &session->returned_position : &session->wire_position;
		size_t   size     = replay ? session->returned_size : session->wire_size;
		uint8_t *bytes    = replay ? session->returned_wire : session->wire_pending;
		protocol_input    = session->transfer != NULL && st_transfer_needs_input(session->transfer);
		if (session->transfer != NULL && st_transfer_done(session->transfer))
			break;
		if (protocol_input || session->musicomm != NULL) {
			size_t count = size - *position;
			bool   skip  = protocol_input && session->transfer_skip_lf && bytes[*position] == '\n';
			if (skip)
				count = 1;
			else if (protocol_input) {
				size_t space = st_transfer_input_space(session->transfer);
				if (count > space)
					count = space;
			}
			if (count == 0)
				break;
			session->transfer_skip_lf = false;
			const uint8_t *input      = bytes + *position;
			if (session->config.capture_active == NULL
			    || session->config.capture_active(session->config.capture_context)) {
				if (!replay && session->config.wire_capture != NULL) {
					status = session->config.wire_capture(session->config.capture_context,
					    input, count);
					if (status != 0)
						return status;
				}
				if (session->musicomm != NULL && session->config.capture != NULL) {
					status = session->config.capture(session->config.capture_context,
					    (const char *)input, count, true);
					if (status != 0)
						return status;
				}
			}
			if (session->musicomm != NULL)
				st_musicomm_feed(session->musicomm, input, count);
			else if (!skip)
				count = st_transfer_feed(session->transfer, input, count);
			if (count == 0)
				break;
			*position += count;
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
		    && session->config.graphics_mode == ST_GRAPHICS_NONE
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
		if (st_ooii_mode(session->ooii) == 0) {
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
	if (session->musicomm != NULL && session->wire_position == session->wire_size
	    && session->returned_position == session->returned_size
	    && st_connection_pending_raw(session->config.connection) == 0) {
		st_musicomm_idle(session->musicomm);
		if (st_musicomm_state(session->musicomm) == ST_MUSICOMM_OFF) {
			status = release_musicomm(session);
			if (status != 0)
				return status;
		}
	}

	/* Publish the consumed text batch before local keys/scripts inspect it. */
	st_term_rip3_flush(session->rip3);
	for (;;) {
		if (session->modal_tail.capture_token != 0) {
			struct local_input input          = session->modal_tail;
			session->modal_tail.capture_token = 0;
			status                            = handle_event(session, &input, disposition, finished);
		}
		else if (session->deferred_head != NULL && !graphics_input_pending(session))
			status = deliver_deferred(session);
		else {
			status = st_console_event_try(session->config.console, &event);
			if (status != 0)
				break;
			status =
			    handle_event(session, &(struct local_input) {.event = event}, disposition, finished);
		}
		session->did_work = true;
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
	if (controller_subscribed(session) && controller_input_owned(session)) {
		status = poll_controllers(session, disposition, finished);
		if (status || *finished)
			return status;
	}
	else {
		session->controller_refresh = true;
		if (session->pads.paused && controller_input_owned(session) && !session->deferred_head)
			st_cterm_pad_pause(st_term_session_cterm(session), false);
	}
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
	    && (session->transfer != NULL || session->transfer_reserved
	        || (paused && !session->ddofx.sleeper.warning && !session->ddofx.slide.due)
	        || cterm_big_needs_poll(session->big) || cterm_igs_needs_poll(session->igs)
	        || session->musicomm != NULL || st_cterm_needs_poll(st_term_session_cterm(session))))
		timeout_ms = 1;
	if (ddofx_can_run(session)) {
		uint32_t ddofx_wait = st_ddofx_wait_ms(&session->ddofx);
		if (timeout_ms > ddofx_wait)
			timeout_ms = ddofx_wait;
	}
	if (controller_subscribed(session) && controller_input_owned(session)) {
		uint32_t controller_wait = st_controllers_wait_ms(session->config.controllers);
		if (timeout_ms > controller_wait)
			timeout_ms = controller_wait;
	}
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
st_term_session_set_controller_hook(st_term_session_t *session, bool active)
{
	if (session)
		session->controller_hook = active;
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
	if (session->transfer != NULL || st_term_session_wire_owned(session))
		return IO_ERROR_BUSY;
	session->transfer_was_binary = st_connection_binary_mode(session->config.connection);
	int status                   = st_transfer_needs_input(transfer)
	                                   ? st_connection_set_binary_mode(session->config.connection, true)
	                                   : 0;
	if (status != 0)
		return status;
	st_ddofx_sleep_close(&session->ddofx);
	session->transfer          = transfer;
	session->transfer_reserved = false;
	if (session->ddofx.graphics && session->ddofx.enable_background_download) {
		struct st_transfer_inspection state;
		st_transfer_inspect(transfer, &state);
		if (state.protocol == ST_TRANSFER_ZMODEM && !state.upload) {
			st_ddofx_background_download(&session->ddofx, true);
			(void)st_time_monotonic_ms(&session->download_started_ms);
			session->download_row[0] = 0;
			session->download_row_due = 0;
		}
	}
	return 0;
}

int
st_term_session_end_transfer(st_term_session_t *session)
{
	if (session == NULL || session->transfer == NULL)
		return 0;
	st_transfer_t *transfer = session->transfer;
	(void)st_transfer_join(transfer);
	if (session->packages.autoplay.name[0]) {
		const struct st_ddofx_sink sink = ddofx_sink(session);
		st_ddofx_play_realplay(&session->ddofx, &sink, session->packages.autoplay.name,
		    session->packages.autoplay.rate);
		session->packages.autoplay = (struct st_ddofx_autoplay) {0};
	}
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
	if (session->ddofx.background_download) {
		(void)st_console_write_status_row(session->config.console, NULL, 0);
		st_ddofx_background_download(&session->ddofx, false);
	}
	st_ddofx_sleep_arm(&session->ddofx);
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
		if (reserved && st_term_session_wire_owned(session))
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
		if (st_term_session_wire_owned(session))
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
	if (st_term_session_wire_owned(session) || session->transfer != NULL || session->transfer_reserved
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
	return session != NULL && (session->wire_owner != NULL || session->musicomm != NULL);
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
