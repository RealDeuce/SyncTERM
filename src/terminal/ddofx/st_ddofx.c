/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "filesystem.h"
#include "io_error.h"
#include "st_cmf.h"
#include "st_ddofx_graphics.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_samples.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"
#include "value_convert_time.h"

static bool
matches(const char *body, const char *name)
{
	return body[0] == name[0] && ascii_case_compare(body + 1, name + 1) == 0;
}

static bool
argument(const struct st_ddofx *ddo, const char *body, const char *name, int32_t *value)
{
	const size_t length = strlen(name);
	return body[0] == name[0] && ascii_case_ncompare(body + 1, name + 1, length - 1) == 0
	       && body[length] == ',' && st_ddofx_number(ddo, body + length + 1, value);
}

void
st_ddofx_query_reply(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *text, bool numeric)
{
	if (st_ddofx_capture(ddo, text, numeric))
		return;
	uint8_t reply[ST_DDOFX_BODY_MAX + 1];
	size_t  n = strlen(text);
	memcpy(reply, text, n);
	reply[n] = 0xff;
	sink->reply(sink->context, reply, n + 1);
}

static void
file_record(char *record, const struct st_ddofx_sink *sink, const char *category, const char *client,
    const char *name)
{
	FILE          *file = sink->open_asset(sink->context, category, client, name);
	struct fs_info info;
	bool           found = file != NULL && fs_get_stream_info(&info, file) == 0 && info.kind == FS_ENTRY_FILE;
	if (file != NULL)
		fclose(file);
	struct tm local;
	if (found) {
		time_t stamp = (time_t)info.modified_time;
		found        = (int64_t)stamp == info.modified_time && value_convert_localtime(stamp, &local);
	}
	if (!found) {
		strcpy(record, client == NULL ? "NO" : "N");
		return;
	}

	/* The SDK's timestamp comes from DOS file time, in two-second units. */
	snprintf(record, ST_DDOFX_BODY_MAX + 1, "%" PRIu64 ",%d,%d,%d,%d,%d,%d", info.size, local.tm_year + 1900,
	    local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec & ~1);
}

static int
resource_id(const struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *container,
    const char *member, int wanted)
{
	const char *name     = st_ddofx_value(ddo, container);
	const char *category = "BBSTHEME";
	if (ascii_case_ncompare(name, "BBSTHEME", 8) == 0 && (name[8] == '/' || name[8] == '\\'))
		name += 9;
	else if (ascii_case_ncompare(name, "THEMES", 6) == 0 && (name[6] == '/' || name[6] == '\\')) {
		category = "THEMES";
		name += 7;
	}
	FILE *file = sink->open_asset(sink->context, category, NULL, name);
	if (file == NULL)
		return -1;
	uint32_t size;
	int      id = st_ddofx_resource_find(file, st_ddofx_value(ddo, member), wanted, &size, NULL);
	fclose(file);
	return id;
}

