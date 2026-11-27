/* Incremental terminal-emulation parser and semantic console renderer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_key.h"
#include "cterm_rip_args.h"
#include "cterm_rip_asset.h"
#include "cterm_rip_engine.h"
#include "cterm_rip_file_query.h"
#include "cterm_rip_fonts.h"
#include "cterm_rip_time.h"
#include "cterm_vt52.h"
#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_cterm.h"
#include "st_rip154_internal.h"
#include "st_screen_mode.h"
#include "st_session_history.h"
#include "st_threads.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "string_compat.h"

static struct st_rip154 *
device(const st_cterm_t *terminal)
{
	return terminal->device.context;
}

static int rip_send_mouse_command(st_cterm_t *terminal, const char *command);
static int rip_send_field_command(st_cterm_t *terminal, const char *command, struct cterm_rip_mouse_field *field);
static int rip_activate_field(st_cterm_t *terminal, struct cterm_rip_mouse_field *field);
static int rip_compat_frame(st_cterm_t *terminal);

/* RIPv3 reset restores the session font, outside RIP's six text styles. */
enum {
	RIP_TEXT_FONT_DEFAULT = 6
};

static int
rip_text_mode(st_cterm_t *terminal, struct st_console_config *mode)
{
	st_console_t           *console = terminal->screen.console;
	struct st_pixel_buffer *saved   = NULL;
	struct st_console_rect  area    = {0};
	uint32_t                old_width, old_height, old_cell_width, old_cell_height;
	bool                    blocky;
	int                     status = st_console_pixel_size(console, &area.width, &area.height);

	if (status == 0)
		status = st_console_size(console, &old_width, &old_height);
	if (status == 0)
		status = st_console_cell_pixel_size(console, &old_cell_width, &old_cell_height);
	if (status == 0)
		status = st_console_get_blink_mode(console, &mode->blink_mode);
	if (status == 0)
		status = st_console_get_video_flags(console, &mode->video_flags);
	if (status == 0)
		status = st_console_get_scaling_mode(console, &mode->scaling_mode);
	if (status == 0)
		status = st_console_get_color_emoji(console, &mode->color_emoji);
	if (status == 0)
		status = st_console_get_blocky_scaling(console, &blocky);
	if (status == 0)
		status = st_console_get_default_cursor(console, &mode->default_cursor);
	if (status != 0)
		return status;
	mode->scaling_policy       = blocky ? ST_PIXEL_SCALING_BLOCKY : ST_PIXEL_SCALING_POINTY;
	mode->default_cursor.range = mode->cell_height;
	mode->default_cursor.start = mode->cell_height - (mode->cell_height > 8 ? 2U : 1U);
	mode->default_cursor.end   = mode->cell_height - 1U;
	if (area.width > mode->pixel_width)
		area.width = mode->pixel_width;
	if (area.height > mode->pixel_height)
		area.height = mode->pixel_height;
	status = st_console_get_pixels(&saved, console, &area, false);
	if (status != 0)
		return status;
	/* Status and API-reserved rows are local chrome, not saved RIP artwork. */
	uint32_t chrome_rows   = terminal->api_reserved_rows + (terminal->status_display_type >= 1 ? 1U : 0U);
	uint32_t chrome_top    = (old_height - chrome_rows) * old_cell_height;
	uint32_t chrome_bottom = old_height * old_cell_height;
	for (uint32_t y = chrome_top; y < chrome_bottom && y < saved->height; y++) {
		memset(saved->primary + (size_t)y * saved->width, 0, saved->width * sizeof(saved->primary[0]));
		memset(saved->alternate + (size_t)y * saved->width, 0, saved->width * sizeof(saved->alternate[0]));
	}
	status = st_console_begin_update(console);
	if (status == 0) {
		status = st_console_reconfigure(console, mode);
		if (status == 0) {
			const struct st_pixel_blit blit = {
			    .width   = saved->width,
			    .height  = saved->height,
			    .scale_x = 1,
			    .scale_y = 1,
			};

			status = st_console_blit_pixels(console, saved, NULL, &blit);
		}
		int finished = st_console_end_update(console);
		if (status == 0)
			status = finished;
	}
	st_pixel_buffer_destroy(saved);
	return status;
}

static int
rip_text_font(st_cterm_t *terminal, int style)
{
	struct st_console_font          font;
	struct st_console_custom_cursor cursor;
	struct st_console_config        mode       = {0};
	struct st_console_cell         *scrollback = NULL;
	struct st_console_cell         *old_scrollback;
	struct st_pixel_buffer        **history_pixels = NULL;
	const uint8_t                  *bitmap;
	size_t                          bitmap_size;
	uint32_t                        cell_width, cell_height, columns, rows, old_width, old_height;
	uint32_t                        old_cell_width, old_cell_height;
	uint32_t                        status_rows;
	uint16_t                        identifier  = 0;
	bool                            change_mode = device(terminal)->engine.raster.version == 3;
	enum st_codepage                codepage    = ST_CODEPAGE_CP437;
	int                             status;

	if (style < 0 || style > RIP_TEXT_FONT_DEFAULT)
		return 0;
	if (style == RIP_TEXT_FONT_DEFAULT) {
		const struct cterm_rip_engine *engine = &device(terminal)->engine;

		cell_width  = engine->default_text.cell_width;
		cell_height = engine->default_text.cell_height;
		columns     = engine->default_text.columns;
		rows        = engine->default_text.rows;
		identifier  = engine->default_text.font_identifier;
		codepage    = engine->default_text.codepage;
	}
	else {
		cell_width  = style == 1 || style == 3 ? 7 : style == 4 ? 16 : 8;
		cell_height = style < 2 ? 8 : style == 5 ? 16 : 14;
		columns     = style == 1 || style == 3 ? 91 : style == 4 ? 40 : 80;
		rows        = cell_height == 8 ? 43 : cell_height == 16 ? 21 : 25;
	}
	if (change_mode) {
		status = st_console_pixel_size(terminal->screen.console, &mode.pixel_width, &mode.pixel_height);
		if (status == 0)
			status = st_console_get_aspect_ratio(terminal->screen.console, &mode.aspect_width,
			    &mode.aspect_height);
		if (status != 0)
			return status;
		/* A RIP text font changes the grid, not the device resolution. */
		columns = mode.pixel_width / cell_width;
		rows    = mode.pixel_height / cell_height;
	}
	status_rows = terminal->status_display_type >= 1 ? 1 : 0;
	if (rows <= status_rows + terminal->api_reserved_rows)
		return IO_ERROR_OUT_OF_RANGE;
	if (style != RIP_TEXT_FONT_DEFAULT && !device(terminal)->rip_text_font_registered[style]) {
		if (!st_builtin_font_get(45, 8, 8, &font))
			return IO_ERROR_NOT_SUPPORTED;
		if (style == 0 || style == 2 || style == 5) {
			if (!st_builtin_font_get(style == 5 ? 0 : 45, cell_width, cell_height, &font))
				return IO_ERROR_NOT_SUPPORTED;
		}
		else {
			bitmap = cterm_rip_text_font_data(style, &bitmap_size);
			if (bitmap == NULL)
				return IO_ERROR_NOT_SUPPORTED;
			font.bitmap       = bitmap;
			font.bitmap_size  = bitmap_size;
			font.width        = cell_width;
			font.height       = cell_height;
			font.row_stride   = (cell_width + 7U) / 8U;
			font.glyph_stride = font.row_stride * cell_height;
		}
		status = st_console_register_static_font(terminal->screen.console, &font, &identifier);
		if (status != 0)
			return status;
		device(terminal)->rip_text_font_identifiers[style] = identifier;
		device(terminal)->rip_text_font_registered[style]  = true;
	}
	if (style != RIP_TEXT_FONT_DEFAULT)
		identifier = device(terminal)->rip_text_font_identifiers[style];
	status = st_console_size(terminal->screen.console, &old_width, &old_height);
	if (status != 0)
		return status;
	status = st_console_cell_pixel_size(terminal->screen.console, &old_cell_width, &old_cell_height);
	if (status != 0)
		return status;
	if (old_cell_height != cell_height) {
		status = st_console_get_default_cursor(terminal->screen.console, &cursor);
		if (status != 0)
			return status;
		cursor.range = cell_height;
		cursor.start = cell_height - (cell_height > 8 ? 2U : 1U);
		cursor.end   = cell_height - 1U;
	}
	if (columns != old_width || rows != old_height || cell_width != old_cell_width
	    || cell_height != old_cell_height) {
		if (terminal->backlines != 0) {
			status = cterm_resize_scrollback(terminal, columns, cell_width, cell_height, &scrollback,
			    &history_pixels);
			if (status != 0)
				return status;
		}
		mode.columns     = columns;
		mode.rows        = rows;
		mode.cell_width  = cell_width;
		mode.cell_height = cell_height;
		status = change_mode ? rip_text_mode(terminal, &mode)
		                     : st_console_regrid_preserve_pixels(terminal->screen.console, columns, rows,
					   cell_width, cell_height);
		if (status != 0) {
			free(scrollback);
			st_history_free_pixels(history_pixels, (size_t)terminal->backlines);
			return status;
		}
		old_scrollback       = terminal->scrollback;
		terminal->scrollback = scrollback;
		st_history_free_pixels(terminal->backpixels, (size_t)terminal->backlines);
		terminal->backpixels = history_pixels;
		terminal->backwidth  = (int)columns;
		terminal->backstart  = 0;
		terminal->backpos    = terminal->backfilled == terminal->backlines ? 0 : terminal->backfilled;
		free(old_scrollback);
		cterm_resize_parser_geometry(terminal, (int)columns,
		    (int)(rows - status_rows - terminal->api_reserved_rows), false);
		if (terminal->status_sub != NULL) {
			terminal->status_sub->x = terminal->x;
			terminal->status_sub->y = terminal->y + terminal->height;
			cterm_resize_parser_geometry(terminal->status_sub, (int)columns, 1, false);
		}
		if (old_cell_height != cell_height) {
			status = st_console_set_default_cursor(terminal->screen.console, &cursor);
			if (status != 0)
				return status;
		}
		if (change_mode) {
			device(terminal)->engine.raster.screen_width  = mode.pixel_width;
			device(terminal)->engine.raster.screen_height = mode.pixel_height;
			if (terminal->api_size_changed != NULL)
				terminal->api_size_changed(terminal->api_size_context, columns,
				    (uint32_t)terminal->height, mode.pixel_width, mode.pixel_height);
		}
	}
	for (size_t selector = 0; selector < UINT8_MAX + 1U; selector++) {
		terminal->screen.font_identifiers[selector] = identifier;
		terminal->screen.font_valid[selector]       = true;
		terminal->screen.font_codepages[selector]   = codepage;
	}
	cterm_render_attribute_colours(terminal, terminal->attr, &terminal->fg_color, &terminal->bg_color);
	if ((terminal->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) != 0) {
		terminal->fg_color += 16;
		terminal->bg_color += 16;
	}
	cterm_render_set_attribute(terminal, terminal->attr);
	cterm_render_set_colours(terminal, terminal->fg_color, terminal->bg_color);
	/* Legacy reinit_screen() starts its replacement CTerm before restoring
	 * the saved pixel canvas.  A later plain-text byte must not start it
	 * again and erase graphics beneath the text window. */
	terminal->started                     = 1;
	device(terminal)->rip_text_font_style = style;
	return cterm_render_take_error(terminal);
}

static int
rip_passthrough(void *context, const uint8_t *bytes, size_t size)
{
	st_cterm_t *terminal = context;
	size_t      consumed;
	int         status = cterm_input_write(terminal, bytes, size, &consumed, CTERM_INPUT_WIRE);

	return status == 0 && consumed != size ? IO_ERROR_IO : status;
}