bool
st_ddofx_builtin(const struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *name, char *result,
    bool *numeric)
{
	*numeric = false;
	if (!ascii_case_compare(name, "LITERM"))
		strcpy(result, "LITERM Ver. 2.00");
	else if (!ascii_case_compare(name, "SPEED")) {
		snprintf(result, ST_DDOFX_BODY_MAX + 1, "%" PRId32, st_ddofx_speed_index());
		*numeric = true;
	}
	else if (!ascii_case_compare(name, "MEM") || !ascii_case_compare(name, "DRIVE")) {
		int32_t bytes = !ascii_case_compare(name, "DRIVE")
		                    ? st_ddofx_available_drive(sink->cache_directory)
		                : sink->available_memory ? sink->available_memory()
		                                         : -1;
		snprintf(result, ST_DDOFX_BODY_MAX + 1, "%" PRId32, bytes);
		*numeric = true;
	}
	else if (!ascii_case_compare(name, "VCARD"))
		strcpy(result, st_ddofx_svga(sink) ? "S" : "V");
	else if (!ascii_case_compare(name, "MOUSE")) {
		strcpy(result, sink->mouse_disabled ? "0" : "3");
		*numeric = true;
	}
	else if (!ascii_case_compare(name, "GETFONT"))
		strcpy(result, st_ddofx_vga_font_name(ddo));
	else if (!ascii_case_compare(name, "GETMUSIC"))
		strcpy(result, *ddo->music_name ? ddo->music_name : "NONE");
	else if (!ascii_case_compare(name, "GETTHEME"))
		strcpy(result, st_ddofx_theme_name(ddo));
	else if (!ascii_case_compare(name, "MUSICON") || !ascii_case_compare(name, "SOUNDON")
	         || !ascii_case_compare(name, "ACARD")) {
		bool available = st_audio_runtime_is_pull(sink->audio) || st_audio_runtime_allowed(sink->audio);
		if (!ascii_case_compare(name, "ACARD"))
			strcpy(result, available ? "SB 16" : "NONE");
		else
			strcpy(result, available ? "YES" : "NO");
	}
	else if (ascii_case_ncompare(name, "RESOURCE|", 9) == 0) {
		char fields[ST_DDOFX_BODY_MAX + 1];
		strcpy(fields, name + 9);
		char *member = strchr(fields, '|');
		int   id     = -1;
		if (member != NULL) {
			*member++ = 0;
			id        = resource_id(ddo, sink, fields, member, -1);
		}
		snprintf(result, ST_DDOFX_BODY_MAX + 1, "%d", id);
		*numeric = true;
	}
	else if (*name && strchr("mftr", *name) != NULL) {
		const char *category = *name == 'm' ? "MUSIC" : *name == 'f' ? "FONTS" : "BBSTHEME";
		file_record(result, sink, category, NULL, name + 1);
	}
	else
		return false;
	return true;
}

static void
stop_music(struct st_ddofx *ddo)
{
	st_tracker_destroy(ddo->player);
	ddo->player = NULL;
	st_cmf_destroy(ddo->cmf);
	ddo->cmf           = NULL;
	ddo->music_name[0] = 0;
}

static void
music(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *filename)
{
	if (ascii_case_compare(filename, "NONE") == 0) {
		stop_music(ddo);
		return;
	}
	if (!st_audio_runtime_is_pull(sink->audio) && st_audio_runtime_allowed(sink->audio) == 0)
		return;
	FILE *file = sink->open_asset(sink->context, "MUSIC", NULL, filename);
	if (file == NULL)
		return;
	uint8_t signature[4];
	bool    cmf =
	    fread(signature, 1, sizeof(signature), file) == sizeof(signature) && !memcmp(signature, "CTMF", 4);
	struct st_cmf     *fm   = NULL;
	struct st_tracker *next = NULL;
	if (fs_stream_seek(file, 0) == 0) {
		if (cmf)
			fm = st_cmf_play(sink->audio, file);
		else
			next = st_tracker_play(sink->audio, file, ddo->volume);
	}
	fclose(file);
	if (next != NULL || fm != NULL) {
		stop_music(ddo);
		ddo->cmf    = fm;
		ddo->player = next;
		memcpy(ddo->music_name, filename, strlen(filename) + 1);
	}
}

void
st_ddofx_reset(struct st_ddofx *ddo)
{
	st_ddofx_graphics_exit(ddo);
	st_ddofx_release_vga_font(ddo);
	st_ddofx_slide_restore(ddo);
	st_ddofx_restore_lite_palette(ddo);
	for (size_t i = 0; i < sizeof(ddo->page_files) / sizeof(ddo->page_files[0]); ++i)
		if (ddo->page_files[i] != NULL)
			fclose(ddo->page_files[i]);
	if (ddo->retrieval.file)
		fclose(ddo->retrieval.file);
	free(ddo->saved_palette);
	free(ddo->queued);
	free(ddo->keys);
	free(ddo->joystick);
	free(ddo->events);
	st_ddofx_samples_destroy(ddo);
	st_ddofx_script_destroy(ddo);
	stop_music(ddo);
	/* The session releases the receive worker after joining it. Resetting
	 * a display during reception must retain that worker's wire ownership. */
	*ddo = (struct st_ddofx) {
	    .volume              = 64,
	    .background_download = ddo->background_download,
	    .slide.suspended     = ddo->slide.suspended,
	    .sleeper.suspended   = ddo->sleeper.suspended,
	    .controller_paused   = ddo->controller_paused,
	    .sticks              = {ddo->sticks[0], ddo->sticks[1]}
        };
}

static void
check_memory(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *amount)
{
	int32_t requested;
	if (!st_ddofx_number(ddo, amount, &requested) || requested <= 0 || !sink->available_memory)
		return;
	int32_t available = sink->available_memory();
	if (available < 0 || available >= requested)
		return;
	if (sink->alert) {
		char message[192];
		snprintf(message, sizeof(message), "This script needs %" PRId32 " bytes more memory.%s",
		    requested - available, ddo->graphics ? " Returning to text mode." : "");
		sink->alert(sink->ui_context, "DDO F/X", message);
	}
	st_ddofx_graphics_exit(ddo);
	ddo->keys_locked  = false;
	ddo->abort_script = st_ddofx_script_active(ddo);
}

static void
execute(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	if (matches(body, "~HEARCMD"))
		ddo->ignoring = false;
	else if (matches(body, "~IGNORCMD"))
		ddo->ignoring = true;
	else if (matches(body, "~LOCKKEYS"))
		ddo->keys_locked = true;
	else if (matches(body, "~UNLOCKKEYS"))
		ddo->keys_locked = false;
	else if (matches(body, "~PAUSEBAK"))
		ddo->background_paused = true;
	else if (matches(body, "~UNPAUSEBAK"))
		ddo->background_paused = false;
	else if (body[0] == '7' && !ddo->ignoring && ddo->graphics && st_ddofx_bar_command(ddo, body))
		return;
	else if (body[0] == '~' || (!ddo->ignoring && (body[0] == '7' || body[0] == '4')))
		st_ddofx_script_command(ddo, sink, body);
	else if (ddo->ignoring || body[0] == ':')
		return;
	else if (matches(body, "2RESET")) {
		if (ddo->graphics) {
			ddo->ping.interval_ms = 0;
			ddo->sleeper.configured = false;
			st_ddofx_sleep_arm(ddo);
		}
		st_ddofx_graphics_reset(ddo->graphics);
		st_ddofx_script_clear(ddo);
		st_ddofx_clear_keys(ddo);
		free(ddo->joystick);
		ddo->joystick = NULL;
		ddo->keys_locked = false;
		st_ddofx_clear_sample_profiles(ddo);
		free(ddo->events);
		ddo->events = NULL;
		ddo->background_paused = false;
		ddo->enable_background_download = false;
		ddo->define_stealth = false;
	}
	else if (matches(body, "dVIDMODE"))
		st_ddofx_query_reply(ddo, sink, ddo->graphics ? "1" : "0", true);
	else if (!strncmp(body, "2f", 2)) {
		if (strlen(body + 2) < sizeof(ddo->resource_prefix))
			strcpy(ddo->resource_prefix, body + 2);
	}
	else if (ddo->graphics && !ascii_case_ncompare(body, "1SCALE,", 7))
		st_ddofx_text_scale(ddo, sink, body + 7);
	else if (ddo->graphics && body[0] == '1')
		st_ddofx_text_window_command(ddo, sink, body);
	else if (body[0] == '2') {
		if (ddo->graphics != NULL)
			st_ddofx_graphics_command(ddo, sink, body);
	}
	else if (ddo->graphics && body[0] == 'M' && !ascii_case_ncompare(body + 1, "SPD,", 4))
		st_ddofx_mouse_command(ddo, sink, body);
	else if (body[0] == '3' && ddo->graphics)
		st_ddofx_mouse_command(ddo, sink, body + 1);
	else if (ddo->graphics && !ascii_case_ncompare(body, "5SLEEPER,", 9)) {
		int32_t minutes;
		if (st_ddofx_number(ddo, body + 9, &minutes) && minutes >= 0) {
			ddo->sleeper.minutes    = minutes;
			ddo->sleeper.configured = true;
			st_ddofx_sleep_arm(ddo);
		}
	}
	else if (ddo->graphics && !ascii_case_ncompare(body, "5PING,", 6))
		st_ddofx_define_ping(ddo, body + 6);
	else if (body[0] == '5' && ddo->graphics)
		st_ddofx_gadget_command(ddo, sink, body);
	else if (body[0] == 'E' && !ascii_case_ncompare(body + 1, "EDTEXT,", 7))
		st_ddofx_edit_text_file(ddo, sink, body + 8);
	else if (body[0] == 'E' && !ascii_case_ncompare(body + 1, "WTEXT,", 6))
		st_ddofx_write_text_file(ddo, sink, body + 7);
	else if (body[0] == 'E' && !ascii_case_ncompare(body + 1, "PTEXT,", 6))
		st_ddofx_print_file(ddo, sink, body + 7);
	else if (body[0] == 'E' && !ascii_case_ncompare(body + 1, "RTEXT,", 6))
		st_ddofx_retrieve_text_file(ddo, sink, body + 7);
	else if (!ddo->graphics && body[0] == '0') {
		int32_t value;
		if (argument(ddo, body, "0SETFUL", &value)) {
			if ((value == 1 || value == 2) && sink->lite_screen_mode)
				sink->lite_screen_mode(sink->context, (unsigned)value);
		}
		else if (matches(body, "0RSETFUL")) {
			if (sink->lite_screen_mode)
				sink->lite_screen_mode(sink->context, 0);
		}
		else if (matches(body, "0SLIDEU"))
			st_ddofx_slide(ddo, sink, true, false);
		else if (matches(body, "0SLIDED") || matches(body, "0BOUNCED"))
			st_ddofx_slide(ddo, sink, false, matches(body, "0BOUNCED"));
		else if (argument(ddo, body, "0TFADEIN", &value))
			st_ddofx_lite_fade(ddo, sink, true, value);
		else if (argument(ddo, body, "0TFADEOUT", &value))
			st_ddofx_lite_fade(ddo, sink, false, value);
	}
	else if (!ddo->graphics && body[0] == '!')
		st_ddofx_display_raw(ddo, sink, body + 1);
	else if (body[0] == '*')
		st_ddofx_run_resource(ddo, sink, st_ddofx_value(ddo, body + 1));
	else if (body[0] == 'd' && !ascii_case_ncompare(body + 1, "VMEM,", 5))
		check_memory(ddo, sink, body + 6);
	else if (body[0] == 'd' && ascii_case_ncompare(body + 1, "RESOURCE,", 9) == 0) {
		char       *args      = body + 10;
		const char *container = st_ddofx_field(&args);
		const char *member    = st_ddofx_field(&args);
		int32_t     id;
		bool        found = member != NULL && args != NULL && st_ddofx_number(ddo, args, &id) && id >= 0
		                    && id <= 64000 && resource_id(ddo, sink, container, member, id) >= 0;
		st_ddofx_query_reply(ddo, sink, found ? "YES" : "NO", false);
	}
	else if (body[0] == 'd' && ascii_case_ncompare(body + 1, "RESOURCE|", 9) != 0) {
		/* The script operand exists in Lite, the wire query belongs to Normal mode. */
		if (!ddo->graphics && !ascii_case_compare(body + 1, "MOUSE"))
			return;
		char result[ST_DDOFX_BODY_MAX + 1];
		bool numeric;
		if (st_ddofx_builtin(ddo, sink, body + 1, result, &numeric))
			st_ddofx_query_reply(ddo, sink, result, numeric);
	}
	else if (ascii_case_ncompare(body, "8CDIR,", 6) == 0)
		st_ddofx_query_reply(ddo, sink,
		    sink->client_exists(sink->context, st_ddofx_value(ddo, body + 6)) ? "Y" : "N", false);
	else if (ascii_case_ncompare(body, "8CFILE,", 7) == 0) {
		char *client = (char *)body + 7;
		char *comma  = strchr(client, ',');
		if (comma != NULL) {
			*comma = 0;
			char result[ST_DDOFX_BODY_MAX + 1];
			file_record(result, sink, "TERMS", st_ddofx_value(ddo, client),
			    st_ddofx_value(ddo, comma + 1));
			st_ddofx_query_reply(ddo, sink, result, false);
		}
		else
			st_ddofx_query_reply(ddo, sink, "N", false);
	}
	else if (body[0] == '6' && st_ddofx_joystick_command(ddo, sink, body))
		return;
	else if (st_ddofx_samples_command(ddo, sink, body))
		return;
	else if (body[0] == 'f')
		st_ddofx_load_vga_font(ddo, sink, st_ddofx_value(ddo, body + 1));
	else if (body[0] == 'm')
		music(ddo, sink, st_ddofx_value(ddo, body + 1));
	else if (body[0] == 'w') {
		int32_t  ticks;
		uint64_t now;
		if (st_ddofx_number(ddo, body + 1, &ticks) && ticks > 0 && st_time_monotonic_ms(&now) == 0)
			ddo->wait_until = now + ((uint64_t)ticks * 1000 + 17) / 18;
	}
	else if (matches(body, "MICMF"))
		st_ddofx_query_reply(ddo, sink, ddo->cmf ? "Y" : "N", false);
	else if (matches(body, "MMPR"))
		st_tracker_toggle_pause(ddo->player);
	else if (matches(body, "MGVOL")) {
		char volume[4];
		snprintf(volume, sizeof(volume), "%u", ddo->volume);
		st_ddofx_query_reply(ddo, sink, volume, true);
	}
	else {
		int32_t value;
		if (argument(ddo, body, "MMPAT", &value) && (value == -1 || value == 1))
			st_tracker_step_order(ddo->player, (int)value);
		else if (argument(ddo, body, "MSVOL", &value) && value >= 0 && value <= 64)
			ddo->volume = (unsigned)value;
		else if (argument(ddo, body, "MMVOL", &value)) {
			if (value <= -(long)ddo->volume)
				ddo->volume = 0;
			else if (value >= 64 - (long)ddo->volume)
				ddo->volume = 64;
			else
				ddo->volume = (unsigned)((long)ddo->volume + value);
		}
		else
			return;
		st_tracker_set_volume(ddo->player, ddo->volume);
	}
}