static int
rip_text_boundary(void *context)
{
	st_cterm_t *terminal = context;
	int         status   = cterm_text_boundary(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_text_boundary(terminal->status_sub);
	return status;
}

static int
rip_control(void *context, const uint8_t *bytes, size_t size)
{
	st_cterm_t *terminal = context;
	const char *version;

	if (size < 3 || bytes[0] != 0x1b || bytes[1] != '[')
		return IO_ERROR_INVALID_ARGUMENT;
	if (size != 3 && (size != 4 || bytes[2] != '0'))
		return 0;
	version = device(terminal)->engine.raster.version == 3 ? "RIPSCRIP030001" : "RIPSCRIP015410";
	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context, ST_CTERM_OUTPUT_RESPONSE,
		    (const uint8_t *)version, 14);
	return 0;
}

static bool
rip_music_introducer(void *context, uint8_t final)
{
	st_cterm_t *terminal = context;

	return final == '|' || (final == 'M' && terminal->music_enable == CTERM_MUSIC_ENABLED)
	       || (final == 'N' && terminal->music_enable >= CTERM_MUSIC_BANSI);
}

static bool
rip_at_column(void *context)
{
	st_cterm_t *terminal = context;

	return terminal->xpos == cterm_crpos(terminal);
}

static bool
rip_text_disabled(void *context)
{
	st_cterm_t *terminal = context;

	return device(terminal)->rip_text_disabled;
}

static char
rip_ascii_upper(char ch)
{
	return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
}

static bool
rip_variable_name_equal(const char *left, const char *right)
{
	while (*left != '\0' && *right != '\0') {
		if (rip_ascii_upper(*left++) != rip_ascii_upper(*right++))
			return false;
	}
	return *left == *right;
}

static bool
rip_variable_token_equal(const char *text, size_t length, const char *name)
{
	for (size_t i = 0; i < length; i++) {
		if (name[i] == '\0' || rip_ascii_upper(text[i]) != rip_ascii_upper(name[i]))
			return false;
	}
	return name[length] == '\0';
}

static int rip_host_command(void *context, int level, int sublevel, uint8_t code, const char *arguments);

static int
rip_save_text_window(st_cterm_t *terminal)
{
	struct cterm_rip_raster     *raster = &device(terminal)->engine.raster;
	bool                         visible;
	enum st_console_cursor_shape shape;
	int                          status = st_console_get_cursor(terminal->screen.console, &visible, &shape);

	if (status != 0)
		return status;
	device(terminal)->rip_saved_text.x              = raster->x;
	device(terminal)->rip_saved_text.y              = raster->y;
	device(terminal)->rip_saved_text.left           = terminal->left_margin;
	device(terminal)->rip_saved_text.top            = terminal->top_margin;
	device(terminal)->rip_saved_text.right          = terminal->right_margin;
	device(terminal)->rip_saved_text.bottom         = terminal->bottom_margin;
	device(terminal)->rip_saved_text.font_style     = device(terminal)->rip_text_font_style;
	device(terminal)->rip_saved_text.wrap           = (terminal->extattr & CTERM_EXTATTR_AUTOWRAP) != 0;
	device(terminal)->rip_saved_text.disabled       = device(terminal)->rip_text_disabled;
	device(terminal)->rip_saved_text.cursor_visible = visible;
	device(terminal)->rip_saved_text.cursor_shape   = shape;
	device(terminal)->rip_saved_text.attr           = terminal->attr;
	device(terminal)->rip_saved_text.foreground     = terminal->fg_color;
	device(terminal)->rip_saved_text.background     = terminal->bg_color;
	return 0;
}

static int
rip_restore_text_window(st_cterm_t *terminal)
{
	struct cterm_rip_raster *raster = &device(terminal)->engine.raster;
	bool                     window_changed =
	    terminal->left_margin != device(terminal)->rip_saved_text.left
	    || terminal->top_margin != device(terminal)->rip_saved_text.top
	    || terminal->right_margin != device(terminal)->rip_saved_text.right
	    || terminal->bottom_margin != device(terminal)->rip_saved_text.bottom
	    || device(terminal)->rip_text_font_style != device(terminal)->rip_saved_text.font_style;
	int status;

	if (device(terminal)->rip_text_font_style != device(terminal)->rip_saved_text.font_style) {
		status = rip_text_font(terminal, device(terminal)->rip_saved_text.font_style);
		if (status != 0)
			return status;
	}
	terminal->left_margin =
	    device(terminal)->rip_saved_text.left < 1 ? 1 : device(terminal)->rip_saved_text.left;
	terminal->top_margin = device(terminal)->rip_saved_text.top < 1 ? 1 : device(terminal)->rip_saved_text.top;
	terminal->right_margin =
	    device(terminal)->rip_saved_text.right < 1 ? 1 : device(terminal)->rip_saved_text.right;
	terminal->bottom_margin =
	    device(terminal)->rip_saved_text.bottom < 1 ? 1 : device(terminal)->rip_saved_text.bottom;
	if (terminal->left_margin > terminal->width)
		terminal->left_margin = terminal->width;
	if (terminal->top_margin > terminal->height)
		terminal->top_margin = terminal->height;
	if (terminal->right_margin > terminal->width)
		terminal->right_margin = terminal->width;
	if (terminal->bottom_margin > terminal->height)
		terminal->bottom_margin = terminal->height;
	if (device(terminal)->rip_saved_text.wrap)
		terminal->extattr |= CTERM_EXTATTR_AUTOWRAP;
	else
		terminal->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
	terminal->extattr |= CTERM_EXTATTR_ORIGINMODE;
	device(terminal)->rip_text_disabled =
	    device(terminal)->rip_saved_text.disabled
	    || (device(terminal)->rip_saved_text.left == 0 && device(terminal)->rip_saved_text.top == 0
	        && device(terminal)->rip_saved_text.right == 0 && device(terminal)->rip_saved_text.bottom == 0);
	if (window_changed) {
		cterm_apply_window(terminal);
		cterm_set_position(terminal, 1, 1);
	}
	raster->x          = device(terminal)->rip_saved_text.x;
	raster->y          = device(terminal)->rip_saved_text.y;
	terminal->attr     = device(terminal)->rip_saved_text.attr;
	terminal->fg_color = device(terminal)->rip_saved_text.foreground;
	terminal->bg_color = device(terminal)->rip_saved_text.background;
	cterm_render_set_attribute(terminal, terminal->attr);
	cterm_render_set_colours(terminal, terminal->fg_color, terminal->bg_color);
	status = cterm_render_take_error(terminal);
	if (status != 0)
		return status;
	return st_console_set_cursor(terminal->screen.console, device(terminal)->rip_saved_text.cursor_visible,
	    device(terminal)->rip_saved_text.cursor_shape);
}

enum {
	RIP_QUERY_VALUE_SIZE = 128
};

/* The old RIP MUSIC/PHASER samples use a phase-continuous linear sweep. */
static double
rip_sweep(double phase, unsigned start, unsigned end, size_t frames, int16_t *output)
{
	const double two_pi = 6.28318530717958647692;
	const double scale  = two_pi / AUDIO_SAMPLE_RATE;
	double       initial, delta;

	if (start < end) {
		initial = -scale * start;
		delta   = scale * (end - start) / frames;
	}
	else {
		initial = scale * start;
		delta   = -scale * (start - end) / frames;
	}
	for (size_t i = 0; i < frames; i++) {
		int16_t sample;

		phase += initial + i * delta;
		if (phase > two_pi)
			phase -= two_pi;
		sample            = (int16_t)(sin(phase) * 8192.0);
		output[2 * i]     = sample;
		output[2 * i + 1] = sample;
	}
	return phase;
}

static int
rip_sound_variable(st_cterm_t *terminal, const char *name, size_t length, bool *handled)
{
	const struct timespec beep_pause = {.tv_sec = 0, .tv_nsec = 75000000};
	const struct timespec blip_pause = {.tv_sec = 0, .tv_nsec = 10000000};
	int16_t              *wave;
	size_t                frames;

	*handled = true;
	if (rip_variable_token_equal(name, length, "ALARM")) {
		for (int i = 0; i < 3; i++) {
			(void)cterm_play_fx_tone(terminal, 320, 200, AUDIO_WAVE_SINE);
			(void)cterm_play_fx_tone(terminal, 160, 425, AUDIO_WAVE_SINE);
		}
		return 0;
	}
	if (rip_variable_token_equal(name, length, "BEEP")) {
		(void)cterm_play_fx_tone(terminal, 1000, 75, AUDIO_WAVE_SINE);
		(void)st_thread_sleep(&beep_pause, NULL);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "BLIP")) {
		frames = AUDIO_SAMPLE_RATE * 25U / 1000U;
		wave   = malloc(frames * AUDIO_FRAME_SIZE);
		if (wave == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (audio_generate_tone(wave, frames, 50, AUDIO_WAVE_SINE, false) == 0)
			(void)cterm_play_fx(terminal, wave, frames);
		free(wave);
		(void)st_thread_sleep(&blip_pause, NULL);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "MUSIC") || rip_variable_token_equal(name, length, "PHASER")
	    || rip_variable_token_equal(name, length, "REVPHASER")) {
		bool music = rip_variable_token_equal(name, length, "MUSIC");

		frames = music ? 15872 : 10760;
		wave   = malloc(frames * AUDIO_FRAME_SIZE);
		if (wave == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (music) {
			double phase = 0.0;

			for (size_t i = 0; i < 4; i++) {
				int16_t *part = wave + 2 * i * 3968;

				phase = rip_sweep(phase, 1300, 700, 3086, part);
				phase = rip_sweep(phase, 700, 850, 442, part + 2 * 3086);
				phase = rip_sweep(phase, 850, 1300, 440, part + 2 * 3528);
			}
		}
		else if (rip_variable_token_equal(name, length, "PHASER"))
			(void)rip_sweep(0.0, 2500, 50, frames, wave);
		else
			(void)rip_sweep(0.0, 50, 2500, frames, wave);
		(void)cterm_play_fx(terminal, wave, frames);
		free(wave);
		return 0;
	}
	*handled = false;
	return 0;
}