int
st_ddofx_parse(struct st_ddofx *ddo, struct st_ddofx_framer *frame, const struct st_ddofx_sink *sink,
    const uint8_t *bytes, size_t size, size_t *consumed)
{
	*consumed = 0;
	while (*consumed < size) {
		const uint8_t *here = bytes + *consumed;
		if (frame->phase == ST_DDO_TEXT && *here != 0x1d) {
			if (frame == &ddo->wire)
				ddo->mode_prefix = false;
			size_t         remaining = size - *consumed;
			const uint8_t *gs        = memchr(here, 0x1d, remaining);
			size_t         length    = gs == NULL ? remaining : (size_t)(gs - here);
			size_t         used      = 0;
			/* Publish all rows changed by this text run together. */
			st_console_t *display =
			    ddo->graphics && sink->text_config ? sink->text_config->console : NULL;
			bool batch  = display && st_console_begin_update(display) == 0;
			int  status = sink->text(sink->context, here, length, &used);
			if (batch)
				(void)st_console_end_update(display);
			*consumed += used;
			if (status != 0 || used < length)
				return status;
			continue;
		}
		uint8_t byte = *here;
		switch (frame->phase) {
			case ST_DDO_TEXT:
				frame->phase = ST_DDO_PREFIX;
				break;
			case ST_DDO_PREFIX:
				if (byte == '{') {
					frame->phase  = ST_DDO_BODY;
					frame->length = 0;
				}
				else if (byte != 0x1d && byte != 0) {
					frame->phase = ST_DDO_TEXT;
					continue;
				}
				break;
			case ST_DDO_BODY:
				if (byte == '}') {
					frame->phase               = ST_DDO_TEXT;
					frame->body[frame->length] = 0;
					if (frame == &ddo->wire && !ddo->ignoring
					    && matches(frame->body, "LIGMODE")) {
						if (ddo->mode_prefix && ddo->graphics == NULL)
							st_ddofx_graphics_enter(ddo, sink);
						ddo->mode_prefix = ddo->graphics == NULL && !ddo->mode_prefix;
					}
					else {
						if (frame == &ddo->wire)
							ddo->mode_prefix = false;
						execute(ddo, sink, frame->body);
					}
					++*consumed;
					return 0;
				}
				else if (byte == '\r' || byte == '\n') {
					frame->phase = ST_DDO_TEXT;
					continue;
				}
				else if (byte != 0) {
					if (frame->length == ST_DDOFX_BODY_MAX) {
						frame->phase = ST_DDO_DISCARD;
						if (frame == &ddo->wire)
							ddo->mode_prefix = false;
					}
					else
						frame->body[frame->length++] = byte;
				}
				break;
			case ST_DDO_DISCARD:
				if (byte == '}')
					frame->phase = ST_DDO_TEXT;
				else if (byte == '\r' || byte == '\n') {
					frame->phase = ST_DDO_TEXT;
					continue;
				}
				else if (byte == 0x1d)
					frame->phase = ST_DDO_PREFIX;
				break;
		}
		++*consumed;
	}
	return 0;
}

bool
st_ddofx_busy(const struct st_ddofx *ddo)
{
	return ddo->slide.due || ddo->sleeper.warning || ddo->retrieval.file || ddo->wait_until != 0
	       || ddo->fade_due != 0 || st_ddofx_script_active(ddo) || ddo->queued_start != ddo->queued_end;
}

static uint32_t
ping_wait_ms(const struct st_ddofx *ddo, uint64_t *now)
{
	if (!ddo->ping.interval_ms || !ddo->graphics || ddo->background_download || ddo->wait_until
	    || ddo->fade_due || st_time_monotonic_ms(now) != 0)
		return UINT32_MAX;
	if (*now >= ddo->ping.due)
		return 0;
	uint64_t remaining = ddo->ping.due - *now;
	return remaining < UINT32_MAX ? (uint32_t)remaining : UINT32_MAX - 1;
}

uint32_t
st_ddofx_wait_ms(const struct st_ddofx *ddo)
{
	uint64_t now;
	uint32_t ping    = ping_wait_ms(ddo, &now);
	uint32_t sleeper = st_ddofx_sleep_wait_ms(ddo);
	uint32_t timers  = ping < sleeper ? ping : sleeper;
	if (ddo->slide.due) {
		if (st_time_monotonic_ms(&now) || now >= ddo->slide.due)
			return 0;
		uint64_t frame = ddo->slide.due - now;
		if (frame < timers)
			timers = (uint32_t)frame;
	}
	if (ddo->sleeper.warning)
		return timers;
	uint32_t shake = st_ddofx_shake_wait_ms(ddo);
	if (shake < timers)
		timers = shake;
	uint32_t audio = st_ddofx_sample_wait_ms(ddo), event = st_ddofx_event_wait_ms(ddo);
	uint32_t bars = st_ddofx_bar_wait_ms(ddo);
	uint32_t wait = audio < event ? audio : event;
	uint32_t grid = st_ddofx_grid_wait_ms(ddo);
	uint32_t mouse = st_ddofx_mouse_wait_ms(ddo);
	uint32_t pointer = st_ddofx_pointer_wait_ms(ddo);
	uint32_t marquee = st_ddofx_marquee_wait_ms(ddo);
	uint32_t joystick = st_ddofx_joystick_wait_ms(ddo);
	if (joystick < wait)
		wait = joystick;
	uint32_t animation = st_ddofx_animation_wait_ms(ddo);
	if (animation < wait)
		wait = animation;
	uint32_t polygon = st_ddofx_polygon_wait_ms(ddo);
	if (polygon < wait)
		wait = polygon;
	uint32_t ray = st_ddofx_ray_wait_ms(ddo);
	if (ray < wait)
		wait = ray;
	uint32_t land = st_ddofx_landscape_wait_ms(ddo);
	if (land < wait)
		wait = land;
	uint32_t movie = st_ddofx_movie_wait_ms(ddo);
	if (movie < wait)
		wait = movie;
	uint32_t clock_ms = st_ddofx_clock_wait_ms(ddo);
	if (clock_ms < wait)
		wait = clock_ms;
	if (marquee < wait)
		wait = marquee;
	if (pointer < wait)
		wait = pointer;
	if (mouse < wait)
		wait = mouse;
	if (grid < wait)
		wait = grid;
	if (bars < wait)
		wait = bars;
	return timers < wait ? timers : wait;
}