static int
rip_clock_variable(const char *name, size_t length, char result[RIP_QUERY_VALUE_SIZE], bool *handled)
{
	static const char *const weekdays[7] = {
	    "Sun",
	    "Mon",
	    "Tue",
	    "Wed",
	    "Thu",
	    "Fri",
	    "Sat",
	};
	static const char *const months[12] = {
	    "Jan",
	    "Feb",
	    "Mar",
	    "Apr",
	    "May",
	    "Jun",
	    "Jul",
	    "Aug",
	    "Sep",
	    "Oct",
	    "Nov",
	    "Dec",
	};
	struct tm   local;
	time_t      now;
	const char *format = NULL;

	*handled =
	    rip_variable_token_equal(name, length, "ADOW") || rip_variable_token_equal(name, length, "DATE")
	    || rip_variable_token_equal(name, length, "DATETIME") || rip_variable_token_equal(name, length, "DAY")
	    || rip_variable_token_equal(name, length, "DOW") || rip_variable_token_equal(name, length, "DOY")
	    || rip_variable_token_equal(name, length, "MONTH")
	    || rip_variable_token_equal(name, length, "MONTHNUM") || rip_variable_token_equal(name, length, "YEAR")
	    || rip_variable_token_equal(name, length, "FYEAR") || rip_variable_token_equal(name, length, "WDAY")
	    || rip_variable_token_equal(name, length, "WOY") || rip_variable_token_equal(name, length, "WOYM")
	    || rip_variable_token_equal(name, length, "AMPM") || rip_variable_token_equal(name, length, "HOUR")
	    || rip_variable_token_equal(name, length, "MHOUR") || rip_variable_token_equal(name, length, "MIN")
	    || rip_variable_token_equal(name, length, "SEC") || rip_variable_token_equal(name, length, "TIME")
	    || rip_variable_token_equal(name, length, "TIMEZONE");
	if (!*handled)
		return 0;
	now = time(NULL);
	if (now == (time_t)-1 || !cterm_rip_localtime(now, &local))
		return IO_ERROR_IO;
	if (rip_variable_token_equal(name, length, "DATE"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d/%02d/%02d", local.tm_mon + 1, local.tm_mday,
		    local.tm_year % 100);
	else if (rip_variable_token_equal(name, length, "DATETIME")) {
		if (local.tm_wday < 0 || local.tm_wday >= 7 || local.tm_mon < 0 || local.tm_mon >= 12)
			return IO_ERROR_OUT_OF_RANGE;
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%s %s %2d %02d:%02d:%02d %04d\n",
		    weekdays[local.tm_wday], months[local.tm_mon], local.tm_mday, local.tm_hour, local.tm_min,
		    local.tm_sec, local.tm_year + 1900);
	}
	else if (rip_variable_token_equal(name, length, "DAY"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_mday);
	else if (rip_variable_token_equal(name, length, "DOY"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%03d", local.tm_yday + 1);
	else if (rip_variable_token_equal(name, length, "MONTHNUM"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_mon + 1);
	else if (rip_variable_token_equal(name, length, "YEAR"))
		/* Preserve the legacy parser's month-index substitution. */
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_mon % 100);
	else if (rip_variable_token_equal(name, length, "FYEAR"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%04d", local.tm_mon + 1900);
	else if (rip_variable_token_equal(name, length, "WDAY"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_wday);
	else if (rip_variable_token_equal(name, length, "AMPM"))
		strcpy(result, local.tm_hour < 12 ? "AM" : "PM");
	else if (rip_variable_token_equal(name, length, "MHOUR"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_hour);
	else if (rip_variable_token_equal(name, length, "MIN"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_min);
	else if (rip_variable_token_equal(name, length, "SEC"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d", local.tm_sec);
	else if (rip_variable_token_equal(name, length, "TIME"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d:%02d:%02d", local.tm_hour, local.tm_min,
		    local.tm_sec);
	else if (rip_variable_token_equal(name, length, "ADOW"))
		format = "%a";
	else if (rip_variable_token_equal(name, length, "DOW"))
		format = "%A";
	else if (rip_variable_token_equal(name, length, "MONTH"))
		format = "%B";
	else if (rip_variable_token_equal(name, length, "HOUR"))
		format = "%I";
	else if (rip_variable_token_equal(name, length, "WOY"))
		format = "%U";
	else if (rip_variable_token_equal(name, length, "WOYM"))
		format = "%W";
	else if (rip_variable_token_equal(name, length, "TIMEZONE"))
		format = "%Z";
	if (format != NULL) {
		result[0] = '\0';
		(void)strftime(result, RIP_QUERY_VALUE_SIZE, format, &local);
	}
	return 0;
}

static int
rip_query_variable(st_cterm_t *terminal, const char *name, size_t length, char result[RIP_QUERY_VALUE_SIZE],
    bool *handled)
{
	uint32_t                     cell_width, cell_height;
	bool                         visible;
	enum st_console_cursor_shape shape;
	int                          x, y, value;
	int                          status;

	*handled = true;
	status   = rip_clock_variable(name, length, result, handled);
	if (status != 0 || *handled)
		return status;
	*handled = true;
	if (rip_variable_token_equal(name, length, "RIPVER")) {
		memcpy(result, device(terminal)->engine.raster.version == 3 ? "RIPSCRIP030001" : "RIPSCRIP015410",
		    15);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "TWIN")) {
		strcpy(result, device(terminal)->rip_text_disabled ? "NO" : "YES");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "STATBAR")) {
		strcpy(result, terminal->status_display_type == 0 ? "NO" : "YES");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "CURSOR")) {
		status = st_console_get_cursor(terminal->screen.console, &visible, &shape);
		if (status != 0)
			return status;
		strcpy(result, visible && !device(terminal)->rip_text_disabled
		                       && !(terminal->left_margin == 1 && terminal->top_margin == 1
		                            && terminal->right_margin == 1 && terminal->bottom_margin == 1)
				   ? "YES"
				   : "NO");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "MSTAT")) {
		strcpy(result, "YES");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "X") || rip_variable_token_equal(name, length, "Y")
	    || rip_variable_token_equal(name, length, "XY") || rip_variable_token_equal(name, length, "XYM")
	    || rip_variable_token_equal(name, length, "M")) {
		struct cterm_rip_engine *engine = &device(terminal)->engine;
		int      mx = engine->mouse_query.active ? engine->mouse_query.x : engine->last_mouse_x;
		int      my = engine->mouse_query.active ? engine->mouse_query.y : engine->last_mouse_y;
		uint32_t buttons =
		    engine->mouse_query.active ? engine->mouse_query.buttons : engine->last_mouse_buttons;
		int width = engine->mouse_query.active && engine->mouse_query.text ? 2 : 4;

		if (rip_variable_token_equal(name, length, "X"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*d", width, mx);
		else if (rip_variable_token_equal(name, length, "Y"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*d", width, my);
		else if (rip_variable_token_equal(name, length, "M"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%u%u%u", (unsigned)(buttons & 1),
			    (unsigned)((buttons >> 1) & 1), (unsigned)((buttons >> 2) & 1));
		else if (rip_variable_token_equal(name, length, "XY"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*x:%0*x", width, mx, width, my);
		else
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*x:%0*x:%u%u%u", width, mx, width, my,
			    (unsigned)(buttons & 1), (unsigned)((buttons >> 1) & 1),
			    (unsigned)((buttons >> 2) & 1));
		return 0;
	}
	if (rip_variable_token_equal(name, length, "CURX") || rip_variable_token_equal(name, length, "CURY")) {
		cterm_coord_get_xy(terminal, CTERM_COORD_CURR, &x, &y);
		value = rip_variable_token_equal(name, length, "CURX") ? x - 1 : y - 1;
	}
	else if (rip_variable_token_equal(name, length, "TWFONT")) {
		value = 0;
		if (!device(terminal)->rip_text_disabled) {
			status = st_console_cell_pixel_size(terminal->screen.console, &cell_width, &cell_height);
			if (status != 0)
				return status;
			if (cell_width == 8 && cell_height == 8)
				value = 1;
			else if (cell_width == 7 && cell_height == 8)
				value = 2;
			else if (cell_width == 8 && cell_height == 14)
				value = 3;
			else if (cell_width == 16 && cell_height == 14)
				value = 4;
			else if (cell_width == 7 && cell_height == 14)
				value = 5;
		}
	}
	else if (rip_variable_token_equal(name, length, "TWW"))
		value =
		    device(terminal)->rip_text_disabled ? 0 : terminal->right_margin - terminal->left_margin + 1;
	else if (rip_variable_token_equal(name, length, "TWH"))
		value =
		    device(terminal)->rip_text_disabled ? 0 : terminal->bottom_margin - terminal->top_margin + 1;
	else if (rip_variable_token_equal(name, length, "TWX0"))
		value = device(terminal)->rip_text_disabled ? 0 : terminal->left_margin - 1;
	else if (rip_variable_token_equal(name, length, "TWX1"))
		value = device(terminal)->rip_text_disabled ? 0 : terminal->right_margin - 1;
	else if (rip_variable_token_equal(name, length, "TWY0"))
		value = device(terminal)->rip_text_disabled ? 0 : terminal->top_margin - 1;
	else if (rip_variable_token_equal(name, length, "TWY1"))
		value = device(terminal)->rip_text_disabled ? 0 : terminal->bottom_margin - 1;
	else {
		*handled  = false;
		result[0] = '\0';
		return 0;
	}
	(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%d", value);
	return 0;
}

static int
rip_state_variable(st_cterm_t *terminal, const char *name, size_t length, bool *handled)
{
	struct cterm_rip_raster *raster = &device(terminal)->engine.raster;
	int                      status;

	status = rip_sound_variable(terminal, name, length, handled);
	if (status != 0 || *handled)
		return status;
	/* These legacy built-ins are present but have no implementation there. */
	if ((length == 4 && rip_variable_token_equal(name, 3, "APP") && name[3] >= '0' && name[3] <= '9')
	    || rip_variable_token_equal(name, length, "VT102ON")
	    || rip_variable_token_equal(name, length, "VT102OFF")) {
		*handled = true;
		return 0;
	}
	if (rip_variable_token_equal(name, length, "COFF") || rip_variable_token_equal(name, length, "CON")) {
		*handled = true;
		return st_console_set_cursor(terminal->screen.console,
		    rip_variable_token_equal(name, length, "CON"), ST_CONSOLE_CURSOR_UNDERLINE);
	}
	if (rip_variable_token_equal(name, length, "DTW")) {
		*handled                            = true;
		device(terminal)->rip_text_disabled = true;
		return 0;
	}
	if (rip_variable_token_equal(name, length, "DWAYON")
	    || rip_variable_token_equal(name, length, "DWAYOFF")) {
		*handled               = true;
		terminal->doorway_mode = rip_variable_token_equal(name, length, "DWAYON");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "HKEYON")
	    || rip_variable_token_equal(name, length, "HKEYOFF")) {
		*handled                                 = true;
		device(terminal)->engine.hotkeys_enabled = rip_variable_token_equal(name, length, "HKEYON");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "TABON") || rip_variable_token_equal(name, length, "TABOFF")) {
		*handled                             = true;
		device(terminal)->engine.tab_enabled = rip_variable_token_equal(name, length, "TABON");
		if (!device(terminal)->engine.tab_enabled)
			cterm_rip_engine_clear_tab(&device(terminal)->engine);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "SBARON")
	    || rip_variable_token_equal(name, length, "SBAROFF")) {
		int old    = terminal->status_display_type;
		int wanted = rip_variable_token_equal(name, length, "SBARON") ? 1 : 0;

		*handled                      = true;
		terminal->status_display_type = wanted;
		if (old != wanted)
			terminal->status_display_cb(terminal, old, wanted, terminal->status_display_cbdata);
		return cterm_render_take_error(terminal);
	}
	if (rip_variable_token_equal(name, length, "STW")) {
		*handled = true;
		return rip_save_text_window(terminal);
	}
	if (rip_variable_token_equal(name, length, "RTW")) {
		*handled = true;
		return rip_restore_text_window(terminal);
	}
	if (rip_variable_token_equal(name, length, "SCB")) {
		*handled = true;
		return cterm_rip_raster_save_clipboard(raster);
	}
	if (rip_variable_token_equal(name, length, "RCB")) {
		*handled = true;
		return cterm_rip_raster_restore_clipboard(raster);
	}
	if (rip_variable_token_equal(name, length, "PCB")) {
		*handled = true;
		return cterm_rip_raster_paste_clipboard(raster);
	}
	if (rip_variable_token_equal(name, length, "EGW")) {
		bool raster_handled;

		*handled = true;
		return cterm_rip_raster_command(raster, 0, 0, 'E', "", &raster_handled);
	}
	if (rip_variable_token_equal(name, length, "ETW")) {
		*handled = true;
		cterm_render_clear_window(terminal);
		cterm_set_position(terminal, 1, 1);
		return cterm_render_take_error(terminal);
	}
	if (rip_variable_token_equal(name, length, "RESET")) {
		*handled = true;
		return rip_host_command(terminal, 0, 0, '*', "");
	}
	if (rip_variable_token_equal(name, length, "SMF")) {
		*handled = true;
		return cterm_rip_engine_save_mouse(&device(terminal)->engine);
	}
	if (rip_variable_token_equal(name, length, "RMF")) {
		*handled = true;
		return cterm_rip_engine_restore_mouse(&device(terminal)->engine);
	}
	if (rip_variable_token_equal(name, length, "MKILL")) {
		*handled = true;
		cterm_rip_engine_clear_mouse(&device(terminal)->engine);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "SAVEALL")) {
		*handled = true;
		status   = rip_save_text_window(terminal);
		if (status == 0)
			status = cterm_rip_raster_save_clipboard(raster);
		if (status == 0)
			status = cterm_rip_engine_save_mouse(&device(terminal)->engine);
		if (status == 0)
			status = cterm_rip_raster_save_screen(raster, 10);
		return status;
	}
	if (rip_variable_token_equal(name, length, "RESTOREALL")) {
		*handled = true;
		status   = rip_restore_text_window(terminal);
		if (status == 0)
			status = cterm_rip_raster_restore_clipboard(raster);
		if (status == 0)
			status = cterm_rip_engine_restore_mouse(&device(terminal)->engine);
		if (status == 0)
			status = cterm_rip_raster_restore_screen(raster, 10, false);
		return status;
	}
	if (rip_variable_token_equal(name, length, "SAVE")) {
		*handled = true;
		return cterm_rip_raster_save_screen(raster, 10);
	}
	if (rip_variable_token_equal(name, length, "RESTORE")) {
		*handled = true;
		return cterm_rip_raster_restore_screen(raster, 10, false);
	}
	if (length == 5 && name[4] >= '0' && name[4] <= '9' && rip_variable_token_equal(name, 4, "SAVE")) {
		*handled = true;
		return cterm_rip_raster_save_screen(raster, name[4] - '0');
	}
	if (length == 8 && name[7] >= '1' && name[7] <= '9' && rip_variable_token_equal(name, 7, "RESTORE")) {
		*handled = true;
		return cterm_rip_raster_restore_screen(&device(terminal)->engine.raster, name[7] - '0', true);
	}
	return 0;
}

static bool
rip_variable_name_valid(const char *name, size_t length)
{
	if (length == 0 || length > 12)
		return false;
	for (size_t i = 0; i < length; i++) {
		char ch = name[i];

		if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
		        || (i != 0 && ((ch >= '0' && ch <= '9') || ch == '_'))))
			return false;
	}
	return true;
}

static struct cterm_rip_user_var *
rip_find_variable(struct cterm_rip_engine *engine, const char *name)
{
	for (struct cterm_rip_user_var *variable = engine->user_vars; variable != NULL;
	    variable                             = variable->next) {
		if (rip_variable_name_equal(variable->name, name))
			return variable;
	}
	return NULL;
}

static int
rip_set_variable(struct cterm_rip_engine *engine, const char *name, const char *value, int width, bool persistent)
{
	struct cterm_rip_user_var *variable = rip_find_variable(engine, name);
	char                      *copy     = strdup(value);

	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (variable == NULL) {
		variable = calloc(1, sizeof(*variable));
		if (variable == NULL) {
			free(copy);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		memcpy(variable->name, name, strlen(name) + 1);
		variable->next    = engine->user_vars;
		engine->user_vars = variable;
	}
	free(variable->value);
	variable->value       = copy;
	variable->field_width = width;
	variable->persistent  = persistent;
	return 0;
}

static FILE *
rip_open_variable_file(const char *directory)
{
	struct fs_info info;
	char          *path   = cterm_rip_asset_path(directory, "variables.ini", false);
	FILE          *stream = NULL;

	if (path != NULL && fs_get_link_info(&info, directory) == 0 && info.kind == FS_ENTRY_DIRECTORY
	    && fs_get_link_info(&info, path) == 0 && info.kind == FS_ENTRY_FILE)
		(void)fs_open(&stream, path, "rb");
	free(path);
	return stream;
}

static int
rip_load_variables(st_cterm_t *terminal)
{
	ini_document_t *document = NULL;
	ini_section_t  *root;
	FILE           *stream;
	int             status = 0;

	stream = rip_open_variable_file(device(terminal)->rip_directory);
	if (stream == NULL)
		return 0;
	if (ini_document_read(&document, stream) != 0) {
		fclose(stream);
		return 0;
	}
	fclose(stream);
	root = ini_document_root(document);
	for (size_t i = 0; i < ini_section_count(root); i++) {
		const struct name_value *item = ini_section_at(root, i);

		if (item == NULL || item->name == NULL || item->value == NULL
		    || !rip_variable_name_valid(item->name, strlen(item->name)))
			continue;
		status = rip_set_variable(&device(terminal)->engine, item->name, item->value, 60, true);
		if (status != 0)
			break;
	}
	ini_document_free(&document);
	return status;
}

static void
rip_save_variable(st_cterm_t *terminal, const char *name)
{
	struct cterm_rip_user_var *variable;
	struct fs_info             info;
	ini_document_t            *document = NULL;
	ini_section_t             *root;
	char                      *path = NULL, *serialized = NULL;
	FILE                      *stream = NULL;
	size_t                     size   = 0;
	int                        status;

	variable = rip_find_variable(&device(terminal)->engine, name);
	if (variable == NULL)
		return;
	path = cterm_rip_asset_path(device(terminal)->rip_directory, "variables.ini", false);
	if (path == NULL)
		return;
	status = fs_get_link_info(&info, device(terminal)->rip_directory);
	if (status == IO_ERROR_NOT_FOUND) {
		status = fs_create_directories(device(terminal)->rip_directory);
		if (status == 0)
			status = fs_get_link_info(&info, device(terminal)->rip_directory);
	}
	if (status != 0 || info.kind != FS_ENTRY_DIRECTORY)
		goto done;
	status = fs_get_link_info(&info, path);
	if (status != IO_ERROR_NOT_FOUND && (status != 0 || info.kind != FS_ENTRY_FILE))
		goto done;
	if (status == 0) {
		status = fs_open(&stream, path, "rb");
		if (status != 0)
			goto done;
		status = ini_document_read(&document, stream);
		fclose(stream);
		stream = NULL;
	}
	else
		status = ini_document_create(&document);
	if (status != 0)
		goto done;
	root   = ini_document_root(document);
	status = ini_section_set_string(root, name, variable->value, NULL);
	if (status == 0)
		status = ini_document_serialize(document, &serialized, &size);
	if (status == 0)
		(void)fs_replace_file(path, serialized, size);
done:
	if (stream != NULL)
		fclose(stream);
	free(serialized);
	ini_document_free(&document);
	free(path);
}

/* 1: valid, 0: invalid definition, -1: allocation failure. */
static int
rip_parse_definition(const char *text, char name[13], int *width, char **question, char **default_value)
{
	const char *colon = strchr(text, ':');
	const char *comma;
	size_t      length;

	*question      = NULL;
	*default_value = NULL;
	*width         = 60;
	if (colon == NULL)
		return false;
	comma  = memchr(text, ',', (size_t)(colon - text));
	length = (size_t)((comma == NULL ? colon : comma) - text);
	if (!rip_variable_name_valid(text, length))
		return false;
	memcpy(name, text, length);
	name[length] = '\0';
	if (comma != NULL) {
		long requested = strtol(comma + 1, NULL, 10);

		*width = requested < 1 ? 1 : requested > 1023 ? 1023 : (int)requested;
	}
	text = colon + 1;
	if (*text == '?') {
		const char *end = strchr(text + 1, '?');

		if (end != NULL) {
			*question = strndup(text + 1, (size_t)(end - text - 1));
			if (*question == NULL)
				return -1;
			text = end + 1;
		}
	}
	if (*text != '\0') {
		*default_value = strdup(text);
		if (*default_value == NULL)
			return -1;
	}
	return true;
}

static int
rip_define_variable(st_cterm_t *terminal, const char *arguments)
{
	struct cterm_rip_engine   *engine = &device(terminal)->engine;
	struct cterm_rip_user_var *existing;
	char                       name[13], fallback[80], *question, *default_value, *entered = NULL;
	char                      *normalized = NULL;
	const char                *initial, *prompt;
	size_t                     normalized_size = 0;
	int                        flags, numbers[2], width, parsed, status = 0;
	const uint8_t              widths[2] = {3, 2};
	const char                *text      = arguments;
	bool                       save      = false, reject_blank, noninteractive;

	if (cterm_rip_read_fields(&text, widths, 2, numbers) != 2)
		return 0;
	flags  = (uint16_t)numbers[0];
	parsed = rip_parse_definition(text, name, &width, &question, &default_value);
	if (parsed == 0)
		return 0;
	if (parsed < 0) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	save           = (flags & 1) != 0;
	reject_blank   = (flags & 2) != 0;
	noninteractive = (flags & 4) != 0;
	existing       = rip_find_variable(engine, name);
	if (noninteractive && existing != NULL) {
		if (default_value != NULL && default_value[0] != '\0')
			status = rip_set_variable(engine, name, default_value, width, save);
		goto done;
	}
	if (device(terminal)->rip_prompt == NULL)
		goto done;
	initial = existing != NULL && existing->value != NULL ? existing->value
	          : default_value != NULL                     ? default_value
	                                                      : "";
	if (question != NULL)
		prompt = question;
	else {
		snprintf(fallback, sizeof(fallback), "Please enter: \"%s\"", name);
		prompt = fallback;
	}
	do {
		free(entered);
		entered = NULL;
		free(normalized);
		normalized = NULL;
		status     = device(terminal)->rip_prompt(device(terminal)->rip_prompt_context, prompt, initial,
		    (size_t)width, reject_blank, &entered);
		if (status != 0 || entered == NULL)
			break;
		status = st_unicode_text_normalize(&normalized, &normalized_size, entered, strlen(entered), NULL);
		if (status != 0)
			break;
		size_t position = 0, characters = 0;

		while (position < normalized_size && characters <= (size_t)width) {
			uint32_t codepoint;
			size_t   consumed;

			status = st_utf8_decode_one(&codepoint, &consumed, normalized + position,
			    normalized_size - position);
			if (status != 0)
				break;
			position += consumed;
			characters++;
		}
		if (status != 0)
			break;
		if (characters <= (size_t)width && (!reject_blank || normalized_size != 0))
			break;
	} while (true);
	if (status == 0 && normalized != NULL && (normalized_size != 0 || !reject_blank))
		status = rip_set_variable(engine, name, normalized, width, save);
done:
	if (status == 0 && save)
		rip_save_variable(terminal, name);
	free(entered);
	free(normalized);
	free(question);
	free(default_value);
	return status;
}

static int
rip_host_command(void *context, int level, int sublevel, uint8_t code, const char *arguments)
{
	st_cterm_t              *terminal = context;
	struct cterm_rip_raster *raster   = &device(terminal)->engine.raster;
	int                      first;
	int                      mouse[9];
	uint32_t                 previous_background;
	const char              *suffix = arguments;

	if (level == 9 && sublevel == 0 && code == 0x1b) {
		struct st_cterm_rip_transfer_request request = {0};
		size_t                               length;
		const uint8_t                        widths[4] = {1, 1, 2, 4};
		int                                  numbers[4], mode, protocol, type;
		int                                  status   = IO_ERROR_INVALID_ARGUMENT;
		char                                *filename = NULL, *path = NULL;

		if (cterm_rip_read_fields(&suffix, widths, 4, numbers) != 4)
			goto rip_transfer_rejected;
		mode     = numbers[0];
		protocol = numbers[1];
		type     = numbers[2];
		length   = strlen(suffix);
		if (!device(terminal)->engine.parser.command_terminated_cr
		    || device(terminal)->engine.scene_depth != 0 || length < 2
		    || strcmp(suffix + length - 2, "<>") != 0 || (unsigned)mode > 1 || (unsigned)protocol > 7
		    || (mode == 0 && ((unsigned)type > 5 || (type >= 4 && protocol < 5)))
		    || ((mode == 1 || protocol <= 4) && length == 2))
			goto rip_transfer_rejected;
		filename = malloc(length - 1);
		if (filename == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto rip_transfer_rejected;
		}
		memcpy(filename, suffix, length - 2);
		filename[length - 2] = '\0';
		if (*filename != '\0') {
			path = cterm_rip_asset_path(device(terminal)->rip_directory, filename, false);
			if (path == NULL)
				goto rip_transfer_rejected;
		}
		if (protocol == 4 || device(terminal)->rip_transfer == NULL) {
			status = IO_ERROR_NOT_SUPPORTED;
			goto rip_transfer_rejected;
		}
		request.upload    = mode == 1;
		request.protocol  = (unsigned)protocol;
		request.file_type = (uint16_t)type;
		request.directory = device(terminal)->rip_directory;
		request.filename  = filename;
		status = device(terminal)->rip_transfer(device(terminal)->rip_transfer_context, &request);
rip_transfer_rejected:
		free(filename);
		free(path);
		if (status != 0) {
			cterm_log(terminal, ST_CTERM_LOG_WARNING, "RIP block transfer rejected (%d)", status);
			static const uint8_t cancel[10] = {24, 24, 24, 24, 24, 24, 24, 24, 24, 24};
			if (terminal->api_output != NULL && device(terminal)->engine.scene_depth == 0)
				terminal->api_output(terminal->api_output_context, ST_CTERM_OUTPUT_RESPONSE,
				    cancel, sizeof(cancel));
		}
		return 0;
	}

	if (level == 1 && sublevel == 0) {
		switch (code) {
			case 'D':
				return rip_define_variable(terminal, arguments);
			case 'F':
			{
				char  *response      = NULL;
				size_t response_size = 0;
				int    status        = cterm_rip_file_query(&response, &response_size,
				    device(terminal)->rip_directory, arguments);

				if (status != 0)
					return status;
				if (response != NULL && terminal->api_output != NULL)
					terminal->api_output(terminal->api_output_context,
					    ST_CTERM_OUTPUT_RESPONSE, (const uint8_t *)response, response_size);
				free(response);
				return 0;
			}
			case 0x1b:
			{
				char        **stored;
				char         *replacement;
				const uint8_t widths[2] = {1, 3};
				int           numbers[2];

				if (cterm_rip_read_fields(&suffix, widths, 2, numbers) != 2)
					return 0;
				first = numbers[0];
				if (first == 0)
					return rip_send_mouse_command(terminal, suffix);
				if (first != 1 && first != 2)
					return 0;
				stored = first == 1 ? &device(terminal)->engine.graphics_click
				                    : &device(terminal)->engine.text_click;
				if (strcmp(suffix, "$OFF$") == 0) {
					free(*stored);
					*stored = NULL;
					return 0;
				}
				replacement = strdup(suffix);
				if (replacement == NULL)
					return IO_ERROR_OUT_OF_MEMORY;
				free(*stored);
				*stored = replacement;
				return 0;
			}
			case 'K':
				cterm_rip_engine_clear_mouse(&device(terminal)->engine);
				return 0;
			case 'M':
			{
				static const uint8_t widths[9] = {2, 2, 2, 2, 2, 1, 1, 4, 1};
				if (cterm_rip_read_fields(&suffix, widths, 9, mouse) != 9)
					return 0;
				if ((unsigned)mouse[0] >= 128
				    || mouse[3] > raster->viewport.ex - raster->viewport.sx
				    || mouse[4] > raster->viewport.ey - raster->viewport.sy || mouse[5] > 1
				    || mouse[6] > 1 || mouse[1] > mouse[3] || mouse[2] > mouse[4])
					return 0;
				return cterm_rip_engine_add_mouse(&device(terminal)->engine,
				    mouse[1] + raster->viewport.sx, mouse[2] + raster->viewport.sy,
				    mouse[3] + raster->viewport.sx, mouse[4] + raster->viewport.sy, mouse[5] != 0,
				    mouse[6] != 0, suffix);
			}
			case 'U':
			{
				static const uint8_t                 widths[7]    = {2, 2, 2, 2, 2, 1, 1};
				const struct cterm_rip_button_style *button_style = &raster->last_button_style;
				int                                  button[7];
				const char                          *icon_end;
				const char                          *label_start;
				const char                          *label_end;
				const char                          *command;
				char                                *icon    = NULL;
				char                                *label   = NULL;
				struct cterm_rip_mouse_field        *created = NULL;
				int                                  status;

				if (cterm_rip_read_fields(&suffix, widths, 7, button) != 7)
					return 0;
				label_start = strstr(suffix, "<>");
				icon_end    = label_start == NULL ? arguments + strlen(arguments) : label_start;
				if (icon_end != suffix) {
					icon = strndup(suffix, (size_t)(icon_end - suffix));
					if (icon == NULL)
						return IO_ERROR_OUT_OF_MEMORY;
				}
				if (label_start != NULL)
					label_start += 2;
				command   = label_start == NULL ? NULL : strstr(label_start, "<>");
				label_end = command == NULL ? NULL : command;
				command   = command == NULL ? "" : command + 2;
				if (label_start != NULL) {
					if (label_end == NULL)
						label_end = label_start + strlen(label_start);
					if (label_end != label_start) {
						size_t size = (size_t)(label_end - label_start);

						label = malloc(size + 1);
						if (label == NULL) {
							free(icon);
							return IO_ERROR_OUT_OF_MEMORY;
						}
						memcpy(label, label_start, size);
						label[size] = '\0';
					}
				}
				if ((button_style->flags & 1024) != 0) {
					const struct cterm_rip_button_draw drawing = {
					    .style         = *button_style,
					    .x1            = button[0] + raster->viewport.sx,
					    .y1            = button[1] + raster->viewport.sy,
					    .x2            = raster->last_button_x2,
					    .y2            = raster->last_button_y2,
					    .hotkey        = button[4],
					    .font_number   = raster->font_number,
					    .font_size     = raster->font_size,
					    .font_vertical = raster->font_vertical,
					    .icon          = icon,
					    .label         = label,
					};

					status = cterm_rip_engine_add_button_mouse(&device(terminal)->engine,
					    &drawing, (button[5] & 1) != 0, command);
					if (status != 0) {
						free(icon);
						free(label);
						return status;
					}
					created = device(terminal)->engine.mouse_fields;
				}
				free(icon);
				free(label);
				if ((button[5] & 1) != 0 && command[0] != '\0')
					return rip_send_field_command(terminal, command, created);
				return 0;
			}
			default:
				return IO_ERROR_NOT_SUPPORTED;
		}
	}
	if (level != 0 || sublevel != 0)
		return IO_ERROR_NOT_SUPPORTED;
	switch (code) {
		case '#':
			return 0;
		case '$':
		{
			const char *end = strchr(arguments, '$');
			char        answer[RIP_QUERY_VALUE_SIZE];
			bool        handled;
			int         status;

			if (end == NULL)
				return 0;
			status = rip_state_variable(terminal, arguments, (size_t)(end - arguments), &handled);
			if (status != 0 || handled)
				return status;

			if (rip_variable_token_equal(arguments, (size_t)(end - arguments), "COMPAT"))
				return rip_compat_frame(terminal);
			status =
			    rip_query_variable(terminal, arguments, (size_t)(end - arguments), answer, &handled);
			if (status != 0 || handled)
				return status;
			/* Legacy variables not recognized by this client expand to empty. */
			return 0;
		}
		case '*':
			cterm_rip_engine_clear_mouse(&device(terminal)->engine);
			device(terminal)->engine.hotkeys_enabled = true;
			device(terminal)->engine.tab_enabled     = true;
			{
				int status =
				    rip_text_font(terminal, raster->version == 3 ? RIP_TEXT_FONT_DEFAULT : 0);

				if (status != 0)
					return status;
			}
			device(terminal)->rip_text_disabled = false;
			terminal->left_margin               = 1;
			terminal->top_margin                = 1;
			terminal->right_margin              = terminal->width;
			terminal->bottom_margin             = terminal->height;
			terminal->extattr |= CTERM_EXTATTR_ORIGINMODE | CTERM_EXTATTR_AUTOWRAP;
			terminal->attr     = 15;
			terminal->fg_color = raster->palette[15];
			terminal->bg_color = raster->palette[0];
			cterm_apply_window(terminal);
			cterm_render_set_attribute(terminal, terminal->attr);
			cterm_render_set_colours(terminal, terminal->fg_color, terminal->bg_color);
			cterm_set_position(terminal, 1, 1);
			cterm_render_clear_window(terminal);
			return cterm_rip_raster_reset_windows(raster);
		case 'H':
			cterm_set_position(terminal, 1, 1);
			return cterm_render_take_error(terminal);
		case 'g':
		{
			const uint8_t widths[2] = {2, 2};
			int           coords[2];
			if (cterm_rip_read_fields(&suffix, widths, 2, coords) != 2)
				return 0;
			cterm_set_position(terminal, cterm_rip_signed_byte((uint8_t)coords[0]) + 1,
			    cterm_rip_signed_byte((uint8_t)coords[1]) + 1);
			return cterm_render_take_error(terminal);
		}
		case 'w':
		{
			const uint8_t widths[6] = {2, 2, 2, 2, 1, 1};
			int           window[6], coords[4], wrap, style;
			if (cterm_rip_read_fields(&suffix, widths, 6, window) != 6)
				return 0;
			for (size_t index = 0; index < 4; index++) {
				coords[index] = cterm_rip_signed_byte((uint8_t)window[index]);
			}
			wrap  = (uint8_t)window[4];
			style = window[5] >= 5 ? 0 : window[5];
			if (coords[0] < 0)
				coords[0] = 0;
			if (coords[1] < 0)
				coords[1] = 0;
			/* Reject inverted byte bounds before publishing a console window. */
			if (coords[2] < coords[0] || coords[3] < coords[1])
				return 0;
			if (coords[0] == 0 && coords[1] == 0 && coords[2] == 0 && coords[3] == 0) {
				device(terminal)->rip_text_disabled = true;
				return 0;
			}
			device(terminal)->rip_text_disabled = false;
			if ((unsigned)wrap > 1 || (unsigned)style > 4)
				return 0;
			{
				int status = rip_text_font(terminal, style);

				if (status != 0)
					return status;
			}
			for (size_t index = 0; index < 4; index++) {
				int maximum = index % 2 == 0 ? terminal->width : terminal->height;
				if (coords[index] >= maximum)
					coords[index] = maximum - 1;
			}
			terminal->left_margin   = coords[0] + 1;
			terminal->top_margin    = coords[1] + 1;
			terminal->right_margin  = coords[2] + 1;
			terminal->bottom_margin = coords[3] + 1;
			/* Legacy rip_text_window sets wrap before reinit_screen(),
		 * which recreates CTerm and restores its default modes. */
			terminal->extattr = CTERM_EXTATTR_AUTOWRAP | CTERM_EXTATTR_ORIGINMODE
			                    | CTERM_EXTATTR_SXSCROLL | CTERM_EXTATTR_DECBKM;
			cterm_apply_window(terminal);
			cterm_set_position(terminal, 1, 1);
			return cterm_render_take_error(terminal);
		}
		case 'b':
		{
			static const uint8_t widths[8] = {2, 2, 2, 2, 2, 2, 1, 4};
			int                  extended[8];
			int                  pixel_width, pixel_height, font_width, font_height;
			int                  xoffset, yoffset, font_style;
			int                  status;

			if (cterm_rip_read_fields(&suffix, widths, 8, extended) != 8)
				return 0;
			for (size_t index = 0; index < 4; index++) {
				if (extended[index] < 0)
					return 0;
			}
			extended[7]  = (uint16_t)extended[7];
			extended[0]  = cterm_rip_raster_map_x(raster, extended[0]);
			extended[2]  = cterm_rip_raster_map_x(raster, extended[2]);
			extended[1]  = cterm_rip_raster_map_y(raster, extended[1]);
			extended[3]  = cterm_rip_raster_map_y(raster, extended[3]);
			pixel_width  = extended[2] - extended[0] + 1;
			pixel_height = extended[3] - extended[1] + 1;
			if (pixel_width <= 0 || pixel_height <= 0 || extended[4] <= 0 || extended[5] <= 0)
				return 0;
			font_width = 16;
			if ((extended[7] & 0x10) == 0 && extended[6] != 4)
				font_width = 8;
			else if (pixel_width < font_width * extended[4])
				font_width = 8;
			if (pixel_width < font_width * extended[4])
				font_width = 7;
			font_height = 16;
			if (font_width == 16)
				font_height = 14;
			else if (pixel_height < font_height * extended[5])
				font_height = 14;
			if (pixel_height < font_height * extended[5])
				font_height = 8;
			if (pixel_width < font_width * extended[4])
				extended[4] = pixel_width / font_width;
			if (pixel_height < font_height * extended[5])
				extended[5] = pixel_height / font_height;
			if (extended[4] <= 0 || extended[5] <= 0)
				return 0;
			if ((extended[7] & 0x10) == 0) {
				switch (extended[6]) {
					case 0:
						if (font_height > 8)
							font_height = 8;
						break;
					case 1:
						if (font_height > 8)
							font_height = 8;
						if (font_width > 7)
							font_width = 7;
						break;
					case 3:
						if (font_width > 7)
							font_width = 7;
						break;
				}
			}
			xoffset = ((pixel_width - font_width * extended[4]) / 2) / font_width;
			yoffset = ((pixel_height - font_height * extended[5]) / 2) / font_height;
			if (font_width == 16 && font_height == 14)
				font_style = 4;
			else if (font_width == 8 && font_height == 16)
				font_style = 5;
			else if (font_width == 8 && font_height == 14)
				font_style = 2;
			else if (font_width == 8 && font_height == 8)
				font_style = 0;
			else if (font_width == 7 && font_height == 14)
				font_style = 3;
			else
				font_style = 1;
			status = rip_text_font(terminal, font_style);
			if (status != 0)
				return status;
			terminal->left_margin   = extended[0] / font_width + xoffset;
			terminal->top_margin    = extended[1] / font_height + yoffset;
			terminal->right_margin  = terminal->left_margin + extended[4] - 1;
			terminal->bottom_margin = terminal->top_margin + extended[5] - 1;
			terminal->extattr |= CTERM_EXTATTR_ORIGINMODE;
			if ((extended[7] & 1) != 0)
				terminal->extattr |= CTERM_EXTATTR_AUTOWRAP;
			else
				terminal->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
			cterm_apply_window(terminal);
			cterm_set_position(terminal, 1, 1);
			if ((extended[7] & 0x20) != 0) {
				int     old_style  = raster->fill_style;
				int     old_colour = raster->fill_colour;
				uint8_t old_pattern[8];

				memcpy(old_pattern, raster->fill_pattern, sizeof(old_pattern));
				cterm_rip_raster_fill_style(raster, 1, 0);
				cterm_rip_raster_bar(raster, extended[0], extended[1], extended[2], extended[3]);
				raster->fill_style  = old_style;
				raster->fill_colour = old_colour;
				memcpy(raster->fill_pattern, old_pattern, sizeof(old_pattern));
			}
			if (raster->error != 0)
				return raster->error;
			return cterm_render_take_error(terminal);
		}
		case 'e':
			if (!device(terminal)->rip_text_disabled) {
				cterm_render_clear_window(terminal);
				cterm_set_position(terminal, 1, 1);
			}
			return cterm_render_take_error(terminal);
		case '>':
			previous_background = terminal->bg_color;
			terminal->bg_color  = raster->palette[0];
			cterm_clear_line(terminal);
			terminal->bg_color = previous_background;
			return cterm_render_take_error(terminal);
		default:
			cterm_log(terminal, ST_CTERM_LOG_WARNING,
			    "Unsupported RIP command: level=%d sublevel=%d code=0x%02X", level, sublevel,
			    (unsigned)code);
			return IO_ERROR_NOT_SUPPORTED;
	}
}

static void
rip_warning(void *context, const char *message)
{
	cterm_log(context, ST_CTERM_LOG_WARNING, "%s", message);
}

static int
rip_compat_frame(st_cterm_t *terminal)
{
	/* The legacy COMPAT variable resets the coordinate frame only. */
	struct cterm_rip_raster *raster = &device(terminal)->engine.raster;
	uint32_t                 width, height;
	int                      status = st_console_pixel_size(raster->console, &width, &height);

	if (status != 0)
		return status;
	raster->screen_width  = width;
	raster->screen_height = height;
	raster->x_dim         = 640;
	raster->y_dim         = 350;
	raster->x_max         = width < 640 ? (int)width : 640;
	raster->y_max         = height < 350 ? (int)height : 350;
	raster->viewport.sx   = 0;
	raster->viewport.sy   = 0;
	raster->viewport.ex   = 639;
	raster->viewport.ey   = 349;
	return 0;
}

#define RIP_HOST_LIMIT 4095

struct rip_command_context {
	st_cterm_t              *terminal;
	struct cterm_rip_engine *engine;
	uint8_t                  output[RIP_HOST_LIMIT];
	size_t                   used;
	size_t                   depth;
};

static void
rip_command_append(struct rip_command_context *context, const void *bytes, size_t size)
{
	if (size > RIP_HOST_LIMIT - context->used)
		size = RIP_HOST_LIMIT - context->used;
	memcpy(context->output + context->used, bytes, size);
	context->used += size;
}

static int
rip_template_index(char byte)
{
	if (byte >= '0' && byte <= '9')
		return byte - '0';
	if (byte >= 'A' && byte <= 'Z')
		return byte - 'A' + 10;
	return -1;
}

static int
rip_store_template(struct cterm_rip_engine *engine, int slot, const char *text,
    struct cterm_rip_mouse_field *button, size_t generation)
{
	char  *replacement;
	char   group[RIP_HOST_LIMIT + 1];
	size_t used = 0;

	if (button != NULL && generation == engine->mouse_generation && button->is_button
	    && ((button->drawing.style.flags & 16384) != 0 || (button->drawing.style.flags2 & 1) != 0)) {
		replacement = button->selected ? strdup(text) : NULL;
		if (button->selected && replacement == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		free(button->template_command);
		button->template_command = replacement;
		slot                     = button->drawing.style.group;
		if (slot < 0 || slot >= 36)
			return 0;
		/* Fields are newest first; prepend to retain creation order. */
		for (struct cterm_rip_mouse_field *field = engine->mouse_fields; field != NULL;
		    field                                = field->next) {
			if (!field->is_button || !field->selected || field->drawing.style.group != slot
			    || field->template_command == NULL
			    || ((field->drawing.style.flags & 16384) == 0
			        && (field->drawing.style.flags2 & 1) == 0))
				continue;
			size_t prefix = strlen(field->template_command);

			if (prefix > RIP_HOST_LIMIT)
				prefix = RIP_HOST_LIMIT;
			if (used > RIP_HOST_LIMIT - prefix)
				used = RIP_HOST_LIMIT - prefix;
			memmove(group + prefix, group, used);
			memcpy(group, field->template_command, prefix);
			used += prefix;
		}
		group[used] = '\0';
		text        = group;
	}
	replacement = strdup(text);
	if (replacement == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	free(engine->templates[slot]);
	engine->templates[slot] = replacement;
	return 0;
}

static int
rip_apply_templates(struct cterm_rip_engine *engine, const char *text, const int *slots, size_t count,
    char **result)
{
	char *input = strdup(text);

	*result = NULL;
	if (input == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t slot = 0; slot < count; slot++) {
		const char *source = engine->templates[slots[slot]];
		char        expanded[RIP_HOST_LIMIT + 1];
		size_t      used = 0;

		if (source == NULL) {
			for (struct cterm_rip_mouse_field *field = engine->mouse_fields; field != NULL;
			    field                                = field->next) {
				if (field->is_button && field->drawing.style.group == slots[slot]) {
					free(input);
					return 0;
				}
			}
			continue;
		}
		while (*source != '\0' && used < RIP_HOST_LIMIT) {
			const char *value   = NULL;
			size_t      advance = 1;

			if (strncmp(source, "$?$", 3) == 0) {
				value   = input;
				advance = 3;
			}
			else if (strncmp(source, "$?", 2) == 0 && source[2] != '\0'
			         && rip_template_index(source[2]) >= 0 && source[3] == '$') {
				value   = engine->templates[rip_template_index(source[2])];
				advance = 4;
				if (value == NULL)
					value = "";
			}
			if (value != NULL) {
				size_t size = strlen(value);

				if (size > RIP_HOST_LIMIT - used)
					size = RIP_HOST_LIMIT - used;
				memcpy(expanded + used, value, size);
				used += size;
			}
			else
				expanded[used++] = *source;
			source += advance;
		}
		expanded[used] = '\0';
		free(input);
		input = strdup(expanded);
		if (input == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	*result = input;
	return 0;
}

static int
rip_dialog_text(st_cterm_t *terminal, const char *text, char **result)
{
	size_t length = strlen(text), used = 0;
	size_t normalized_size;

	if (device(terminal)->engine.raster.unicode_text)
		return st_unicode_text_normalize(result, &normalized_size, text, length, NULL);
	char *unicode = malloc(length * 4 + 1);

	if (unicode == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < length; i++) {
		uint32_t  scalar = st_codepage_to_unicode(terminal->screen.codepage, (uint8_t)text[i], false);
		ptrdiff_t size   = st_utf8_encode_one(unicode + used, length * 4 - used, scalar);

		if (size < 0) {
			free(unicode);
			return (int)size;
		}
		used += (size_t)size;
	}
	int status = st_unicode_text_normalize(result, &normalized_size, unicode, used, NULL);

	free(unicode);
	return status;
}

static int rip_process_host(struct rip_command_context *, const char *, struct cterm_rip_mouse_field *, size_t);

static int
rip_popup(struct rip_command_context *context, const char *text, size_t length,
    struct cterm_rip_mouse_field *button, size_t generation)
{
	st_cterm_t                *terminal      = context->terminal;
	struct st_cterm_rip_choice choices[20]   = {{0}};
	char                      *responses[20] = {0}, *question = NULL, *copy = NULL;
	char                      *options, *heading, *separator;
	size_t                     count = 0;
	bool                       required;
	int                        selected = -1, status = 0;

	if (length > 1024 || device(terminal)->rip_choice == NULL)
		return 0;
	copy = strndup(text, length);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	heading  = copy;
	required = *heading == '*';
	if (required)
		heading++;
	options = strstr(heading, "::");
	if (options == NULL) {
		options = heading;
		heading = "";
	}
	else {
		*options = '\0';
		options += 2;
	}
	status = rip_dialog_text(terminal, *heading == '\0' ? "Choose one of the following:" : heading, &question);
	if (status != 0)
		goto done;
	do {
		char *label, *source, *target;

		if (count == 20)
			goto done;
		separator = strchr(options, ',');
		if (separator != NULL)
			*separator = '\0';
		label = strchr(options, '@');
		if (label != NULL)
			*label++ = '\0';
		else
			label = options;
		responses[count] = strdup(options);
		if (responses[count] == NULL) {
			status = IO_ERROR_OUT_OF_MEMORY;
			goto done;
		}
		count++;
		for (source = target = label; *source != '\0'; source++) {
			if (*source == '_' || *source == '~') {
				if (choices[count - 1].hotkey == 0 && source[1] != '\0') {
					if (device(terminal)->engine.raster.unicode_text) {
						size_t consumed;
						(void)st_utf8_decode_one(&choices[count - 1].hotkey, &consumed,
						    source + 1, strlen(source + 1));
					}
					else
						choices[count - 1].hotkey =
						    st_codepage_to_unicode(terminal->screen.codepage,
							(uint8_t)source[1], false);
				}
				continue;
			}
			*target++ = *source;
		}
		*target             = '\0';
		char *unicode_label = NULL;

		status                   = rip_dialog_text(terminal, label, &unicode_label);
		choices[count - 1].label = unicode_label;
		if (status != 0)
			goto done;
		if (separator != NULL)
			options = separator + 1;
	} while (separator != NULL);
	status = device(terminal)->rip_choice(device(terminal)->rip_choice_context, question, choices, count,
	    required, &selected);
	if (status == 0 && selected >= 0 && (size_t)selected < count)
		status = rip_process_host(context, responses[selected], button, generation);
	else if (status == 0 && selected != -1)
		status = IO_ERROR_INVALID_ARGUMENT;
	else if (status == 0 && required)
		status = IO_ERROR_CANCELLED;
done:
	for (size_t i = 0; i < count; i++) {
		free(responses[i]);
		free((void *)choices[i].label);
	}
	free(question);
	free(copy);
	return status;
}

static int
rip_process_host(struct rip_command_context *context, const char *command, struct cterm_rip_mouse_field *button,
    size_t generation)
{
	st_cterm_t              *terminal = context->terminal;
	struct cterm_rip_engine *engine   = context->engine;
	char                    *copy;
	int                      status = 0;

	if (context->depth >= 64)
		return 0;
	/* Playback and active variables can delete the originating field or query. */
	copy = strdup(command);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	context->depth++;
	for (size_t i = 0; copy[i] != '\0' && status == 0; i++) {
		uint8_t value = (uint8_t)copy[i];

		if (value == '[') {
			int    slots[36];
			size_t count = 0, end = i + 1;
			int    slot = rip_template_index(copy[end]);

			if (slot >= 0 && copy[end + 1] == ':' && copy[end + 2] == ']') {
				status = rip_store_template(engine, slot, copy + end + 3, button, generation);
				break;
			}
			while (copy[end] != '\0' && count < 36 && rip_template_index(copy[end]) >= 0)
				slots[count++] = rip_template_index(copy[end++]);
			if (copy[end] == ']') {
				if (count == 0) {
					i = end;
					continue;
				}
				char *expanded = NULL;

				status = rip_apply_templates(engine, copy + end + 1, slots, count, &expanded);
				if (status == 0 && expanded != NULL)
					status = rip_process_host(context, expanded, button, generation);
				free(expanded);
				break;
			}
		}
		if (value == '(' && copy[i + 1] == '(') {
			const char *end = strstr(copy + i + 2, "))");

			if (end != NULL) {
				status = rip_popup(context, copy + i + 2, (size_t)(end - copy - i - 2), button,
				    generation);
				i      = (size_t)(end - copy) + 1;
				continue;
			}
		}
		if (value == '$') {
			char  *end = strchr(copy + i + 1, '$');
			size_t name_length;

			if (end == NULL)
				continue;
			name_length      = (size_t)(end - copy - i - 1);
			*end             = '\0';
			const char *name = copy + i + 1;

			if (name[0] == '>')
				status = cterm_rip_engine_play_scene(engine, name + 1);
			else if (name_length == 2 && name[0] == '?' && rip_template_index(name[1]) >= 0) {
				const char *text = engine->templates[rip_template_index(name[1])];

				if (text != NULL)
					rip_command_append(context, text, strlen(text));
			}
			else if (rip_variable_token_equal(name, name_length, "COMPAT"))
				status = rip_compat_frame(terminal);
			else {
				char answer[RIP_QUERY_VALUE_SIZE];
				bool handled;

				status = rip_query_variable(terminal, name, name_length, answer, &handled);
				if (status == 0 && handled)
					rip_command_append(context, answer, strlen(answer));
				if (status == 0 && !handled) {
					status = rip_state_variable(terminal, name, name_length, &handled);
					if (status == 0 && !handled && name_length >= 1 && name_length <= 12) {
						struct cterm_rip_user_var *variable =
						    rip_find_variable(engine, name);

						if (variable != NULL && variable->value != NULL)
							rip_command_append(context, variable->value,
							    strlen(variable->value));
					}
				}
			}
			*end = '$';
			i    = (size_t)(end - copy);
			continue;
		}
		if ((value == '^' || value == '`') && copy[i + 1] != '\0') {
			uint8_t next = (uint8_t)copy[i + 1];

			if (next == value) {
				i++;
			}
			else {
				if (next >= 'a' && next <= 'z')
					next -= 'a' - 'A';
				if (next >= '@' && next <= '_') {
					value = next - '@';
					i++;
				}
			}
		}
		rip_command_append(context, &value, 1);
	}
	context->depth--;
	free(copy);
	return status;
}

static int
rip_send_field_command(st_cterm_t *terminal, const char *command, struct cterm_rip_mouse_field *field)
{
	struct cterm_rip_engine    *engine  = &device(terminal)->engine;
	struct rip_command_context  local   = {.terminal = terminal, .engine = engine};
	struct rip_command_context *context = engine->command_context;
	bool                        owner   = context == NULL;
	int                         status;

	if (owner)
		engine->command_context = context = &local;
	status = rip_process_host(context, command, field, engine->mouse_generation);
	if (owner) {
		engine->command_context = NULL;
		if (status == 0 && context->used != 0)
			terminal->keystroke_cb((const char *)context->output, context->used,
			    terminal->keystroke_cbdata);
	}
	return status;
}

static int
rip_send_mouse_command(st_cterm_t *terminal, const char *command)
{
	return rip_send_field_command(terminal, command, NULL);
}

static int
rip_redraw_button(struct cterm_rip_engine *engine, struct cterm_rip_mouse_field *field, bool inverted)
{
	int status = cterm_rip_raster_draw_button(&engine->raster, &field->drawing, inverted);

	field->x2 = field->drawing.x2;
	field->y2 = field->drawing.y2;
	return status;
}

static int
rip_select_button(struct cterm_rip_engine *engine, struct cterm_rip_mouse_field *field)
{
	int status = 0;

	if (!field->is_button)
		return 0;
	if ((field->drawing.style.flags & 16384) != 0) {
		for (struct cterm_rip_mouse_field *other = engine->mouse_fields; other != NULL && status == 0;
		    other                                = other->next) {
			if (other == field || !other->is_button || !other->selected
			    || (other->drawing.style.flags & 16384) == 0
			    || other->drawing.style.group != field->drawing.style.group)
				continue;
			other->selected = false;
			status          = rip_redraw_button(engine, other, false);
		}
		if (status == 0 && !field->selected) {
			field->selected = true;
			status          = rip_redraw_button(engine, field, true);
		}
	}
	else if ((field->drawing.style.flags2 & 1) != 0) {
		field->selected = !field->selected;
		status          = rip_redraw_button(engine, field, field->selected);
	}
	return status;
}

static int
rip_activate_field(st_cterm_t *terminal, struct cterm_rip_mouse_field *field)
{
	bool reset_after = field->is_button && field->reset_after;
	int  status      = rip_select_button(&device(terminal)->engine, field);

	if (status == 0)
		status = rip_send_field_command(terminal, field->command, field);
	/* The command can clear or replace the field list. */
	if (status == 0 && reset_after && device(terminal)->engine.mouse_fields != NULL)
		status = rip_host_command(terminal, 0, 0, '*', "");
	return status;
}

static bool
rip_handle_mouse(st_cterm_t *terminal, const struct st_console_event *event, uint32_t changed)
{
	struct cterm_rip_engine      *engine = &device(terminal)->engine;
	struct cterm_rip_mouse_field *field;
	uint32_t                      cell_width, cell_height;
	int                           x = event->data.mouse.pixel_x;
	int                           y = event->data.mouse.pixel_y;
	int                           status;
	bool                          text_query;
	bool                          graphics_query;

	if (engine == NULL)
		return false;
	if (x < 0 || y < 0) {
		if (event->data.mouse.x < 0 || event->data.mouse.y < 0
		    || st_console_cell_pixel_size(terminal->screen.console, &cell_width, &cell_height) != 0
		    || cell_width == 0 || cell_height == 0 || (uint32_t)event->data.mouse.x > INT_MAX / cell_width
		    || (uint32_t)event->data.mouse.y > INT_MAX / cell_height)
			return false;
		x = (int)((uint32_t)event->data.mouse.x * cell_width);
		y = (int)((uint32_t)event->data.mouse.y * cell_height);
	}
	x                          = cterm_rip_raster_unmap_x(&engine->raster, x);
	y                          = cterm_rip_raster_unmap_y(&engine->raster, y);
	engine->last_mouse_x       = x;
	engine->last_mouse_y       = y;
	engine->last_mouse_buttons = event->data.mouse.buttons;
	if (event->data.mouse.action != ST_CONSOLE_MOUSE_BUTTON || (changed & ST_CONSOLE_MOUSE_BUTTON_LEFT) == 0)
		return false;
	text_query = engine->text_click != NULL && event->data.mouse.x >= terminal->x + terminal->left_margin - 1
	             && event->data.mouse.x <= terminal->x + terminal->right_margin - 1
	             && event->data.mouse.y >= terminal->y + terminal->top_margin - 1
	             && event->data.mouse.y <= terminal->y + terminal->bottom_margin - 1;
	graphics_query = engine->graphics_click != NULL && x >= engine->raster.viewport.sx
	                 && x <= engine->raster.viewport.ex && y >= engine->raster.viewport.sy
	                 && y <= engine->raster.viewport.ey;
	if ((event->data.mouse.buttons & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0) {
		for (field = engine->mouse_fields; field != NULL; field = field->next) {
			if (x >= field->x1 && x <= field->x2 && y >= field->y1 && y <= field->y2)
				break;
		}
		engine->pressed_field = field;
		if (field == NULL)
			return text_query || graphics_query;
		if (field->is_button) {
			const int flags = field->drawing.style.flags;

			if ((flags & 16384) != 0 || (field->drawing.style.flags2 & 1) != 0) {
				status = rip_select_button(engine, field);
				cterm_render_record_error(terminal, status);
			}
			else if (field->invert) {
				status = rip_redraw_button(engine, field, !field->selected);
				cterm_render_record_error(terminal, status);
			}
		}
		else if (field->invert) {
			status = cterm_rip_raster_invert_rectangle(&engine->raster, field->x1, field->y1,
			    field->x2, field->y2);
			cterm_render_record_error(terminal, status);
		}
		return true;
	}
	field                 = engine->pressed_field;
	engine->pressed_field = NULL;
	if (field == NULL) {
		if (text_query) {
			engine->mouse_query.x       = event->data.mouse.x;
			engine->mouse_query.y       = event->data.mouse.y;
			engine->mouse_query.buttons = event->data.mouse.buttons | ST_CONSOLE_MOUSE_BUTTON_LEFT;
			engine->mouse_query.text    = true;
			engine->mouse_query.active  = true;
			status                      = rip_send_mouse_command(terminal, engine->text_click);
			engine->mouse_query.active  = false;
			cterm_render_record_error(terminal, status);
		}
		if (graphics_query) {
			engine->mouse_query.x       = x;
			engine->mouse_query.y       = y;
			engine->mouse_query.buttons = event->data.mouse.buttons | ST_CONSOLE_MOUSE_BUTTON_LEFT;
			engine->mouse_query.text    = false;
			engine->mouse_query.active  = true;
			status                      = rip_send_mouse_command(terminal, engine->graphics_click);
			engine->mouse_query.active  = false;
			cterm_render_record_error(terminal, status);
		}
		return text_query || graphics_query;
	}
	if (field->is_button && field->invert && (field->drawing.style.flags & 16384) == 0
	    && (field->drawing.style.flags2 & 1) == 0) {
		status = rip_redraw_button(engine, field, field->selected);
		cterm_render_record_error(terminal, status);
	}
	else if (!field->is_button && field->invert) {
		status =
		    cterm_rip_raster_invert_rectangle(&engine->raster, field->x1, field->y1, field->x2, field->y2);
		cterm_render_record_error(terminal, status);
	}
	bool reset_after = field->reset_after;

	status = rip_send_field_command(terminal, field->command, field);
	cterm_render_record_error(terminal, status);
	if (reset_after) {
		status = rip_host_command(terminal, 0, 0, '*', "");
		cterm_render_record_error(terminal, status);
	}
	return true;
}

bool
st_rip154_text_key(struct st_rip154 *rip, uint32_t codepoint)
{
	struct cterm_rip_engine *engine;

	st_cterm_t *terminal = rip->terminal;
	engine               = &rip->engine;
	if (engine->tab_highlight != NULL) {
		cterm_rip_engine_clear_tab(engine);
		return false;
	}
	if (!engine->hotkeys_enabled)
		return false;
	if (codepoint >= 'a' && codepoint <= 'z')
		codepoint -= 'a' - 'A';
	for (struct cterm_rip_mouse_field *field = engine->mouse_fields; field != NULL; field = field->next) {
		int      hotkey = (uint8_t)field->drawing.hotkey;
		uint32_t decoded;

		if (!field->is_button)
			continue;
		if (hotkey >= 'a' && hotkey <= 'z')
			hotkey -= 'a' - 'A';
		decoded = hotkey < 0x80
		              ? (uint32_t)hotkey
		              : st_codepage_to_unicode(terminal->screen.codepage, (uint8_t)hotkey, false);
		if (decoded != codepoint)
			continue;
		cterm_render_record_error(terminal, rip_activate_field(terminal, field));
		return true;
	}
	return false;
}

static bool
device_key(void *context, const struct cterm_key *key)
{
	struct st_rip154        *rip      = context;
	st_cterm_t              *terminal = rip->terminal;
	struct cterm_rip_engine *engine   = &rip->engine;
	switch (key->kind) {
		case CTERM_KEY_TEXT:
			return st_rip154_text_key(rip, key->value);
		case CTERM_KEY_CODEPAGE:
			return st_rip154_text_key(rip,
			    st_codepage_to_unicode(terminal->screen.codepage, (uint8_t)key->value, false));
		case CTERM_KEY_DOORWAY:
			if (engine->tab_enabled && key->value == 0x0f) {
				cterm_rip_engine_next_tab(engine, true);
				return true;
			}
			cterm_rip_engine_clear_tab(engine);
			return false;
		case CTERM_KEY_NORMAL:
			break;
		default:
			return false;
	}
	uint32_t key_code  = key->value;
	uint32_t modifiers = key->modifiers;
	if (engine != NULL && engine->tab_enabled && key_code == '\t') {
		/* The historical handler passes false for Tab, true for Backtab. */
		cterm_rip_engine_next_tab(engine, (modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0);
		return true;
	}
	if (engine != NULL && engine->tab_highlight != NULL) {
		struct cterm_rip_mouse_field *field = engine->tab_highlight;

		cterm_rip_engine_clear_tab(engine);
		if (key_code == '\r' || key_code == '\n') {
			cterm_render_record_error(terminal, rip_activate_field(terminal, field));
			return true;
		}
	}
	if (engine != NULL && key_code != 0 && key_code <= 0x7f && st_rip154_text_key(rip, key_code))
		return true;
	return false;
}

static bool
device_mouse(void *context, const struct st_console_event *event, uint32_t changed)
{
	struct st_rip154 *rip = context;
	return rip_handle_mouse(rip->terminal, event, changed);
}

static int
device_mouse_mode(const void *context)
{
	const struct st_rip154 *rip = context;
	return rip->engine.mouse_fields != NULL || rip->engine.text_click != NULL
	       || rip->engine.graphics_click != NULL;
}

static void
device_encoding(void *context, enum st_cterm_encoding encoding, enum st_codepage codepage)
{
	struct st_rip154 *rip            = context;
	rip->engine.raster.text_codepage = codepage;
	rip->engine.raster.unicode_text  = encoding == ST_CTERM_ENCODING_UTF8;
	rip->engine.parser.utf8_input    = encoding == ST_CTERM_ENCODING_UTF8;
}

int
st_rip154_open(struct st_rip154 **result, st_cterm_t **text, const struct st_cterm_config *config,
    const struct st_rip154_services *services)
{
	if (result == NULL || text == NULL || config == NULL || config->emulation != ST_CTERM_ANSI_BBS)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip154 *rip = calloc(1, sizeof(*rip));
	if (rip == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_cterm_config tty = *config;
	tty.graphics_mode          = ST_CTERM_GRAPHICS_OFF;
	tty.force_lcf              = true;
	int status                 = st_cterm_create(&rip->terminal, &tty);
	if (status != 0) {
		free(rip);
		return status;
	}
	st_cterm_t *terminal       = rip->terminal;
	terminal->graphics_mode    = config->graphics_mode;
	terminal->last_column_flag = CTERM_LCF_FORCED | CTERM_LCF_ENABLED;
	terminal->device           = (struct cterm_device_events) {.context = rip,
	    .key                                                  = device_key,
	    .mouse                                                = device_mouse,
	    .mouse_mode                                           = device_mouse_mode,
	    .mouse_passthrough                                    = true,
	    .encoding                                             = device_encoding};
	uint16_t normal;
	st_font_catalog_identifier(config->fonts, config->font_selector, &normal);
	if (services != NULL) {
		rip->rip_prompt           = services->rip_prompt;
		rip->rip_prompt_context   = services->rip_prompt_context;
		rip->rip_choice           = services->rip_choice;
		rip->rip_choice_context   = services->rip_choice_context;
		rip->rip_transfer         = services->rip_transfer;
		rip->rip_transfer_context = services->rip_transfer_context;
		rip->rip_directory        = services->rip_directory;
	}
	const struct cterm_rip_parser_sink sink = {
	    .context                   = terminal,
	    .boundary                  = rip_text_boundary,
	    .passthrough               = rip_passthrough,
	    .command                   = rip_host_command,
	    .control                   = rip_control,
	    .at_carriage_return_column = rip_at_column,
	    .text_disabled             = rip_text_disabled,
	    .music_introducer          = rip_music_introducer,
	    .warning                   = rip_warning,
	};

	status = cterm_rip_engine_init(&device(terminal)->engine, config->console, &sink);
	if (status != 0)
		goto fail;
	device(terminal)->engine.parser.utf8_input = config->encoding == ST_CTERM_ENCODING_UTF8;
	status                                     = st_console_set_full_surface_scroll(config->console, true);
	if (status != 0)
		goto fail;
	device(terminal)->engine.raster.version = config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3 ? 3 : 1;
	device(terminal)->engine.default_text.font_identifier = normal;
	device(terminal)->engine.default_text.codepage =
	    st_font_catalog_codepage(config->fonts, config->font_selector);
	status = st_console_size(config->console, &device(terminal)->engine.default_text.columns,
	    &device(terminal)->engine.default_text.rows);
	if (status == 0)
		status =
		    st_console_cell_pixel_size(config->console, &device(terminal)->engine.default_text.cell_width,
			&device(terminal)->engine.default_text.cell_height);
	if (status != 0)
		goto fail;
	if (config->graphics_mode == ST_CTERM_GRAPHICS_RIP_3)
		device(terminal)->rip_text_font_style = RIP_TEXT_FONT_DEFAULT;
	device(terminal)->engine.raster.text_codepage = config->codepage;
	device(terminal)->engine.raster.unicode_text  = config->encoding == ST_CTERM_ENCODING_UTF8;
	device(terminal)->engine.raster.rip_directory = rip->rip_directory;
	status                                        = rip_load_variables(terminal);
	if (status != 0)
		goto fail;
	*result = rip;
	*text   = terminal;
	return 0;

fail:
	st_rip154_destroy(rip);
	st_cterm_destroy(&terminal);
	return status;
}

void
st_rip154_destroy(struct st_rip154 *rip)
{
	if (rip == NULL)
		return;
	rip->terminal->device = (struct cterm_device_events) {0};
	cterm_rip_engine_destroy(&rip->engine);
	(void)st_console_set_full_surface_scroll(rip->terminal->screen.console, false);
	free(rip);
}

bool
st_rip154_owns_bytes(const struct st_rip154 *rip)
{
	if (rip == NULL)
		return false;
	const struct cterm_rip_parser *parser = &rip->engine.parser;
	return parser->enabled && !parser->suspended
	    && (parser->length > 1 || (parser->length == 1 && parser->command[0] == '|'));
}

int
st_rip154_feed(struct st_rip154 *rip, const void *bytes, size_t size, size_t *used)
{
	return cterm_rip_engine_feed(&rip->engine, bytes, size, used);
}

int
st_rip154_finish(struct st_rip154 *rip)
{
	return cterm_rip_engine_flush(&rip->engine);
}

int
st_rip154_reset(struct st_rip154 *rip)
{
	st_cterm_t *terminal = rip->terminal;
	int         status   = st_rip154_finish(rip);
	if (status == 0)
		status = st_cterm_reset_terminal(terminal);
	if (status != 0)
		return status;
	struct cterm_rip_engine     *engine        = &device(terminal)->engine;
	struct cterm_rip_parser_sink host          = engine->host;
	int                          version       = engine->raster.version;
	enum st_codepage             text_codepage = engine->raster.text_codepage;
	bool                         unicode_text  = engine->raster.unicode_text;

	cterm_start(terminal);
	status = cterm_render_take_error(terminal);
	if (status != 0)
		return status;
	cterm_rip_engine_destroy(engine);
	status = cterm_rip_engine_init(engine, terminal->screen.console, &host);
	if (status != 0)
		return status;
	engine->parser.utf8_input    = terminal->wire_encoding == ST_CTERM_ENCODING_UTF8;
	engine->raster.version       = version;
	engine->raster.text_codepage = text_codepage;
	engine->raster.unicode_text  = unicode_text;
	engine->raster.rip_directory = device(terminal)->rip_directory;
	status                       = rip_load_variables(terminal);
	if (status != 0)
		return status;
	device(terminal)->rip_text_disabled = false;
	return cterm_render_take_error(terminal);
}

int
st_rip154_receive_begin(struct st_rip154 *rip)
{
	return rip == NULL ? IO_ERROR_NOT_SUPPORTED : cterm_rip_engine_receive_begin(&rip->engine);
}

int
st_rip154_receive_data(struct st_rip154 *rip, const uint8_t *bytes, size_t size)
{
	return rip == NULL || (bytes == NULL && size != 0)
		   ? IO_ERROR_INVALID_ARGUMENT
		   : cterm_rip_engine_receive_data(&rip->engine, bytes, size);
}

int
st_rip154_receive_end(struct st_rip154 *rip, bool success)
{
	return rip == NULL ? 0 : cterm_rip_engine_receive_end(&rip->engine, success);
}