int
st_ddofx_poll(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	uint64_t ping_time;
	if (ping_wait_ms(ddo, &ping_time) == 0) {
		ddo->ping.due = ping_time + ddo->ping.interval_ms;
		if (ddo->ping.length && sink->reply)
			sink->reply(sink->context, ddo->ping.bytes, ddo->ping.length);
	}
	if (st_ddofx_sleep_poll(ddo, sink))
		return 0;
	st_ddofx_shake_poll(ddo);
	st_ddofx_samples_poll(ddo);
	st_ddofx_bar_poll(ddo);
	st_ddofx_grid_poll(ddo);
	st_ddofx_landscape_poll(ddo);
	st_ddofx_ray_poll(ddo);
	st_ddofx_polygon_poll(ddo);
	st_ddofx_joystick_poll(ddo);
	st_ddofx_movie_poll(ddo);
	st_ddofx_animation_poll(ddo, sink);
	st_ddofx_clock_poll(ddo);
	st_ddofx_marquee_poll(ddo);
	st_ddofx_event_poll(ddo);
	st_ddofx_mouse_poll(ddo);
	st_ddofx_pointer_poll(ddo);
	/* Yield back to the session between slices so long or infinite scripts
	 * cannot prevent local disconnect, redraws or transfer servicing. */
	for (unsigned i = 0; i < 128; ++i) {
		if (ddo->slide.due && st_ddofx_slide_step(ddo, sink))
			break;
		if (ddo->retrieval.file) {
			st_ddofx_retrieve_step(ddo, sink);
			if (ddo->retrieval.file)
				break;
		}
		if (ddo->fade_due != 0 || ddo->wait_until != 0) {
			uint64_t now;
			if (st_time_monotonic_ms(&now) != 0)
				now = UINT64_MAX;
			if (ddo->fade_due && (now < ddo->fade_due || st_ddofx_fade_step(ddo, now)))
				break;
			if (now < ddo->wait_until)
				break;
			ddo->wait_until = 0;
		}
		if (st_ddofx_script_active(ddo)) {
			int status = st_ddofx_script_step(ddo, sink);
			if (status != 0)
				return status;
		}
		else if (ddo->queued_start != ddo->queued_end) {
			size_t used;
			int    status = st_ddofx_parse(ddo, &ddo->wire, sink, ddo->queued + ddo->queued_start,
			    ddo->queued_end - ddo->queued_start, &used);
			ddo->queued_start += used;
			if (ddo->queued_start == ddo->queued_end) {
				free(ddo->queued);
				ddo->queued       = NULL;
				ddo->queued_start = ddo->queued_end = 0;
			}
			if (status != 0 || used == 0)
				return status;
		}
		else
			break;
	}
	return 0;
}

int
st_ddofx_feed(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const uint8_t *bytes, size_t size,
    size_t *consumed)
{
	*consumed = 0;
	while (*consumed < size && !st_ddofx_busy(ddo)) {
		size_t used;
		int    status = st_ddofx_parse(ddo, &ddo->wire, sink, bytes + *consumed, size - *consumed, &used);
		*consumed += used;
		if (status != 0 || used == 0)
			return status;
	}
	/* The session stops reading while busy. Preserve the rest of the current
	 * delivery, including any bytes supplied by an input hook. */
	if (*consumed < size) {
		size_t count = size - *consumed;
		if (count > SIZE_MAX - ddo->queued_end)
			return IO_ERROR_OUT_OF_MEMORY;
		uint8_t *pending = realloc(ddo->queued, ddo->queued_end + count);
		if (pending == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		ddo->queued = pending;
		memcpy(pending + ddo->queued_end, bytes + *consumed, count);
		ddo->queued_end += count;
		*consumed = size;
	}
	return 0;
}
