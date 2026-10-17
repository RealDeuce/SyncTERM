/* Incremental terminal-emulation parser and semantic console renderer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_cterm.h"

#include "cterm.h"
#include "cterm_apc.h"
#include "cterm_internal.h"
#include "cterm_rip_engine.h"
#include "cterm_rip_asset.h"
#include "cterm_rip_file_query.h"
#include "cterm_rip_fonts.h"
#include "cterm_rip_time.h"
#include "filesystem.h"
#include "ini.h"
#include "io_error.h"
#include "string_compat.h"
#include "st_builtin_fonts.h"
#include "st_unicode_text.h"
#include "st_utf8.h"
#include "st_threads.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int write_plain(st_cterm_t *terminal, const void *bytes,
    size_t size, size_t *consumed, bool semantic_utf8);
static int filter_input_scalar(st_cterm_t *terminal, uint32_t scalar,
    bool *consumed, char *replacement, size_t *replacement_size);
static int present_filtered_scalar(void *context, uint32_t scalar,
    bool *consumed);
static int capture_scalar(st_cterm_t *terminal, uint32_t scalar);
static int rip_send_mouse_command(st_cterm_t *terminal,
    const char *command);
static int rip_activate_field(st_cterm_t *terminal,
    const struct cterm_rip_mouse_field *field);
static int rip_compat_frame(st_cterm_t *terminal);
static void status_display_changed(struct cterminal *terminal,
    int old_type, int new_type, void *context);
static int resize_scrollback(st_cterm_t *terminal, uint32_t columns,
    struct st_console_cell **result);
static void resize_parser_geometry(st_cterm_t *terminal, int columns,
    int rows, int cursor_x, int cursor_y);

/* RIPv3 reset restores the session font, outside RIP's six text styles. */
enum { RIP_TEXT_FONT_DEFAULT = 6 };

static int
rip_text_font(st_cterm_t *terminal, int style)
{
	struct st_console_font font;
	struct st_console_custom_cursor cursor;
	struct st_console_cell *scrollback = NULL;
	struct st_console_cell *old_scrollback;
	const uint8_t *bitmap;
	size_t bitmap_size;
	uint32_t cell_width, cell_height, columns, rows, old_width, old_height;
	uint32_t old_cell_width, old_cell_height;
	uint32_t status_rows;
	uint16_t identifier;
	int status;

	if (style < 0 || style > RIP_TEXT_FONT_DEFAULT)
		return 0;
	if (style == RIP_TEXT_FONT_DEFAULT) {
		const struct cterm_rip_engine *engine = terminal->rip_engine;

		cell_width = engine->default_text.cell_width;
		cell_height = engine->default_text.cell_height;
		columns = engine->default_text.columns;
		rows = engine->default_text.rows;
		identifier = engine->default_text.font_identifier;
	}
	else {
		cell_width = style == 1 || style == 3 ? 7
		    : style == 4 ? 16 : 8;
		cell_height = style < 2 ? 8 : style == 5 ? 16 : 14;
		columns = style == 1 || style == 3 ? 91
		    : style == 4 ? 40 : 80;
		rows = cell_height == 8 ? 43 : cell_height == 16 ? 21 : 25;
	}
	status_rows = terminal->status_display_type >= 1 ? 1 : 0;
	if (rows <= status_rows + terminal->api_reserved_rows)
		return IO_ERROR_OUT_OF_RANGE;
	if (style != RIP_TEXT_FONT_DEFAULT
	    && !terminal->rip_text_font_registered[style]) {
		if (!st_builtin_font_get(45, 8, 8, &font))
			return IO_ERROR_NOT_SUPPORTED;
		if (style == 0 || style == 2 || style == 5) {
			if (!st_builtin_font_get(style == 5 ? 0 : 45,
			    cell_width, cell_height,
			    &font))
				return IO_ERROR_NOT_SUPPORTED;
		}
		else {
			bitmap = cterm_rip_text_font_data(style, &bitmap_size);
			if (bitmap == NULL)
				return IO_ERROR_NOT_SUPPORTED;
			font.bitmap = bitmap;
			font.bitmap_size = bitmap_size;
			font.width = cell_width;
			font.height = cell_height;
			font.row_stride = (cell_width + 7U) / 8U;
			font.glyph_stride = font.row_stride * cell_height;
		}
		status = st_console_register_static_font(terminal->screen.console,
		    &font, &identifier);
		if (status != 0)
			return status;
		terminal->rip_text_font_identifiers[style] = identifier;
		terminal->rip_text_font_registered[style] = true;
	}
	if (style != RIP_TEXT_FONT_DEFAULT)
		identifier = terminal->rip_text_font_identifiers[style];
	status = st_console_size(terminal->screen.console, &old_width,
	    &old_height);
	if (status != 0)
		return status;
	status = st_console_cell_pixel_size(terminal->screen.console,
	    &old_cell_width, &old_cell_height);
	if (status != 0)
		return status;
	if (old_cell_height != cell_height) {
		status = st_console_get_default_cursor(
		    terminal->screen.console, &cursor);
		if (status != 0)
			return status;
		cursor.range = cell_height;
		cursor.start = cell_height - (cell_height > 8 ? 2U : 1U);
		cursor.end = cell_height - 1U;
	}
	if (columns != old_width || rows != old_height
	    || cell_width != old_cell_width || cell_height != old_cell_height) {
		if (terminal->backlines != 0) {
			status = resize_scrollback(terminal, columns, &scrollback);
			if (status != 0)
				return status;
		}
		status = st_console_regrid_preserve_pixels(
		    terminal->screen.console, columns, rows, cell_width,
		    cell_height);
		if (status != 0) {
			free(scrollback);
			return status;
		}
		old_scrollback = terminal->scrollback;
		terminal->scrollback = scrollback;
		terminal->backwidth = (int)columns;
		terminal->backstart = 0;
		terminal->backpos = terminal->backfilled == terminal->backlines
		    ? 0 : terminal->backfilled;
		free(old_scrollback);
		resize_parser_geometry(terminal, (int)columns,
		    (int)(rows - status_rows - terminal->api_reserved_rows), 1, 1);
		if (terminal->status_sub != NULL) {
			terminal->status_sub->x = terminal->x;
			terminal->status_sub->y = terminal->y + terminal->height;
			resize_parser_geometry(terminal->status_sub,
			    (int)columns, 1, 1, 1);
		}
		if (old_cell_height != cell_height) {
			status = st_console_set_default_cursor(
			    terminal->screen.console, &cursor);
			if (status != 0)
				return status;
		}
	}
	for (size_t selector = 0; selector < UINT8_MAX + 1U; selector++) {
		terminal->screen.font_identifiers[selector] = identifier;
		terminal->screen.font_valid[selector] = true;
	}
	cterm_render_attribute_colours(terminal, terminal->attr,
	    &terminal->fg_color, &terminal->bg_color);
	if ((terminal->screen.capabilities
	    & ST_CONSOLE_CAP_EXTENDED_PALETTE) != 0) {
		terminal->fg_color += 16;
		terminal->bg_color += 16;
	}
	cterm_render_set_attribute(terminal, terminal->attr);
	cterm_render_set_colours(terminal, terminal->fg_color,
	    terminal->bg_color);
	/* Legacy reinit_screen() starts its replacement CTerm before restoring
	 * the saved pixel canvas.  A later plain-text byte must not start it
	 * again and erase graphics beneath the text window. */
	terminal->started = 1;
	terminal->rip_text_font_style = style;
	return cterm_render_take_error(terminal);
}

static int
rip_passthrough(void *context, const uint8_t *bytes, size_t size)
{
	st_cterm_t *terminal = context;
	size_t consumed;
	int status = write_plain(terminal, bytes, size, &consumed, false);

	return status == 0 && consumed != size ? IO_ERROR_IO : status;
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
	version = terminal->rip_engine->raster.version == 3
	    ? "RIPSCRIP030001" : "RIPSCRIP015410";
	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context,
		    ST_CTERM_OUTPUT_RESPONSE, (const uint8_t *)version, 14);
	return 0;
}

static bool
rip_music_introducer(void *context, uint8_t final)
{
	st_cterm_t *terminal = context;

	return final == '|'
	    || (final == 'M' && terminal->music_enable == CTERM_MUSIC_ENABLED)
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

	return terminal->rip_text_disabled;
}

static int
rip_digit(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'A' && value <= 'Z')
		return value - 'A' + 10;
	if (value >= 'a' && value <= 'z')
		return value - 'a' + 10;
	return -1;
}

static bool
rip_number(const char *text, size_t width, int *value)
{
	int number = 0;

	for (size_t i = 0; i < width; i++) {
		int digit = rip_digit(text[i]);

		if (digit < 0)
			return false;
		number = number * 36 + digit;
	}
	*value = number;
	return true;
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
rip_variable_token_equal(const char *text, size_t length,
	const char *name)
{
	for (size_t i = 0; i < length; i++) {
		if (name[i] == '\0'
		    || rip_ascii_upper(text[i]) != rip_ascii_upper(name[i]))
			return false;
	}
	return name[length] == '\0';
}

static int rip_host_command(void *context, int level, int sublevel,
	uint8_t code, const char *arguments);

static int
rip_save_text_window(st_cterm_t *terminal)
{
	struct cterm_rip_raster *raster = &terminal->rip_engine->raster;
	bool visible;
	enum st_console_cursor_shape shape;
	int status = st_console_get_cursor(terminal->screen.console,
	    &visible, &shape);

	if (status != 0)
		return status;
	terminal->rip_saved_text.x = raster->x;
	terminal->rip_saved_text.y = raster->y;
	terminal->rip_saved_text.left = terminal->left_margin;
	terminal->rip_saved_text.top = terminal->top_margin;
	terminal->rip_saved_text.right = terminal->right_margin;
	terminal->rip_saved_text.bottom = terminal->bottom_margin;
	terminal->rip_saved_text.font_style = terminal->rip_text_font_style;
	terminal->rip_saved_text.wrap =
	    (terminal->extattr & CTERM_EXTATTR_AUTOWRAP) != 0;
	terminal->rip_saved_text.disabled = terminal->rip_text_disabled;
	terminal->rip_saved_text.cursor_visible = visible;
	terminal->rip_saved_text.cursor_shape = shape;
	terminal->rip_saved_text.attr = terminal->attr;
	terminal->rip_saved_text.foreground = terminal->fg_color;
	terminal->rip_saved_text.background = terminal->bg_color;
	return 0;
}

static int
rip_restore_text_window(st_cterm_t *terminal)
{
	struct cterm_rip_raster *raster = &terminal->rip_engine->raster;
	bool window_changed = terminal->left_margin
	    != terminal->rip_saved_text.left
	    || terminal->top_margin != terminal->rip_saved_text.top
	    || terminal->right_margin != terminal->rip_saved_text.right
	    || terminal->bottom_margin != terminal->rip_saved_text.bottom
	    || terminal->rip_text_font_style
	    != terminal->rip_saved_text.font_style;
	int status;

	if (terminal->rip_text_font_style
	    != terminal->rip_saved_text.font_style) {
		status = rip_text_font(terminal,
		    terminal->rip_saved_text.font_style);
		if (status != 0)
			return status;
	}
	terminal->left_margin = terminal->rip_saved_text.left < 1
	    ? 1 : terminal->rip_saved_text.left;
	terminal->top_margin = terminal->rip_saved_text.top < 1
	    ? 1 : terminal->rip_saved_text.top;
	terminal->right_margin = terminal->rip_saved_text.right < 1
	    ? 1 : terminal->rip_saved_text.right;
	terminal->bottom_margin = terminal->rip_saved_text.bottom < 1
	    ? 1 : terminal->rip_saved_text.bottom;
	if (terminal->left_margin > terminal->width)
		terminal->left_margin = terminal->width;
	if (terminal->top_margin > terminal->height)
		terminal->top_margin = terminal->height;
	if (terminal->right_margin > terminal->width)
		terminal->right_margin = terminal->width;
	if (terminal->bottom_margin > terminal->height)
		terminal->bottom_margin = terminal->height;
	if (terminal->rip_saved_text.wrap)
		terminal->extattr |= CTERM_EXTATTR_AUTOWRAP;
	else
		terminal->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
	terminal->extattr |= CTERM_EXTATTR_ORIGINMODE;
	terminal->rip_text_disabled = terminal->rip_saved_text.disabled
	    || (terminal->rip_saved_text.left == 0
	    && terminal->rip_saved_text.top == 0
	    && terminal->rip_saved_text.right == 0
	    && terminal->rip_saved_text.bottom == 0);
	if (window_changed) {
		cterm_apply_window(terminal);
		cterm_set_position(terminal, 1, 1);
	}
	raster->x = terminal->rip_saved_text.x;
	raster->y = terminal->rip_saved_text.y;
	terminal->attr = terminal->rip_saved_text.attr;
	terminal->fg_color = terminal->rip_saved_text.foreground;
	terminal->bg_color = terminal->rip_saved_text.background;
	cterm_render_set_attribute(terminal, terminal->attr);
	cterm_render_set_colours(terminal, terminal->fg_color,
	    terminal->bg_color);
	status = cterm_render_take_error(terminal);
	if (status != 0)
		return status;
	return st_console_set_cursor(terminal->screen.console,
	    terminal->rip_saved_text.cursor_visible,
	    terminal->rip_saved_text.cursor_shape);
}

enum { RIP_QUERY_VALUE_SIZE = 128 };

/* The old RIP MUSIC/PHASER samples use a phase-continuous linear sweep. */
static double
rip_sweep(double phase, unsigned start, unsigned end, size_t frames,
	int16_t *output)
{
	const double two_pi = 6.28318530717958647692;
	const double scale = two_pi / AUDIO_SAMPLE_RATE;
	double initial, delta;

	if (start < end) {
		initial = -scale * start;
		delta = scale * (end - start) / frames;
	}
	else {
		initial = scale * start;
		delta = -scale * (start - end) / frames;
	}
	for (size_t i = 0; i < frames; i++) {
		int16_t sample;

		phase += initial + i * delta;
		if (phase > two_pi)
			phase -= two_pi;
		sample = (int16_t)(sin(phase) * 8192.0);
		output[2 * i] = sample;
		output[2 * i + 1] = sample;
	}
	return phase;
}

static int
rip_sound_variable(st_cterm_t *terminal, const char *name, size_t length,
	bool *handled)
{
	const struct timespec beep_pause = {.tv_sec = 0, .tv_nsec = 75000000};
	const struct timespec blip_pause = {.tv_sec = 0, .tv_nsec = 10000000};
	int16_t *wave;
	size_t frames;

	*handled = true;
	if (rip_variable_token_equal(name, length, "ALARM")) {
		for (int i = 0; i < 3; i++) {
			(void)cterm_play_fx_tone(terminal, 320, 200,
			    AUDIO_WAVE_SINE);
			(void)cterm_play_fx_tone(terminal, 160, 425,
			    AUDIO_WAVE_SINE);
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
		wave = malloc(frames * AUDIO_FRAME_SIZE);
		if (wave == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (audio_generate_tone(wave, frames, 50, AUDIO_WAVE_SINE,
		    false) == 0)
			(void)cterm_play_fx(terminal, wave, frames);
		free(wave);
		(void)st_thread_sleep(&blip_pause, NULL);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "MUSIC")
	    || rip_variable_token_equal(name, length, "PHASER")
	    || rip_variable_token_equal(name, length, "REVPHASER")) {
		bool music = rip_variable_token_equal(name, length, "MUSIC");

		frames = music ? 15872 : 10760;
		wave = malloc(frames * AUDIO_FRAME_SIZE);
		if (wave == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (music) {
			double phase = 0.0;

			for (size_t i = 0; i < 4; i++) {
				int16_t *part = wave + 2 * i * 3968;

				phase = rip_sweep(phase, 1300, 700, 3086,
				    part);
				phase = rip_sweep(phase, 700, 850, 442,
				    part + 2 * 3086);
				phase = rip_sweep(phase, 850, 1300, 440,
				    part + 2 * 3528);
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
rip_clock_variable(const char *name, size_t length,
	char result[RIP_QUERY_VALUE_SIZE], bool *handled)
{
	static const char *const weekdays[7] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
	};
	static const char *const months[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};
	struct tm local;
	time_t now;
	const char *format = NULL;

	*handled = rip_variable_token_equal(name, length, "ADOW")
	    || rip_variable_token_equal(name, length, "DATE")
	    || rip_variable_token_equal(name, length, "DATETIME")
	    || rip_variable_token_equal(name, length, "DAY")
	    || rip_variable_token_equal(name, length, "DOW")
	    || rip_variable_token_equal(name, length, "DOY")
	    || rip_variable_token_equal(name, length, "MONTH")
	    || rip_variable_token_equal(name, length, "MONTHNUM")
	    || rip_variable_token_equal(name, length, "YEAR")
	    || rip_variable_token_equal(name, length, "FYEAR")
	    || rip_variable_token_equal(name, length, "WDAY")
	    || rip_variable_token_equal(name, length, "WOY")
	    || rip_variable_token_equal(name, length, "WOYM")
	    || rip_variable_token_equal(name, length, "AMPM")
	    || rip_variable_token_equal(name, length, "HOUR")
	    || rip_variable_token_equal(name, length, "MHOUR")
	    || rip_variable_token_equal(name, length, "MIN")
	    || rip_variable_token_equal(name, length, "SEC")
	    || rip_variable_token_equal(name, length, "TIME")
	    || rip_variable_token_equal(name, length, "TIMEZONE");
	if (!*handled)
		return 0;
	now = time(NULL);
	if (now == (time_t)-1 || !cterm_rip_localtime(now, &local))
		return IO_ERROR_IO;
	if (rip_variable_token_equal(name, length, "DATE"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d/%02d/%02d",
		    local.tm_mon + 1, local.tm_mday, local.tm_year % 100);
	else if (rip_variable_token_equal(name, length, "DATETIME")) {
		if (local.tm_wday < 0 || local.tm_wday >= 7
		    || local.tm_mon < 0 || local.tm_mon >= 12)
			return IO_ERROR_OUT_OF_RANGE;
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE,
		    "%s %s %2d %02d:%02d:%02d %04d\n",
		    weekdays[local.tm_wday], months[local.tm_mon], local.tm_mday,
		    local.tm_hour, local.tm_min, local.tm_sec,
		    local.tm_year + 1900);
	}
	else if (rip_variable_token_equal(name, length, "DAY"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_mday);
	else if (rip_variable_token_equal(name, length, "DOY"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%03d",
		    local.tm_yday + 1);
	else if (rip_variable_token_equal(name, length, "MONTHNUM"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_mon + 1);
	else if (rip_variable_token_equal(name, length, "YEAR"))
		/* Preserve the legacy parser's month-index substitution. */
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_mon % 100);
	else if (rip_variable_token_equal(name, length, "FYEAR"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%04d",
		    local.tm_mon + 1900);
	else if (rip_variable_token_equal(name, length, "WDAY"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_wday);
	else if (rip_variable_token_equal(name, length, "AMPM"))
		strcpy(result, local.tm_hour < 12 ? "AM" : "PM");
	else if (rip_variable_token_equal(name, length, "MHOUR"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_hour);
	else if (rip_variable_token_equal(name, length, "MIN"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_min);
	else if (rip_variable_token_equal(name, length, "SEC"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d",
		    local.tm_sec);
	else if (rip_variable_token_equal(name, length, "TIME"))
		(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%02d:%02d:%02d",
		    local.tm_hour, local.tm_min, local.tm_sec);
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
rip_query_variable(st_cterm_t *terminal, const char *name,
	size_t length, char result[RIP_QUERY_VALUE_SIZE], bool *handled)
{
	uint32_t cell_width, cell_height;
	bool visible;
	enum st_console_cursor_shape shape;
	int x, y, value;
	int status;

	*handled = true;
	status = rip_clock_variable(name, length, result, handled);
	if (status != 0 || *handled)
		return status;
	*handled = true;
	if (rip_variable_token_equal(name, length, "RIPVER")) {
		memcpy(result, terminal->rip_engine->raster.version == 3
		    ? "RIPSCRIP030001" : "RIPSCRIP015410", 15);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "TWIN")) {
		strcpy(result, terminal->rip_text_disabled ? "NO" : "YES");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "STATBAR")) {
		strcpy(result, terminal->status_display_type == 0
		    ? "NO" : "YES");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "CURSOR")) {
		status = st_console_get_cursor(terminal->screen.console,
		    &visible, &shape);
		if (status != 0)
			return status;
		strcpy(result, visible && !terminal->rip_text_disabled
		    && !(terminal->left_margin == 1
		    && terminal->top_margin == 1
		    && terminal->right_margin == 1
		    && terminal->bottom_margin == 1) ? "YES" : "NO");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "MSTAT")) {
		strcpy(result, "YES");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "X")
	    || rip_variable_token_equal(name, length, "Y")
	    || rip_variable_token_equal(name, length, "XY")
	    || rip_variable_token_equal(name, length, "XYM")
	    || rip_variable_token_equal(name, length, "M")) {
		struct cterm_rip_engine *engine = terminal->rip_engine;
		int mx = engine->mouse_query.active ? engine->mouse_query.x
		    : engine->last_mouse_x;
		int my = engine->mouse_query.active ? engine->mouse_query.y
		    : engine->last_mouse_y;
		uint32_t buttons = engine->mouse_query.active
		    ? engine->mouse_query.buttons : engine->last_mouse_buttons;
		int width = engine->mouse_query.active && engine->mouse_query.text
		    ? 2 : 4;

		if (rip_variable_token_equal(name, length, "X"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*d", width, mx);
		else if (rip_variable_token_equal(name, length, "Y"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*d", width, my);
		else if (rip_variable_token_equal(name, length, "M"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%u%u%u",
			    (unsigned)(buttons & 1),
			    (unsigned)((buttons >> 1) & 1),
			    (unsigned)((buttons >> 2) & 1));
		else if (rip_variable_token_equal(name, length, "XY"))
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%0*x:%0*x",
			    width, mx, width, my);
		else
			(void)snprintf(result, RIP_QUERY_VALUE_SIZE,
			    "%0*x:%0*x:%u%u%u",
			    width, mx, width, my,
			    (unsigned)(buttons & 1),
			    (unsigned)((buttons >> 1) & 1),
			    (unsigned)((buttons >> 2) & 1));
		return 0;
	}
	if (rip_variable_token_equal(name, length, "CURX")
	    || rip_variable_token_equal(name, length, "CURY")) {
		cterm_coord_get_xy(terminal, CTERM_COORD_CURR, &x, &y);
		value = rip_variable_token_equal(name, length, "CURX")
		    ? x - 1 : y - 1;
	}
	else if (rip_variable_token_equal(name, length, "TWFONT")) {
		value = 0;
		if (!terminal->rip_text_disabled) {
			status = st_console_cell_pixel_size(
			    terminal->screen.console, &cell_width, &cell_height);
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
		value = terminal->rip_text_disabled ? 0
		    : terminal->right_margin - terminal->left_margin + 1;
	else if (rip_variable_token_equal(name, length, "TWH"))
		value = terminal->rip_text_disabled ? 0
		    : terminal->bottom_margin - terminal->top_margin + 1;
	else if (rip_variable_token_equal(name, length, "TWX0"))
		value = terminal->rip_text_disabled ? 0
		    : terminal->left_margin - 1;
	else if (rip_variable_token_equal(name, length, "TWX1"))
		value = terminal->rip_text_disabled ? 0
		    : terminal->right_margin - 1;
	else if (rip_variable_token_equal(name, length, "TWY0"))
		value = terminal->rip_text_disabled ? 0
		    : terminal->top_margin - 1;
	else if (rip_variable_token_equal(name, length, "TWY1"))
		value = terminal->rip_text_disabled ? 0
		    : terminal->bottom_margin - 1;
	else {
		*handled = false;
		result[0] = '\0';
		return 0;
	}
	(void)snprintf(result, RIP_QUERY_VALUE_SIZE, "%d", value);
	return 0;
}

static int
rip_state_variable(st_cterm_t *terminal, const char *name,
	size_t length, bool *handled)
{
	struct cterm_rip_raster *raster = &terminal->rip_engine->raster;
	int status;

	status = rip_sound_variable(terminal, name, length, handled);
	if (status != 0 || *handled)
		return status;
	/* These legacy built-ins are present but have no implementation there. */
	if ((length == 4 && rip_variable_token_equal(name, 3, "APP")
	    && name[3] >= '0' && name[3] <= '9')
	    || rip_variable_token_equal(name, length, "VT102ON")
	    || rip_variable_token_equal(name, length, "VT102OFF")) {
		*handled = true;
		return 0;
	}
	if (rip_variable_token_equal(name, length, "COFF")
	    || rip_variable_token_equal(name, length, "CON")) {
		*handled = true;
		return st_console_set_cursor(terminal->screen.console,
		    rip_variable_token_equal(name, length, "CON"),
		    ST_CONSOLE_CURSOR_UNDERLINE);
	}
	if (rip_variable_token_equal(name, length, "DTW")) {
		*handled = true;
		terminal->rip_text_disabled = true;
		return 0;
	}
	if (rip_variable_token_equal(name, length, "DWAYON")
	    || rip_variable_token_equal(name, length, "DWAYOFF")) {
		*handled = true;
		terminal->doorway_mode =
		    rip_variable_token_equal(name, length, "DWAYON");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "HKEYON")
	    || rip_variable_token_equal(name, length, "HKEYOFF")) {
		*handled = true;
		terminal->rip_engine->hotkeys_enabled =
		    rip_variable_token_equal(name, length, "HKEYON");
		return 0;
	}
	if (rip_variable_token_equal(name, length, "TABON")
	    || rip_variable_token_equal(name, length, "TABOFF")) {
		*handled = true;
		terminal->rip_engine->tab_enabled =
		    rip_variable_token_equal(name, length, "TABON");
		if (!terminal->rip_engine->tab_enabled)
			cterm_rip_engine_clear_tab(terminal->rip_engine);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "SBARON")
	    || rip_variable_token_equal(name, length, "SBAROFF")) {
		int old = terminal->status_display_type;
		int wanted = rip_variable_token_equal(name, length, "SBARON")
		    ? 1 : 0;

		*handled = true;
		terminal->status_display_type = wanted;
		if (old != wanted)
			status_display_changed(terminal, old, wanted, terminal);
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
		return cterm_rip_raster_command(raster, 0, 0, 'E', "",
		    &raster_handled);
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
		return cterm_rip_engine_save_mouse(terminal->rip_engine);
	}
	if (rip_variable_token_equal(name, length, "RMF")) {
		*handled = true;
		return cterm_rip_engine_restore_mouse(terminal->rip_engine);
	}
	if (rip_variable_token_equal(name, length, "MKILL")) {
		*handled = true;
		cterm_rip_engine_clear_mouse(terminal->rip_engine);
		return 0;
	}
	if (rip_variable_token_equal(name, length, "SAVEALL")) {
		*handled = true;
		status = rip_save_text_window(terminal);
		if (status == 0)
			status = cterm_rip_raster_save_clipboard(raster);
		if (status == 0)
			status = cterm_rip_engine_save_mouse(terminal->rip_engine);
		if (status == 0)
			status = cterm_rip_raster_save_screen(raster, 10);
		return status;
	}
	if (rip_variable_token_equal(name, length, "RESTOREALL")) {
		*handled = true;
		status = rip_restore_text_window(terminal);
		if (status == 0)
			status = cterm_rip_raster_restore_clipboard(raster);
		if (status == 0)
			status = cterm_rip_engine_restore_mouse(
			    terminal->rip_engine);
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
	if (length == 5 && name[4] >= '0' && name[4] <= '9'
	    && rip_variable_token_equal(name, 4, "SAVE")) {
		*handled = true;
		return cterm_rip_raster_save_screen(raster, name[4] - '0');
	}
	if (length == 8 && name[7] >= '1' && name[7] <= '9'
	    && rip_variable_token_equal(name, 7, "RESTORE")) {
		*handled = true;
		return cterm_rip_raster_restore_screen(
		    &terminal->rip_engine->raster, name[7] - '0', true);
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

		if (!((ch >= 'A' && ch <= 'Z')
		    || (ch >= 'a' && ch <= 'z')
		    || (i != 0 && ((ch >= '0' && ch <= '9') || ch == '_'))))
			return false;
	}
	return true;
}

static struct cterm_rip_user_var *
rip_find_variable(struct cterm_rip_engine *engine, const char *name)
{
	for (struct cterm_rip_user_var *variable = engine->user_vars;
	    variable != NULL; variable = variable->next) {
		if (rip_variable_name_equal(variable->name, name))
			return variable;
	}
	return NULL;
}

static int
rip_set_variable(struct cterm_rip_engine *engine, const char *name,
    const char *value, int width, bool persistent)
{
	struct cterm_rip_user_var *variable = rip_find_variable(engine, name);
	char *copy = strdup(value);

	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (variable == NULL) {
		variable = calloc(1, sizeof(*variable));
		if (variable == NULL) {
			free(copy);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		memcpy(variable->name, name, strlen(name) + 1);
		variable->next = engine->user_vars;
		engine->user_vars = variable;
	}
	free(variable->value);
	variable->value = copy;
	variable->field_width = width;
	variable->persistent = persistent;
	return 0;
}

static FILE *
rip_open_variable_file(const char *directory)
{
	struct fs_info info;
	char *path = cterm_rip_asset_path(directory, "variables.ini", false);
	FILE *stream = NULL;

	if (path != NULL
	    && fs_get_link_info(&info, directory) == 0
	    && info.kind == FS_ENTRY_DIRECTORY
	    && fs_get_link_info(&info, path) == 0
	    && info.kind == FS_ENTRY_FILE)
		(void)fs_open(&stream, path, "rb");
	free(path);
	return stream;
}

static int
rip_load_variables(st_cterm_t *terminal)
{
	ini_document_t *document = NULL;
	ini_section_t *root;
	FILE *stream;
	int status = 0;

	stream = rip_open_variable_file(terminal->rip_directory);
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
		    || !rip_variable_name_valid(item->name,
		    strlen(item->name)))
			continue;
		status = rip_set_variable(terminal->rip_engine, item->name,
		    item->value, 60, true);
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
	struct fs_info info;
	ini_document_t *document = NULL;
	ini_section_t *root;
	char *path = NULL, *serialized = NULL;
	FILE *stream = NULL;
	size_t size;
	int status;

	variable = rip_find_variable(terminal->rip_engine, name);
	if (variable == NULL)
		return;
	path = cterm_rip_asset_path(terminal->rip_directory,
	    "variables.ini", false);
	if (path == NULL)
		return;
	status = fs_get_link_info(&info, terminal->rip_directory);
	if (status == IO_ERROR_NOT_FOUND) {
		status = fs_create_directories(terminal->rip_directory);
		if (status == 0)
			status = fs_get_link_info(&info, terminal->rip_directory);
	}
	if (status != 0 || info.kind != FS_ENTRY_DIRECTORY)
		goto done;
	status = fs_get_link_info(&info, path);
	if (status != IO_ERROR_NOT_FOUND
	    && (status != 0 || info.kind != FS_ENTRY_FILE))
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
	root = ini_document_root(document);
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
rip_parse_definition(const char *text, char name[13], int *width,
    char **question, char **default_value)
{
	const char *colon = strchr(text, ':');
	const char *comma;
	size_t length;

	*question = NULL;
	*default_value = NULL;
	*width = 60;
	if (colon == NULL)
		return false;
	comma = memchr(text, ',', (size_t)(colon - text));
	length = (size_t)((comma == NULL ? colon : comma) - text);
	if (!rip_variable_name_valid(text, length))
		return false;
	memcpy(name, text, length);
	name[length] = '\0';
	if (comma != NULL) {
		long requested = strtol(comma + 1, NULL, 10);

		*width = requested < 1 ? 1 : requested > 1023 ? 1023
		    : (int)requested;
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
	struct cterm_rip_engine *engine = terminal->rip_engine;
	struct cterm_rip_user_var *existing;
	char name[13], fallback[80], *question, *default_value, *entered = NULL;
	char *normalized = NULL;
	const char *initial, *prompt;
	size_t normalized_size;
	int flags, reserved, width, parsed, status = 0;
	bool save = false, reject_blank, noninteractive;

	if (strlen(arguments) < 5
	    || !rip_number(arguments, 3, &flags)
	    || !rip_number(arguments + 3, 2, &reserved))
		return 0;
	parsed = rip_parse_definition(arguments + 5, name, &width,
	    &question, &default_value);
	if (parsed == 0)
		return 0;
	if (parsed < 0) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	save = (flags & 1) != 0;
	reject_blank = (flags & 2) != 0;
	noninteractive = (flags & 4) != 0;
	existing = rip_find_variable(engine, name);
	if (noninteractive && existing != NULL) {
		if (default_value != NULL && default_value[0] != '\0')
			status = rip_set_variable(engine, name, default_value,
			    width, save);
		goto done;
	}
	if (terminal->rip_prompt == NULL)
		goto done;
	initial = existing != NULL && existing->value != NULL
	    ? existing->value : default_value != NULL ? default_value : "";
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
		status = terminal->rip_prompt(terminal->rip_prompt_context,
		    prompt, initial, (size_t)width, reject_blank, &entered);
		if (status != 0 || entered == NULL)
			break;
		status = st_unicode_text_normalize(&normalized,
		    &normalized_size, entered, strlen(entered), NULL);
		if (status != 0)
			break;
		size_t position = 0, characters = 0;

		while (position < normalized_size && characters <= (size_t)width) {
			uint32_t codepoint;
			size_t consumed;

			status = st_utf8_decode_one(&codepoint, &consumed,
			    normalized + position, normalized_size - position);
			if (status != 0)
				break;
			position += consumed;
			characters++;
		}
		if (status != 0)
			break;
		if (characters <= (size_t)width
		    && (!reject_blank || normalized_size != 0))
			break;
	} while (true);
	if (status == 0 && normalized != NULL
	    && (normalized_size != 0 || !reject_blank))
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
rip_host_command(void *context, int level, int sublevel,
    uint8_t code, const char *arguments)
{
	st_cterm_t *terminal = context;
	struct cterm_rip_raster *raster = &terminal->rip_engine->raster;
	int first, second, third, fourth;
	int coords[4], wrap, style;
	int mouse[9];
	uint32_t previous_background;

	if (level == 1 && sublevel == 0) {
		switch (code) {
		case 'D':
			return rip_define_variable(terminal, arguments);
		case 'F': {
			char *response = NULL;
			size_t response_size = 0;
			int status = cterm_rip_file_query(&response,
			    &response_size, terminal->rip_directory, arguments);

			if (status != 0)
				return status;
			if (response != NULL && terminal->api_output != NULL)
				terminal->api_output(terminal->api_output_context,
				    ST_CTERM_OUTPUT_RESPONSE,
				    (const uint8_t *)response, response_size);
			free(response);
			return 0;
		}
		case 0x1b: {
			char **stored;
			char *replacement;

			if (strlen(arguments) < 4
			    || !rip_number(arguments, 1, &first)
			    || !rip_number(arguments + 1, 3, &second))
				return 0;
			if (first == 0)
				return rip_send_mouse_command(terminal,
				    arguments + 4);
			if (first != 1 && first != 2)
				return 0;
			stored = first == 1
			    ? &terminal->rip_engine->graphics_click
			    : &terminal->rip_engine->text_click;
			if (strcmp(arguments + 4, "$OFF$") == 0) {
				free(*stored);
				*stored = NULL;
				return 0;
			}
			replacement = strdup(arguments + 4);
			if (replacement == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
			free(*stored);
			*stored = replacement;
			return 0;
		}
		case 'K':
			cterm_rip_engine_clear_mouse(terminal->rip_engine);
			return 0;
		case 'M': {
			static const uint8_t widths[9] = {
				2, 2, 2, 2, 2, 1, 1, 4, 1
			};
			size_t offset = 0;

			for (size_t i = 0; i < 9; i++) {
				if (!rip_number(arguments + offset, widths[i],
				    &mouse[i]))
					return 0;
				offset += widths[i];
			}
			if (mouse[3] > raster->viewport.ex
			    - raster->viewport.sx
			    || mouse[4] > raster->viewport.ey
			    - raster->viewport.sy
			    || mouse[5] > 1 || mouse[6] > 1
			    || mouse[1] > mouse[3] || mouse[2] > mouse[4])
				return 0;
			return cterm_rip_engine_add_mouse(terminal->rip_engine,
			    mouse[1] + raster->viewport.sx,
			    mouse[2] + raster->viewport.sy,
			    mouse[3] + raster->viewport.sx,
			    mouse[4] + raster->viewport.sy,
			    mouse[5] != 0, mouse[6] != 0,
			    arguments + offset);
		}
		case 'U': {
			static const uint8_t widths[7] = {2, 2, 2, 2, 2, 1, 1};
			const struct cterm_rip_button_style *button_style =
			    &raster->last_button_style;
			int button[7];
			size_t offset = 0;
			const char *icon_end;
			const char *label_start;
			const char *label_end;
			const char *command;
			char *icon = NULL;
			char *label = NULL;
			int status;

			if (strlen(arguments) < 12)
				return 0;
			for (size_t i = 0; i < 7; i++) {
				if (!rip_number(arguments + offset, widths[i],
				    &button[i]))
					return 0;
				offset += widths[i];
			}
			label_start = strstr(arguments + offset, "<>");
			icon_end = label_start == NULL
			    ? arguments + strlen(arguments) : label_start;
			if (icon_end != arguments + offset) {
				icon = strndup(arguments + offset,
				    (size_t)(icon_end - (arguments + offset)));
				if (icon == NULL)
					return IO_ERROR_OUT_OF_MEMORY;
			}
			if (label_start != NULL)
				label_start += 2;
			command = label_start == NULL ? NULL
			    : strstr(label_start, "<>");
			label_end = command == NULL ? NULL : command;
			command = command == NULL ? "" : command + 2;
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
					.style = *button_style,
					.x1 = button[0] + raster->viewport.sx,
					.y1 = button[1] + raster->viewport.sy,
					.x2 = raster->last_button_x2,
					.y2 = raster->last_button_y2,
					.hotkey = button[4],
					.font_number = raster->font_number,
					.font_size = raster->font_size,
					.font_vertical = raster->font_vertical,
					.icon = icon,
					.label = label,
				};

				status = cterm_rip_engine_add_button_mouse(
				    terminal->rip_engine, &drawing,
				    (button[5] & 1) != 0, command);
				if (status != 0) {
					free(icon);
					free(label);
					return status;
				}
			}
			free(icon);
			free(label);
			if ((button[5] & 1) != 0 && command[0] != '\0')
				return rip_send_mouse_command(terminal, command);
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
	case '$': {
		const char *end = strchr(arguments, '$');
		char answer[RIP_QUERY_VALUE_SIZE];
		bool handled;
		int status;

		if (end == NULL)
			return 0;
		status = rip_state_variable(terminal, arguments,
		    (size_t)(end - arguments), &handled);
		if (status != 0 || handled)
			return status;

		if (rip_variable_token_equal(arguments,
		    (size_t)(end - arguments), "COMPAT"))
			return rip_compat_frame(terminal);
		status = rip_query_variable(terminal, arguments,
		    (size_t)(end - arguments), answer, &handled);
		if (status != 0 || handled)
			return status;
		/* Legacy variables not recognized by this client expand to empty. */
		return 0;
	}
	case '*':
		cterm_rip_engine_clear_mouse(terminal->rip_engine);
		terminal->rip_engine->hotkeys_enabled = true;
		terminal->rip_engine->tab_enabled = true;
		{
			int status = rip_text_font(terminal,
			    raster->version == 3 ? RIP_TEXT_FONT_DEFAULT : 0);

			if (status != 0)
				return status;
		}
		terminal->rip_text_disabled = false;
		terminal->left_margin = 1;
		terminal->top_margin = 1;
		terminal->right_margin = terminal->width;
		terminal->bottom_margin = terminal->height;
		terminal->extattr |= CTERM_EXTATTR_ORIGINMODE
		    | CTERM_EXTATTR_AUTOWRAP;
		terminal->attr = 15;
		terminal->fg_color = raster->palette[15];
		terminal->bg_color = raster->palette[0];
		cterm_apply_window(terminal);
		cterm_render_set_attribute(terminal, terminal->attr);
		cterm_render_set_colours(terminal, terminal->fg_color,
		    terminal->bg_color);
		cterm_set_position(terminal, 1, 1);
		cterm_render_clear_window(terminal);
		return cterm_rip_raster_reset_windows(raster);
	case 'H':
		cterm_set_position(terminal, 1, 1);
		return cterm_render_take_error(terminal);
	case 'g':
		if (strlen(arguments) < 4)
			return 0;
		first = rip_digit(arguments[0]);
		second = rip_digit(arguments[1]);
		third = rip_digit(arguments[2]);
		fourth = rip_digit(arguments[3]);
		if (first < 0 || second < 0 || third < 0 || fourth < 0)
			return 0;
		cterm_set_position(terminal, first * 36 + second + 1,
		    third * 36 + fourth + 1);
		return cterm_render_take_error(terminal);
	case 'w':
		if (strlen(arguments) < 10)
			return 0;
		for (size_t index = 0; index < 4; index++) {
			first = rip_digit(arguments[index * 2]);
			second = rip_digit(arguments[index * 2 + 1]);
			if (first < 0 || second < 0)
				return 0;
			coords[index] = first * 36 + second;
		}
		wrap = rip_digit(arguments[8]);
		style = rip_digit(arguments[9]);
		if (coords[0] == 0 && coords[1] == 0
		    && coords[2] == 0 && coords[3] == 0) {
			terminal->rip_text_disabled = true;
			return 0;
		}
		terminal->rip_text_disabled = false;
		if (wrap > 1 || wrap < 0 || style > 4 || style < 0)
			return 0;
		{
			int status = rip_text_font(terminal, style);

			if (status != 0)
				return status;
		}
		for (size_t index = 0; index < 4; index++) {
			int maximum = index % 2 == 0
			    ? terminal->width : terminal->height;
			if (coords[index] >= maximum)
				coords[index] = maximum - 1;
		}
		terminal->left_margin = coords[0] + 1;
		terminal->top_margin = coords[1] + 1;
		terminal->right_margin = coords[2] + 1;
		terminal->bottom_margin = coords[3] + 1;
		/* Legacy rip_text_window sets wrap before reinit_screen(),
		 * which recreates CTerm and restores its default modes. */
		terminal->extattr = CTERM_EXTATTR_AUTOWRAP
		    | CTERM_EXTATTR_ORIGINMODE | CTERM_EXTATTR_SXSCROLL
		    | CTERM_EXTATTR_DECBKM;
		cterm_apply_window(terminal);
		cterm_set_position(terminal, 1, 1);
		return cterm_render_take_error(terminal);
	case 'b': {
		static const uint8_t widths[8] = {2, 2, 2, 2, 2, 2, 1, 4};
		int extended[8];
		int pixel_width, pixel_height, font_width, font_height;
		int xoffset, yoffset, font_style;
		size_t offset = 0;
		int status;

		if (strlen(arguments) < 17)
			return 0;
		for (size_t index = 0; index < 8; index++) {
			if (!rip_number(arguments + offset, widths[index],
			    &extended[index]))
				return 0;
			offset += widths[index];
		}
		extended[0] = cterm_rip_raster_map_x(raster, extended[0]);
		extended[2] = cterm_rip_raster_map_x(raster, extended[2]);
		extended[1] = cterm_rip_raster_map_y(raster, extended[1]);
		extended[3] = cterm_rip_raster_map_y(raster, extended[3]);
		pixel_width = extended[2] - extended[0] + 1;
		pixel_height = extended[3] - extended[1] + 1;
		if (pixel_width <= 0 || pixel_height <= 0
		    || extended[4] <= 0 || extended[5] <= 0)
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
				if (font_height > 8) font_height = 8;
				break;
			case 1:
				if (font_height > 8) font_height = 8;
				if (font_width > 7) font_width = 7;
				break;
			case 3:
				if (font_width > 7) font_width = 7;
				break;
			}
		}
		xoffset = ((pixel_width - font_width * extended[4]) / 2)
		    / font_width;
		yoffset = ((pixel_height - font_height * extended[5]) / 2)
		    / font_height;
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
		terminal->left_margin = extended[0] / font_width + xoffset;
		terminal->top_margin = extended[1] / font_height + yoffset;
		terminal->right_margin = terminal->left_margin + extended[4] - 1;
		terminal->bottom_margin = terminal->top_margin + extended[5] - 1;
		terminal->extattr |= CTERM_EXTATTR_ORIGINMODE;
		if ((extended[7] & 1) != 0)
			terminal->extattr |= CTERM_EXTATTR_AUTOWRAP;
		else
			terminal->extattr &= ~CTERM_EXTATTR_AUTOWRAP;
		cterm_apply_window(terminal);
		cterm_set_position(terminal, 1, 1);
		if ((extended[7] & 0x20) != 0) {
			int old_style = raster->fill_style;
			int old_colour = raster->fill_colour;
			uint8_t old_pattern[8];

			memcpy(old_pattern, raster->fill_pattern,
			    sizeof(old_pattern));
			cterm_rip_raster_fill_style(raster, 1, 0);
			cterm_rip_raster_bar(raster, extended[0], extended[1],
			    extended[2], extended[3]);
			raster->fill_style = old_style;
			raster->fill_colour = old_colour;
			memcpy(raster->fill_pattern, old_pattern,
			    sizeof(old_pattern));
		}
		if (raster->error != 0)
			return raster->error;
		return cterm_render_take_error(terminal);
	}
	case 'e':
		if (!terminal->rip_text_disabled) {
			cterm_render_clear_window(terminal);
			cterm_set_position(terminal, 1, 1);
		}
		return cterm_render_take_error(terminal);
	case '>':
		previous_background = terminal->bg_color;
		terminal->bg_color = raster->palette[0];
		cterm_clear_line(terminal);
		terminal->bg_color = previous_background;
		return cterm_render_take_error(terminal);
	default:
		return IO_ERROR_NOT_SUPPORTED;
	}
}

static void
emit_response(const char *bytes, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context,
		    ST_CTERM_OUTPUT_RESPONSE, (const uint8_t *)bytes, size);
}

static void
emit_user(const char *bytes, size_t size, void *context)
{
	st_cterm_t *terminal = context;

	if (terminal->api_output != NULL)
		terminal->api_output(terminal->api_output_context,
		    ST_CTERM_OUTPUT_USER, (const uint8_t *)bytes, size);
}

static void
emit_size(struct cterminal *terminal, int columns, int rows,
	int pixel_width, int pixel_height, void *context)
{
	st_cterm_t *owner = context;
	uint32_t cell_width;
	uint32_t cell_height;

	if ((pixel_width < 0 || pixel_height < 0)
	    && st_console_cell_pixel_size(terminal->screen.console, &cell_width,
	    &cell_height) == 0 && cell_width != 0 && cell_height != 0
	    && columns >= 0 && rows >= 0
	    && (uint32_t)columns <= UINT32_MAX / cell_width
	    && (uint32_t)rows <= UINT32_MAX / cell_height) {
		pixel_width = columns * (int)cell_width;
		pixel_height = rows * (int)cell_height;
	}

	if (owner->api_size_changed != NULL)
		owner->api_size_changed(owner->api_size_context,
		    (uint32_t)columns, (uint32_t)rows,
		    pixel_width < 0 ? 0 : (uint32_t)pixel_width,
		    pixel_height < 0 ? 0 : (uint32_t)pixel_height);
}

enum {
	MOUSE_X10 = UINT16_C(1) << 0,
	MOUSE_NORMAL = UINT16_C(1) << 1,
	MOUSE_BUTTON = UINT16_C(1) << 2,
	MOUSE_ANY = UINT16_C(1) << 3,
	MOUSE_FOCUS = UINT16_C(1) << 4,
	MOUSE_UTF8 = UINT16_C(1) << 5,
	MOUSE_SGR = UINT16_C(1) << 6,
	MOUSE_ALT_SCROLL = UINT16_C(1) << 7,
	MOUSE_URXVT = UINT16_C(1) << 8,
	MOUSE_SGR_PIXELS = UINT16_C(1) << 9,
};

static uint16_t
mouse_mode_bit(int parameter)
{
	switch (parameter) {
	case 9: return MOUSE_X10;
	case 1000: return MOUSE_NORMAL;
	case 1002: return MOUSE_BUTTON;
	case 1003: return MOUSE_ANY;
	case 1004: return MOUSE_FOCUS;
	case 1005: return MOUSE_UTF8;
	case 1006: return MOUSE_SGR;
	case 1007: return MOUSE_ALT_SCROLL;
	case 1015: return MOUSE_URXVT;
	case 1016: return MOUSE_SGR_PIXELS;
	default: return 0;
	}
}

static void
mouse_mode_changed(int parameter, int enabled, void *context)
{
	st_cterm_t *terminal = context;
	uint16_t bit = mouse_mode_bit(parameter);

	if (enabled)
		terminal->api_mouse_modes |= bit;
	else
		terminal->api_mouse_modes &= (uint16_t)~bit;
}

static int
mouse_mode_query(int parameter, void *context)
{
	st_cterm_t *terminal = context;

	return (terminal->api_mouse_modes & mouse_mode_bit(parameter)) != 0;
}

static int
prepare_rip_unicode_position(st_cterm_t *target, int *x, int *y)
{
	struct cterminal *cterm = target;
	int right = TERM_MAXX;

	if (target->rip_engine == NULL)
		return 0;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR,
	    &right, NULL);
	if ((target->last_column_flag & (CTERM_LCF_ENABLED | CTERM_LCF_SET))
	    != (CTERM_LCF_ENABLED | CTERM_LCF_SET)
	    || (*x != right && *x != CURR_MAXX))
		return 0;
	/* The console itself does not auto-wrap; RIP uses CTerm's deferred
	 * last-column wrap before the next printable grapheme. */
	cterm_advance_char(target, x, y, 1);
	cterm_clear_lcf(target);
	return cterm_render_take_error(target);
}

static void
record_rip_unicode_position(st_cterm_t *target, int x, uint8_t columns)
{
	struct cterminal *cterm = target;
	int right = TERM_MAXX;

	if (target->rip_engine == NULL || columns == 0
	    || (target->extattr & CTERM_EXTATTR_AUTOWRAP) == 0
	    || (target->last_column_flag & CTERM_LCF_ENABLED) == 0)
		return;
	cterm_coord_conv_xy(cterm, CTERM_COORD_TERM, CTERM_COORD_CURR,
	    &right, NULL);
	if (x + columns - 1 >= right || x + columns - 1 >= CURR_MAXX)
		target->last_column_flag |= CTERM_LCF_SET;
}

static int
present_unicode(void *context,
	const struct st_unicode_presentation_event *event)
{
	st_cterm_t *terminal = context;
	st_cterm_t *target = terminal->status_sub != NULL
	    && terminal->status_display_active == 1
	    ? terminal->status_sub : terminal;
	st_console_t *console = target->screen.console;
	int x;
	int y;
	int status;
	enum st_unicode_presentation_event_kind kind = event->kind;

	if (kind == ST_UNICODE_PRESENTATION_COMMIT) {
		target->screen.provisional_drawn = false;
		return 0;
	}
	if (kind == ST_UNICODE_PRESENTATION_BEGIN)
		target->screen.provisional_drawn = false;
	if ((kind == ST_UNICODE_PRESENTATION_BEGIN
	    || kind == ST_UNICODE_PRESENTATION_REVISE) && event->columns == 0)
		return 0;
	if (kind == ST_UNICODE_PRESENTATION_REVISE
	    && !target->screen.provisional_drawn)
		kind = ST_UNICODE_PRESENTATION_BEGIN;

	if (!target->started
	    && event->kind != ST_UNICODE_PRESENTATION_COMMIT) {
		cterm_start(target);
		status = cterm_render_take_error(target);
		if (status != 0)
			return status;
	}

	switch (kind) {
	case ST_UNICODE_PRESENTATION_BEGIN:
		cterm_apply_window(target);
		x = target->xpos;
		y = target->ypos;
		cterm_coord_conv_xy(target, CTERM_COORD_ABSTERM,
		    CTERM_COORD_CURR, &x, &y);
		status = prepare_rip_unicode_position(target, &x, &y);
		if (status != 0)
			return status;
		cterm_render_set_position(target, x, y);
		if (st_console_get_position(console, &target->screen.provisional_x,
		    &target->screen.provisional_y) != 0)
			return IO_ERROR_IO;
		status = st_console_write_utf8(console, (const char *)event->bytes,
		    event->size);
		if (status == 0) {
			target->screen.provisional_drawn = true;
			record_rip_unicode_position(target, x, event->columns);
		}
		break;
	case ST_UNICODE_PRESENTATION_REVISE:
		cterm_apply_window(target);
		if (st_console_set_position(console, target->screen.provisional_x,
		    target->screen.provisional_y) != 0)
			return IO_ERROR_IO;
		status = st_console_write_utf8(console, (const char *)event->bytes,
		    event->size);
		break;
	case ST_UNICODE_PRESENTATION_COMMIT:
		return 0;
	case ST_UNICODE_PRESENTATION_DECODE_ERROR:
		status = capture_scalar(terminal, UINT32_C(0xfffd));
		if (status != 0)
			return status;
		cterm_apply_window(target);
		x = target->xpos;
		y = target->ypos;
		cterm_coord_conv_xy(target, CTERM_COORD_ABSTERM,
		    CTERM_COORD_CURR, &x, &y);
		status = prepare_rip_unicode_position(target, &x, &y);
		if (status != 0)
			return status;
		cterm_render_set_position(target, x, y);
		status = st_console_put_decode_error(console);
		if (status == 0)
			record_rip_unicode_position(target, x, 1);
		break;
	default:
		return IO_ERROR_INVALID_ARGUMENT;
	}
	if (status == 0)
		cterm_coord_get_xy(target, CTERM_COORD_ABSTERM,
		    &target->xpos, &target->ypos);
	return status;
}

static void
destroy_status_sub(st_cterm_t *terminal)
{
	if (terminal->status_sub != NULL) {
		(void)cterm_render_set_hold_update(terminal->status_sub, 0);
		cterm_end(terminal->status_sub, false);
		terminal->status_sub = NULL;
	}
}

static void
clear_reserved_status(st_cterm_t *terminal)
{
	struct st_console_cell blank = {
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(170, 170, 170),
		.background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 0),
		.font = terminal->screen.fonts.normal,
		.glyph_font = terminal->screen.fonts.normal,
	};
	struct st_console_rect row;
	uint8_t width;
	int status;

	row = (struct st_console_rect){
		.x = (uint32_t)(terminal->x - 1),
		.y = (uint32_t)(terminal->y + terminal->height - 1),
		.width = (uint32_t)terminal->width,
		.height = 1,
	};
	status = st_console_font_lookup(
	    terminal->screen.console, blank.font, ' ', &blank.glyph_font,
	    &blank.glyph, &width);
	cterm_render_record_error(terminal, status);
	if (status != 0)
		return;
	cterm_render_record_error(terminal,
	    st_console_fill(terminal->screen.console, &row, &blank));
}

static void
status_display_changed(struct cterminal *terminal, int old_type,
	int new_type, void *context)
{
	struct cterm_render_state screen;
	uint32_t columns;
	uint32_t rows;
	int available;
	int new_rows;
	(void)context;

	if (st_console_size(terminal->screen.console, &columns, &rows) != 0) {
		terminal->status_display_type = old_type;
		cterm_render_record_error(terminal, IO_ERROR_IO);
		return;
	}
	available = terminal->height + (old_type >= 1 ? 1 : 0);
	new_rows = available - (new_type >= 1 ? 1 : 0);
	if (new_rows < 1 || columns > INT_MAX
	    || (uint32_t)(terminal->y - 1 + new_rows
	    + (new_type >= 1 ? 1 : 0)) > rows) {
		terminal->status_display_type = old_type;
		cterm_render_record_error(terminal, IO_ERROR_OUT_OF_RANGE);
		return;
	}
	cterm_resize_rows(terminal, new_rows);
	if (new_type != 2) {
		/* A subterminal may currently be bubbling this callback. */
		terminal->api_status_destroy_pending = terminal->status_sub != NULL;
		if (new_type == 1 && terminal->status_sub == NULL)
			clear_reserved_status(terminal);
		return;
	}
	terminal->api_status_destroy_pending = false;
	if (terminal->status_sub != NULL)
		return;
	screen = terminal->screen;
	screen.unicode = NULL;
	screen.hold_update = 0;
	screen.error = 0;
	screen.hyperlink_root_registered = false;
	terminal->status_sub = cterm_init(1, terminal->width, terminal->x,
	    terminal->y + terminal->height, 0, terminal->width, NULL,
	    terminal->emulation, &screen);
	if (terminal->status_sub == NULL) {
		terminal->status_display_type = old_type;
		cterm_resize_rows(terminal,
		    available - (old_type >= 1 ? 1 : 0));
		cterm_render_record_error(terminal, IO_ERROR_OUT_OF_MEMORY);
		return;
	}
	terminal->status_sub->parent = terminal;
	terminal->status_sub->quiet = terminal->quiet;
	terminal->status_sub->response_cb = terminal->response_cb;
	terminal->status_sub->response_cbdata = terminal->response_cbdata;
	terminal->status_sub->keystroke_cb = terminal->keystroke_cb;
	terminal->status_sub->keystroke_cbdata = terminal->keystroke_cbdata;
	cterm_start(terminal->status_sub);
	cterm_apply_window(terminal);
}

static bool
utf8_supported(enum st_cterm_emulation emulation)
{
	return emulation == ST_CTERM_ANSI_BBS
	    || emulation == ST_CTERM_ATARI_ST_VT52;
}

static int
mark_hyperlinks(void *context, st_hyperlink_mark_fn mark, void *mark_context)
{
	st_cterm_t *terminal = context;
	struct st_console_cell *cells;
	struct st_console_rect rect;
	uint32_t columns;
	uint32_t rows;
	size_t count;
	int status;

	status = mark(mark_context, terminal->screen.current_hyperlink);
	if (status != 0)
		return status;
	if (terminal->status_sub != NULL) {
		status = mark(mark_context,
		    terminal->status_sub->screen.current_hyperlink);
		if (status != 0)
			return status;
	}
	if (terminal->backlines > 0 && terminal->scrollback != NULL) {
		count = (size_t)terminal->backlines * (size_t)terminal->backwidth;
		for (size_t i = 0; i < count; ++i) {
			status = mark(mark_context,
			    terminal->scrollback[i].hyperlink);
			if (status != 0)
				return status;
		}
	}
	status = st_console_size(terminal->screen.console, &columns, &rows);
	if (status != 0)
		return status;
	if (columns == 0 || rows == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / rows
	    || (size_t)columns * rows > SIZE_MAX / sizeof(*cells))
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * rows;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	rect = (struct st_console_rect){0, 0, columns, rows};
	status = st_console_read_cells(terminal->screen.console, &rect, cells,
	    columns);
	for (size_t i = 0; status == 0 && i < count; ++i)
		status = mark(mark_context, cells[i].hyperlink);
	free(cells);
	return status;
}

static void
default_palette_mapping(enum st_cterm_emulation emulation,
	enum st_cterm_petscii_machine petscii_machine, uint32_t mapping[16])
{
	static const uint16_t colour[16] = {
		0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
	};
	static const uint16_t c64[16] = {
		256, 257, 258, 259, 260, 261, 262, 263,
		264, 265, 266, 267, 268, 269, 270, 271,
	};
	static const uint16_t atari[16] = {
		272, 273, 273, 273, 273, 273, 273, 273,
		273, 273, 273, 273, 273, 273, 273, 273,
	};
	static const uint16_t prestel[16] = {
		0, 21, 46, 51, 196, 201, 226, 15,
		0, 21, 46, 51, 196, 201, 226, 15,
	};
	static const uint16_t atari_st[16] = {
		15, 196, 46, 226, 21, 201, 51, 274,
		275, 276, 277, 278, 279, 280, 281, 0,
	};
	const uint16_t *source = colour;

	switch (emulation) {
	case ST_CTERM_PETSCII:
		source = petscii_machine == ST_CTERM_PETSCII_C128_80
		    ? colour : c64;
		break;
	case ST_CTERM_ATASCII: source = atari; break;
	case ST_CTERM_PRESTEL:
	case ST_CTERM_BBC: source = prestel; break;
	case ST_CTERM_ATARI_ST_VT52: source = atari_st; break;
	default: break;
	}
	for (size_t i = 0; i < 16; ++i)
		mapping[i] = source[i];
}

static int
initialize_render_state(struct cterm_render_state *screen,
	const struct st_cterm_config *config)
{
	uint32_t capabilities;
	int mode = CTERM_MODE_IBM;
	int default_attribute = config->emulation == ST_CTERM_PETSCII
	    || config->emulation == ST_CTERM_ATARI_ST_VT52 ? 15 : 7;
	int status;

	status = st_console_capabilities(config->console, &capabilities);
	if (status != 0)
		return status;
	if (config->emulation == ST_CTERM_PETSCII) {
		switch (config->petscii_machine) {
		case ST_CTERM_PETSCII_C64: mode = CTERM_MODE_C64_40; break;
		case ST_CTERM_PETSCII_C128_40: mode = CTERM_MODE_C128_40; break;
		case ST_CTERM_PETSCII_C128_80: mode = CTERM_MODE_C128_80; break;
		default: return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	screen->mode = mode;
	screen->default_attribute = (uint8_t)default_attribute;
	if (config->palette_mapping != NULL) {
		for (size_t i = 0; i < 16; ++i)
			screen->mode_palette[i] = config->palette_mapping[i];
	}
	else
		default_palette_mapping(config->emulation, config->petscii_machine,
		    screen->mode_palette);
	screen->capabilities = capabilities;
	screen->wscroll = 1;
	return 0;
}

int
st_cterm_create(st_cterm_t **result, const struct st_cterm_config *config)
{
	struct cterm_render_state screen = {0};
	struct st_console_cell *scrollback = NULL;
	st_cterm_t *terminal;
	uint16_t normal;
	uint32_t columns;
	uint32_t rows;
	uint32_t terminal_rows;
	uint32_t status_rows;
	int status;

	if (result == NULL || config == NULL || config->console == NULL
	    || config->fonts == NULL
	    || st_font_catalog_console(config->fonts) != config->console
	    || config->emulation < ST_CTERM_ANSI_BBS
	    || config->emulation > ST_CTERM_ATARI_ST_VT52
	    || config->encoding < ST_CTERM_ENCODING_UTF8
	    || config->encoding > ST_CTERM_ENCODING_CODEPAGE
	    || config->rip_version < ST_CTERM_RIP_OFF
	    || config->rip_version > ST_CTERM_RIP_3
	    || (config->rip_version != ST_CTERM_RIP_OFF
	        && config->emulation != ST_CTERM_ANSI_BBS)
	    || config->codepage >= ST_CODEPAGE_COUNT
	    || (config->encoding == ST_CTERM_ENCODING_UTF8
	    && !utf8_supported(config->emulation))
	    || !st_font_catalog_identifier(config->fonts,
	    config->font_selector, &normal)
	    || st_console_size(config->console, &columns, &rows) != 0
	    || config->terminal_rows > rows
	    || config->scrollback_lines > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	status = initialize_render_state(&screen, config);
	if (status != 0)
		return status;
	status_rows = config->status_line ? 1 : 0;
	if (rows <= status_rows)
		return IO_ERROR_INVALID_ARGUMENT;
	terminal_rows = config->terminal_rows == 0
	    ? rows - status_rows : config->terminal_rows;
	if (terminal_rows + status_rows > rows)
		return IO_ERROR_INVALID_ARGUMENT;
	if (config->scrollback_lines != 0) {
		if ((size_t)columns > SIZE_MAX / config->scrollback_lines
		    || (size_t)columns * config->scrollback_lines
		    > SIZE_MAX / sizeof(*scrollback))
			return IO_ERROR_OUT_OF_RANGE;
		scrollback = calloc((size_t)columns * config->scrollback_lines,
		    sizeof(*scrollback));
		if (scrollback == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	screen.console = config->console;
	screen.audio = config->audio;
	screen.hyperlinks = config->hyperlinks;
	screen.codepage = config->codepage;
	screen.fonts = (struct st_console_font_selection){
		.normal = normal, .bright = normal, .blink = normal,
		.bright_blink = normal,
	};
	for (size_t selector = 0; selector < UINT8_MAX + 1U; ++selector) {
		uint16_t identifier;

		if (st_font_catalog_identifier(config->fonts, (uint8_t)selector,
		    &identifier)) {
			screen.font_identifiers[selector] = identifier;
			screen.font_valid[selector] = true;
		}
	}
	for (size_t slot = 0; slot < 5; ++slot)
		screen.selectors[slot] = config->font_selector;
	terminal = cterm_init((int)terminal_rows, (int)columns, 1, 1,
	    (int)config->scrollback_lines, (int)columns, scrollback,
	    config->emulation, &screen);
	if (terminal == NULL) {
		free(scrollback);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	status = cterm_render_take_error(terminal);
	if (status != 0) {
		cterm_end(terminal, false);
		free(scrollback);
		return status;
	}
	terminal->quiet = config->quiet;
	if (config->rip_version != ST_CTERM_RIP_OFF)
		terminal->last_column_flag = CTERM_LCF_FORCED | CTERM_LCF_ENABLED;
	terminal->status_display_type = config->status_line ? 1 : 0;
	terminal->api_reserved_rows = rows - terminal_rows - status_rows;
	terminal->wire_encoding = config->encoding;
	terminal->api_input_filter = config->input_filter;
	terminal->api_input_filter_active = config->input_filter_active;
	terminal->api_input_filter_context = config->input_filter_context;
	terminal->api_capture = config->capture;
	terminal->api_capture_active = config->capture_active;
	terminal->api_capture_context = config->capture_context;
	terminal->api_output = config->output;
	terminal->api_output_context = config->output_context;
	terminal->rip_prompt = config->rip_prompt;
	terminal->rip_prompt_context = config->rip_prompt_context;
	terminal->api_apc = config->apc;
	terminal->api_apc_context = config->apc_context;
	if (config->apc_directory != NULL) {
		terminal->apc = calloc(1, sizeof(*terminal->apc));
		if (terminal->apc == NULL) {
			st_cterm_destroy(&terminal);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		terminal->apc->directory = config->apc_directory;
		terminal->apc->version = config->application_version;
	}
	terminal->api_size_changed = config->size_changed;
	terminal->api_size_context = config->size_context;
	terminal->response_cb = emit_response;
	terminal->response_cbdata = terminal;
	terminal->keystroke_cb = emit_user;
	terminal->keystroke_cbdata = terminal;
	terminal->size_change_cb = config->size_changed == NULL ? NULL : emit_size;
	terminal->size_change_cbdata = terminal;
	terminal->status_display_cb = status_display_changed;
	terminal->status_display_cbdata = terminal;
	terminal->mouse_state_change = mouse_mode_changed;
	terminal->mouse_state_change_cbdata = terminal;
	terminal->mouse_state_query = mouse_mode_query;
	terminal->mouse_state_query_cbdata = terminal;
	if (config->status_line) {
		clear_reserved_status(terminal);
		status = cterm_render_take_error(terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	if (config->encoding == ST_CTERM_ENCODING_UTF8) {
		status = st_unicode_presentation_create(&terminal->screen.unicode,
		    present_unicode, terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	if (config->hyperlinks != NULL) {
		status = st_hyperlink_registry_register_root(config->hyperlinks,
		    mark_hyperlinks, terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
		terminal->screen.hyperlink_root_registered = true;
	}
	if (config->rip_version != ST_CTERM_RIP_OFF) {
		const struct cterm_rip_parser_sink sink = {
			.context = terminal,
			.passthrough = rip_passthrough,
			.command = rip_host_command,
			.control = rip_control,
			.at_carriage_return_column = rip_at_column,
			.text_disabled = rip_text_disabled,
			.music_introducer = rip_music_introducer,
		};

		terminal->rip_engine = calloc(1, sizeof(*terminal->rip_engine));
		if (terminal->rip_engine == NULL) {
			st_cterm_destroy(&terminal);
			return IO_ERROR_OUT_OF_MEMORY;
		}
		status = cterm_rip_engine_init(terminal->rip_engine,
		    config->console, &sink);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
		terminal->rip_engine->parser.utf8_input =
		    config->encoding == ST_CTERM_ENCODING_UTF8;
		status = st_console_set_full_surface_scroll(config->console, true);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
		terminal->rip_engine->raster.version =
		    config->rip_version == ST_CTERM_RIP_3 ? 3 : 1;
		terminal->rip_engine->default_text.font_identifier = normal;
		status = st_console_size(config->console,
		    &terminal->rip_engine->default_text.columns,
		    &terminal->rip_engine->default_text.rows);
		if (status == 0)
			status = st_console_cell_pixel_size(config->console,
			    &terminal->rip_engine->default_text.cell_width,
			    &terminal->rip_engine->default_text.cell_height);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
		if (config->rip_version == ST_CTERM_RIP_3)
			terminal->rip_text_font_style = RIP_TEXT_FONT_DEFAULT;
		terminal->rip_engine->raster.text_codepage = config->codepage;
		terminal->rip_engine->raster.unicode_text =
		    config->encoding == ST_CTERM_ENCODING_UTF8;
		terminal->rip_directory = config->rip_directory;
		terminal->rip_engine->raster.rip_directory = config->rip_directory;
		status = rip_load_variables(terminal);
		if (status != 0) {
			st_cterm_destroy(&terminal);
			return status;
		}
	}
	*result = terminal;
	return 0;
}

void
st_cterm_destroy(st_cterm_t **terminal)
{
	struct st_console_cell *scrollback;

	if (terminal == NULL || *terminal == NULL)
		return;
	scrollback = (*terminal)->scrollback;
	(void)st_cterm_finish(*terminal);
	st_unicode_presentation_destroy(&(*terminal)->screen.unicode);
	if ((*terminal)->rip_engine != NULL) {
		cterm_rip_engine_destroy((*terminal)->rip_engine);
		free((*terminal)->rip_engine);
		(*terminal)->rip_engine = NULL;
		(void)st_console_set_full_surface_scroll(
		    (*terminal)->screen.console, false);
	}
	destroy_status_sub(*terminal);
	(void)cterm_render_set_hold_update(*terminal, 0);
	if ((*terminal)->screen.hyperlink_root_registered) {
		st_hyperlink_registry_unregister_root((*terminal)->screen.hyperlinks,
		    mark_hyperlinks, *terminal);
		(*terminal)->screen.hyperlink_root_registered = false;
	}
	cterm_end(*terminal, false);
	free(scrollback);
	*terminal = NULL;
}

static bool
input_filter_enabled(const st_cterm_t *terminal)
{
	return !terminal->api_input_filter_suppressed
	    && terminal->api_input_filter != NULL
	    && (terminal->api_input_filter_active == NULL
	    || terminal->api_input_filter_active(
	    terminal->api_input_filter_context));
}

static bool
capture_enabled(const st_cterm_t *terminal)
{
	return terminal->api_capture != NULL
	    && (terminal->api_capture_active == NULL
	    || terminal->api_capture_active(terminal->api_capture_context));
}

static int
capture_scalar(st_cterm_t *terminal, uint32_t scalar)
{
	char utf8[4];
	ptrdiff_t size;

	if (!capture_enabled(terminal))
		return 0;
	size = st_utf8_encode_one(utf8, sizeof(utf8), scalar);
	if (size < 0)
		return IO_ERROR_INVALID_ENCODING;
	return terminal->api_capture(terminal->api_capture_context,
	    utf8, (size_t)size);
}

static int
filter_input_scalar(st_cterm_t *terminal, uint32_t scalar, bool *consumed,
	char *replacement, size_t *replacement_size)
{
	int status;

	*consumed = false;
	*replacement_size = 0;
	if (terminal->api_input_filter_suppressed
	    || terminal->api_input_filter == NULL)
		return 0;
	status = terminal->api_input_filter(terminal->api_input_filter_context,
	    scalar, consumed, replacement,
	    ST_CTERM_INPUT_REPLACEMENT_CAPACITY, replacement_size);
	if (status != 0)
		return status;
	return *replacement_size > ST_CTERM_INPUT_REPLACEMENT_CAPACITY
	    || (!*consumed && *replacement_size != 0)
	    ? IO_ERROR_INVALID_FORMAT : 0;
}

static int
present_filtered_scalar(void *context, uint32_t scalar, bool *consumed)
{
	st_cterm_t *terminal = context;
	char replacement[ST_CTERM_INPUT_REPLACEMENT_CAPACITY];
	size_t replacement_size;
	size_t written;
	int status = 0;

	*consumed = false;
	if (input_filter_enabled(terminal))
		status = filter_input_scalar(terminal, scalar, consumed,
		    replacement, &replacement_size);

	if (status != 0)
		return status;
	if (!*consumed)
		return capture_scalar(terminal, scalar);
	if (replacement_size == 0)
		return 0;
	status = st_cterm_write_utf8(terminal, replacement, replacement_size,
	    &written);
	return status == 0 && written != replacement_size ? IO_ERROR_IO : status;
}

static int
codepage_write(st_cterm_t *terminal, const uint8_t *bytes, size_t size,
	size_t *consumed)
{
	int speed = 0;
	int status;
	size_t offset = 0;

	*consumed = 0;
	if (terminal->screen.unicode != NULL) {
		status = st_unicode_presentation_boundary(
		    terminal->screen.unicode);
		if (status != 0)
			return status;
	}

	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	if (status != 0)
		return status;

	while (size != 0) {
		int chunk = size > INT_MAX ? INT_MAX : (int)size;
		size_t used = cterm_write(terminal, bytes, chunk, &speed);

		offset += used;
		*consumed = offset;
		if (used != (size_t)chunk)
			return IO_ERROR_IO;
		bytes += chunk;
		size -= (size_t)chunk;
	}
	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

static int
write_plain(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed, bool semantic_utf8)
{
	const uint8_t *input = bytes;
	size_t offset = 0;
	int status = 0;

	if (terminal == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (!semantic_utf8
	    && terminal->wire_encoding == ST_CTERM_ENCODING_CODEPAGE) {
		bool filter_active = input_filter_enabled(terminal)
		    || capture_enabled(terminal);

		if (!filter_active)
			status = codepage_write(terminal, input, size, consumed);
		else {
			size_t run_start = 0;
			size_t handled = 0;

			for (size_t i = 0; i < size; ++i) {
				char replacement[ST_CTERM_INPUT_REPLACEMENT_CAPACITY];
				size_t replacement_size;
				size_t used;
				bool filtered;
				uint32_t scalar = st_codepage_to_unicode(
				    terminal->screen.codepage, input[i], false);

				filtered = false;
				replacement_size = 0;
				if (input_filter_enabled(terminal))
					status = filter_input_scalar(terminal, scalar,
					    &filtered, replacement, &replacement_size);
				if (status != 0)
					break;
				if (!filtered) {
					status = capture_scalar(terminal, scalar);
					if (status != 0)
						break;
					continue;
				}
				if (i > run_start) {
					status = codepage_write(terminal,
					    input + run_start, i - run_start, &used);
					handled = run_start + used;
					if (status != 0)
						break;
				}
				if (replacement_size != 0) {
					status = st_cterm_write_utf8(terminal,
					    replacement, replacement_size, &used);
					if (status == 0 && used != replacement_size)
						status = IO_ERROR_IO;
					if (status != 0)
						break;
				}
				run_start = i + 1;
				handled = run_start;
			}
			if (status == 0 && run_start < size) {
				size_t used;

				status = codepage_write(terminal,
				    input + run_start, size - run_start, &used);
				handled = run_start + used;
			}
			*consumed = handled;
		}
		if (terminal->api_status_destroy_pending) {
			destroy_status_sub(terminal);
			terminal->api_status_destroy_pending = false;
			if (terminal->status_display_type != 0)
				clear_reserved_status(terminal);
		}
		if (status == 0)
			status = cterm_render_take_error(terminal);
		return status;
	}
	while (offset < size) {
		uint8_t byte = input[offset];
		st_cterm_t *target = terminal->status_sub != NULL
		    && terminal->status_display_active == 1
		    ? terminal->status_sub : terminal;

		if (byte >= 0x80
		    && target->accumulator == cterm_accumulate_ecma_seq
		    && target->seq_phase == SEQ_PHASE_C1
		    && target->seq_key == SEQ_ESC
		    && target->saved_accumulator == NULL) {
			/* A non-ASCII scalar cannot complete a single-byte ESC
			 * sequence.  Emit the literal ESC before feeding its UTF-8
			 * bytes, including any continuation in a later write. */
			char literal_escape[] = "\033";

			target->accumulator = NULL;
			target->seq_phase = SEQ_PHASE_IDLE;
			target->sequence = 0;
			cterm_apply_window(target);
			cterm_uctputs(target, literal_escape);
			cterm_coord_get_xy(target, CTERM_COORD_ABSTERM,
			    &target->xpos, &target->ypos);
			status = cterm_render_take_error(target);
			if (status != 0)
				break;
		}

		if (target->accumulator != NULL || byte < 0x20 || byte == 0x7f) {
			size_t used;
			char replacement[ST_CTERM_INPUT_REPLACEMENT_CAPACITY];
			size_t replacement_size = 0;
			bool filtered = false;

			if (byte < 0x80 && input_filter_enabled(terminal))
				status = filter_input_scalar(terminal, byte,
				    &filtered, replacement, &replacement_size);
			if (status != 0)
				break;
			if (filtered) {
				if (replacement_size != 0) {
					status = st_cterm_write_utf8(terminal,
					    replacement, replacement_size, &used);
					if (status == 0 && used != replacement_size)
						status = IO_ERROR_IO;
				}
				if (status != 0)
					break;
				++offset;
				continue;
			}
			if (byte < 0x80) {
				status = capture_scalar(terminal, byte);
				if (status != 0)
					break;
			}

			status = st_unicode_presentation_boundary(
			    terminal->screen.unicode);
			if (status == 0)
				status = codepage_write(terminal, &byte, 1, &used);
			else
				used = 0;
			offset += used;
			if (status != 0)
				break;
		}
		else {
			size_t span = 1;
			size_t used;

			while (span < size - offset && input[offset + span] >= 0x20
			    && input[offset + span] != 0x7f)
				++span;
			bool previous_filter = terminal->api_input_filter_installed;
			bool enable_filter = input_filter_enabled(terminal)
			    || capture_enabled(terminal);

			terminal->api_input_filter_installed = enable_filter;
			st_unicode_presentation_set_scalar_filter(
			    terminal->screen.unicode,
			    enable_filter ? present_filtered_scalar : NULL, terminal);
			status = st_unicode_presentation_feed(terminal->screen.unicode,
			    input + offset, span, &used);
			terminal->api_input_filter_installed = previous_filter;
			st_unicode_presentation_set_scalar_filter(
			    terminal->screen.unicode,
			    previous_filter ? present_filtered_scalar : NULL, terminal);
			offset += used;
			if (status != 0)
				break;
		}
	}
	if (terminal->api_status_destroy_pending) {
		destroy_status_sub(terminal);
		terminal->api_status_destroy_pending = false;
		if (terminal->status_display_type != 0)
			clear_reserved_status(terminal);
	}
	if (status == 0)
		status = cterm_render_take_error(terminal);
	*consumed = offset;
	return status;
}

int
st_cterm_write(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed)
{
	if (terminal == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->rip_engine != NULL)
		return cterm_rip_engine_feed(terminal->rip_engine, bytes,
		    size, consumed);
	return write_plain(terminal, bytes, size, consumed, false);
}

int
st_cterm_poll(st_cterm_t *terminal)
{
	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	cterm_apc_poll(terminal);
	return 0;
}

int
st_cterm_write_utf8(st_cterm_t *terminal, const void *bytes, size_t size,
	size_t *consumed)
{
	bool suppressed;
	int status;

	if (terminal == NULL || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->screen.unicode == NULL) {
		status = st_unicode_presentation_create(&terminal->screen.unicode,
		    present_unicode, terminal);
		if (status != 0)
			return status;
	}
	suppressed = terminal->api_input_filter_suppressed;
	terminal->api_input_filter_suppressed = true;
	status = write_plain(terminal, bytes, size, consumed, true);
	terminal->api_input_filter_suppressed = suppressed;
	return status;
}

int
st_cterm_finish(st_cterm_t *terminal)
{
	int status;

	if (terminal == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = terminal->rip_engine == NULL ? 0
	    : cterm_rip_engine_flush(terminal->rip_engine);
	if (status == 0)
		status = terminal->screen.unicode == NULL ? 0
		    : st_unicode_presentation_boundary(terminal->screen.unicode);
	if (status == 0)
		status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_prepare_disconnect(st_cterm_t *terminal)
{
	int status = st_cterm_finish(terminal);
	if (status != 0) return status;
	terminal->top_margin = terminal->left_margin = 1;
	terminal->bottom_margin = terminal->height;
	terminal->right_margin = terminal->width;
	if (terminal->status_display_type == 0)
		cterm_scrollup(terminal);
	else {
		int old_type = terminal->status_display_type;
		terminal->status_display_active = 0;
		terminal->status_display_type = 0;
		terminal->size_change_cb = NULL;
		status_display_changed(terminal, old_type, 0, terminal);
		if (terminal->api_status_destroy_pending) {
			destroy_status_sub(terminal);
			terminal->api_status_destroy_pending = false;
		}
		struct st_console_cell blank;
		cterm_render_make_current_cell(terminal, &blank, ' ', 0);
		const struct st_console_rect row = {
			(uint32_t)(terminal->x - 1), (uint32_t)(terminal->y + terminal->height - 2),
			(uint32_t)terminal->width, 1,
		};
		cterm_render_record_error(terminal,
		    st_console_fill(terminal->screen.console, &row, &blank));
	}
	cterm_apply_window(terminal);
	return cterm_render_take_error(terminal);
}

int
st_cterm_reset_terminal(st_cterm_t *terminal)
{
	int status = st_cterm_finish(terminal);

	if (status != 0)
		return status;
	cterm_reset(terminal);
	if (terminal->rip_engine != NULL) {
		struct cterm_rip_engine *engine = terminal->rip_engine;
		struct cterm_rip_parser_sink host = engine->host;
		int version = engine->raster.version;
		enum st_codepage text_codepage = engine->raster.text_codepage;
		bool unicode_text = engine->raster.unicode_text;

		cterm_start(terminal);
		status = cterm_render_take_error(terminal);
		if (status != 0)
			return status;
		cterm_rip_engine_destroy(engine);
		status = cterm_rip_engine_init(engine, terminal->screen.console,
		    &host);
		if (status != 0)
			return status;
		engine->parser.utf8_input =
		    terminal->wire_encoding == ST_CTERM_ENCODING_UTF8;
		engine->raster.version = version;
		engine->raster.text_codepage = text_codepage;
		engine->raster.unicode_text = unicode_text;
		engine->raster.rip_directory = terminal->rip_directory;
		status = rip_load_variables(terminal);
		if (status != 0)
			return status;
		terminal->rip_text_disabled = false;
	}
	return cterm_render_take_error(terminal);
}

static int
resize_scrollback(st_cterm_t *terminal, uint32_t columns,
	struct st_console_cell **result)
{
	struct st_console_cell *resized;
	size_t count;
	size_t copy_columns;

	if (terminal->backlines == 0)
		return 0;
	if ((size_t)columns > SIZE_MAX / (size_t)terminal->backlines
	    || (size_t)columns * (size_t)terminal->backlines
	    > SIZE_MAX / sizeof(*resized))
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)columns * (size_t)terminal->backlines;
	resized = calloc(count, sizeof(*resized));
	if (resized == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	copy_columns = columns < (uint32_t)terminal->backwidth
	    ? columns : (size_t)terminal->backwidth;
	for (int line = 0; line < terminal->backfilled; ++line) {
		int source = terminal->backstart + line;

		if (source >= terminal->backlines)
			source -= terminal->backlines;
		memcpy(resized + (size_t)line * columns,
		    terminal->scrollback + (size_t)source * terminal->backwidth,
		    copy_columns * sizeof(*resized));
	}
	*result = resized;
	return 0;
}

static void
resize_parser_geometry(st_cterm_t *terminal, int columns, int rows,
	int cursor_x, int cursor_y)
{
	int old_columns = terminal->width;
	int old_rows = terminal->height;
	uint32_t last_x = (uint32_t)(terminal->x - 1 + columns - 1);
	uint32_t last_y = (uint32_t)(terminal->y - 1 + rows - 1);

	cterm_render_refresh_remote_fonts(terminal);

	terminal->width = columns;
	terminal->height = rows;
	if (terminal->right_margin == old_columns
	    || terminal->right_margin > columns)
		terminal->right_margin = columns;
	if (terminal->left_margin > columns)
		terminal->left_margin = 1;
	if (terminal->bottom_margin == old_rows
	    || terminal->bottom_margin > rows)
		terminal->bottom_margin = rows;
	if (terminal->top_margin > rows)
		terminal->top_margin = 1;
	if (cursor_x > columns)
		cursor_x = columns;
	if (cursor_y > rows)
		cursor_y = rows;
	if (terminal->screen.provisional_x > last_x)
		terminal->screen.provisional_x = last_x;
	if (terminal->screen.provisional_y > last_y)
		terminal->screen.provisional_y = last_y;
	cterm_apply_window(terminal);
	cterm_set_position(terminal, cursor_x, cursor_y);
}

int
st_cterm_resize(st_cterm_t *terminal, uint32_t columns, uint32_t rows)
{
	struct st_console_cell *scrollback = NULL;
	struct st_console_cell *old_scrollback;
	uint32_t status_rows;
	uint32_t terminal_rows;
	int cursor_x;
	int cursor_y;
	int status_x = 1;
	int status_y = 1;
	int status;

	if (terminal == NULL || columns == 0 || rows == 0
	    || columns > INT_MAX || rows > INT_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	status_rows = terminal->status_display_type >= 1 ? 1 : 0;
	if (rows <= terminal->api_reserved_rows + status_rows)
		return IO_ERROR_OUT_OF_RANGE;
	terminal_rows = rows - terminal->api_reserved_rows - status_rows;
	if (terminal->backlines != 0) {
		status = resize_scrollback(terminal, columns, &scrollback);
		if (status != 0)
			return status;
	}
	cursor_x = terminal->xpos;
	cursor_y = terminal->ypos;
	if (terminal->status_sub != NULL) {
		status_x = terminal->status_sub->xpos;
		status_y = terminal->status_sub->ypos;
	}
	status = st_console_resize(terminal->screen.console, columns, rows);
	if (status != 0) {
		free(scrollback);
		return status;
	}
	old_scrollback = terminal->scrollback;
	terminal->scrollback = scrollback;
	terminal->backwidth = (int)columns;
	terminal->backstart = 0;
	terminal->backpos = terminal->backfilled == terminal->backlines
	    ? 0 : terminal->backfilled;
	free(old_scrollback);
	resize_parser_geometry(terminal, (int)columns, (int)terminal_rows,
	    cursor_x, cursor_y);
	if (terminal->status_sub != NULL) {
		terminal->status_sub->x = terminal->x;
		terminal->status_sub->y = terminal->y + terminal->height;
		resize_parser_geometry(terminal->status_sub, (int)columns, 1,
		    status_x, status_y);
	}
	if (terminal->status_sub == NULL || terminal->status_display_active == 0)
		cterm_apply_window(terminal);
	status = cterm_render_take_error(terminal);
	if (status == 0 && terminal->status_sub != NULL)
		status = cterm_render_take_error(terminal->status_sub);
	return status;
}

int
st_cterm_size(const st_cterm_t *terminal, uint32_t *columns, uint32_t *rows)
{
	if (terminal == NULL || columns == NULL || rows == NULL
	    || terminal->width < 1 || terminal->height < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*columns = (uint32_t)terminal->width;
	*rows = (uint32_t)terminal->height;
	return 0;
}

int
st_cterm_origin(const st_cterm_t *terminal, uint32_t *x, uint32_t *y)
{
	if (terminal == NULL || x == NULL || y == NULL
	    || terminal->x < 1 || terminal->y < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*x = (uint32_t)terminal->x;
	*y = (uint32_t)terminal->y;
	return 0;
}

int
st_cterm_state(const st_cterm_t *terminal, struct st_cterm_state *state)
{
	if (terminal == NULL || state == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*state = (struct st_cterm_state){
		.x = terminal->xpos,
		.y = terminal->ypos,
		.top_margin = terminal->top_margin,
		.bottom_margin = terminal->bottom_margin,
		.left_margin = terminal->left_margin,
		.right_margin = terminal->right_margin,
		.attr = terminal->attr,
		.fg_color = terminal->fg_color & 0xFFFFFFu,
		.bg_color = terminal->bg_color & 0xFFFFFFu,
		.has_palette_override = terminal->has_palette_override,
		.font_slot = terminal->font_slot,
		.scrollback_lines = terminal->backlines,
		.scrollback_width = terminal->backwidth,
		.scrollback_pos = terminal->backpos,
		.scrollback_start = terminal->backstart,
		.emulation = (enum st_cterm_emulation)terminal->emulation,
		.started = terminal->started != 0,
		.skypix = terminal->skypix != 0,
		.ext_attr = {
			.auto_wrap = (terminal->extattr & CTERM_EXTATTR_AUTOWRAP) != 0,
			.origin_mode = (terminal->extattr & CTERM_EXTATTR_ORIGINMODE) != 0,
			.sx_scroll = (terminal->extattr & CTERM_EXTATTR_SXSCROLL) != 0,
			.dec_lrmm = (terminal->extattr & CTERM_EXTATTR_DECLRMM) != 0,
			.bracket_paste = (terminal->extattr & CTERM_EXTATTR_BRACKETPASTE) != 0,
			.dec_bkm = (terminal->extattr & CTERM_EXTATTR_DECBKM) != 0,
			.prestel_mosaic = (terminal->extattr & CTERM_EXTATTR_PRESTEL_MOSAIC) != 0,
			.prestel_double_height = (terminal->extattr & CTERM_EXTATTR_PRESTEL_DOUBLE_HEIGHT) != 0,
			.prestel_conceal = (terminal->extattr & CTERM_EXTATTR_PRESTEL_CONCEAL) != 0,
			.prestel_separated = (terminal->extattr & CTERM_EXTATTR_PRESTEL_SEPARATED) != 0,
			.prestel_hold = (terminal->extattr & CTERM_EXTATTR_PRESTEL_HOLD) != 0,
			.alternate_keypad = (terminal->extattr & CTERM_EXTATTR_ALTERNATE_KEYPAD) != 0,
		},
		.last_column_flag = {
			.set = (terminal->last_column_flag & CTERM_LCF_SET) != 0,
			.enabled = (terminal->last_column_flag & CTERM_LCF_ENABLED) != 0,
			.forced = (terminal->last_column_flag & CTERM_LCF_FORCED) != 0,
		},
	};
	for (size_t i = 0; i < 4; ++i)
		state->alt_fonts[i] = terminal->altfont[i];
	if (terminal->has_palette_override) {
		for (size_t i = 0; i < 16; ++i)
			state->palette_override[i] = terminal->palette_override[i]
			    & 0xFFFFFFu;
	}
	return 0;
}

int
st_cterm_set_alt_font(st_cterm_t *terminal, uint8_t selector)
{
	if (terminal == NULL
	    || !cterm_render_set_font_selector(terminal, selector, false, 1))
		return IO_ERROR_INVALID_ARGUMENT;
	terminal->altfont[0] = selector;
	return 0;
}

int
st_cterm_register_font(st_cterm_t *terminal, uint8_t selector,
	const char *path, uint16_t *identifier)
{
	uint16_t registered;
	int status;

	if (terminal == NULL || path == NULL || *path == '\0'
	    || identifier == NULL
	    || selector < ST_FONT_CATALOG_FIRST_CUSTOM
	    || terminal->screen.font_valid[selector])
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_font_catalog_register_custom(terminal->screen.console, path,
	    &registered);
	if (status != 0)
		return status;
	terminal->screen.font_identifiers[selector] = registered;
	terminal->screen.font_valid[selector] = true;
	*identifier = registered;
	return 0;
}

static void
copy_sauce_field(uint8_t *destination, size_t width, const char *text)
{
	size_t length = text == NULL ? 0 : strlen(text);

	memset(destination, ' ', width);
	if (length > width)
		length = width;
	if (length != 0)
		memcpy(destination, text, length);
}

int
st_cterm_save_binary(st_cterm_t *terminal, FILE *stream,
	bool with_sauce, const char *title, const char *author,
	int *native_error)
{
	struct st_console_cell *cells;
	uint8_t *encoded;
	struct st_console_rect row;
	size_t columns;
	size_t rows;
	int status = 0;

	if (terminal == NULL || stream == NULL || native_error == NULL
	    || terminal->width < 1 || terminal->height < 1
	    || terminal->x < 1 || terminal->y < 1)
		return IO_ERROR_INVALID_ARGUMENT;
	*native_error = 0;
	columns = (size_t)terminal->width;
	rows = (size_t)terminal->height;
	if (columns > SIZE_MAX / 2 || columns > SIZE_MAX / sizeof(*cells)
	    || (with_sauce && (columns > UINT8_MAX * 2U
	    || rows > UINT32_MAX / (columns * 2U))))
		return IO_ERROR_OUT_OF_RANGE;
	cells = malloc(columns * sizeof(*cells));
	encoded = malloc(columns * 2U);
	if (cells == NULL || encoded == NULL) {
		free(cells);
		free(encoded);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	row = (struct st_console_rect){
		.x = (uint32_t)(terminal->x - 1),
		.y = (uint32_t)(terminal->y - 1),
		.width = (uint32_t)columns,
		.height = 1,
	};
	for (size_t y = 0; y < rows; ++y) {
		row.y = (uint32_t)(terminal->y - 1) + (uint32_t)y;
		status = st_console_read_cells(terminal->screen.console, &row,
		    cells, columns);
		if (status != 0)
			break;
		for (size_t x = 0; x < columns; ++x) {
			encoded[x * 2U] = cterm_render_cell_byte(terminal,
			    &cells[x]);
			encoded[x * 2U + 1U] = cterm_render_cell_attribute(terminal,
			    &cells[x]);
		}
		if (fwrite(encoded, 1, columns * 2U, stream) != columns * 2U) {
			*native_error = errno == 0 ? EIO : errno;
			status = IO_ERROR_IO;
			break;
		}
	}
	free(cells);
	free(encoded);
	if (status != 0 || !with_sauce)
		return status;
	{
		uint8_t sauce[129] = {0};
		char date[9] = {0};
		time_t now = time(NULL);
		struct tm *local = localtime(&now);
		long length = ftell(stream);
		uint32_t file_size;

		if (length < 0 || (unsigned long)length > UINT32_MAX) {
			*native_error = errno == 0 ? ERANGE : errno;
			return IO_ERROR_OUT_OF_RANGE;
		}
		file_size = (uint32_t)length;
		sauce[0] = 0x1a;
		memcpy(sauce + 1, "SAUCE00", 7);
		copy_sauce_field(sauce + 8, 35, title);
		copy_sauce_field(sauce + 43, 20, author);
		copy_sauce_field(sauce + 63, 20, NULL);
		if (local != NULL && strftime(date, sizeof(date), "%Y%m%d",
		    local) == 8)
			memcpy(sauce + 83, date, 8);
		sauce[91] = (uint8_t)file_size;
		sauce[92] = (uint8_t)(file_size >> 8);
		sauce[93] = (uint8_t)(file_size >> 16);
		sauce[94] = (uint8_t)(file_size >> 24);
		sauce[95] = 5; /* BinaryText SAUCE data type. */
		sauce[96] = (uint8_t)(columns / 2U);
		if ((cterm_render_get_video_flags(terminal)
		    & (ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND
		    | ST_CONSOLE_VIDEO_NO_BLINK)) != 0)
			sauce[106] = 1;
		if (fwrite(sauce, 1, sizeof(sauce), stream) != sizeof(sauce)) {
			*native_error = errno == 0 ? EIO : errno;
			return IO_ERROR_IO;
		}
	}
	return 0;
}

enum st_cterm_encoding
st_cterm_encoding(const st_cterm_t *terminal)
{
	return terminal == NULL ? ST_CTERM_ENCODING_CODEPAGE
	    : terminal->wire_encoding;
}

enum st_codepage
st_cterm_codepage(const st_cterm_t *terminal)
{
	return terminal == NULL ? ST_CODEPAGE_CP437 : terminal->screen.codepage;
}

int
st_cterm_status_display(const st_cterm_t *terminal)
{
	return terminal == NULL ? 0 : terminal->status_display_type;
}

bool
st_cterm_doorway_mode(const st_cterm_t *terminal)
{
	return terminal != NULL && terminal->doorway_mode != 0;
}

void
st_cterm_set_doorway_mode(st_cterm_t *terminal, bool enabled)
{
	if (terminal != NULL)
		terminal->doorway_mode = enabled ? 1 : 0;
}

int
st_cterm_music_mode(const st_cterm_t *terminal)
{
	return terminal == NULL ? CTERM_MUSIC_SYNCTERM
	    : terminal->music_enable;
}

void
st_cterm_set_music_mode(st_cterm_t *terminal, int mode)
{
	if (terminal == NULL)
		return;
	if (mode < CTERM_MUSIC_SYNCTERM)
		mode = CTERM_MUSIC_SYNCTERM;
	if (mode > CTERM_MUSIC_ENABLED)
		mode = CTERM_MUSIC_ENABLED;
	terminal->music_enable = (cterm_music_t)mode;
}

bool
st_cterm_atascii_inverse(const st_cterm_t *terminal)
{
	return terminal != NULL && terminal->atascii_inverse;
}

int
st_cterm_mouse_mode(const st_cterm_t *terminal)
{
	uint16_t modes;

	if (terminal == NULL)
		return 0;
	if (terminal->rip_engine != NULL
	    && (terminal->rip_engine->mouse_fields != NULL
	    || terminal->rip_engine->text_click != NULL
	    || terminal->rip_engine->graphics_click != NULL))
		return 1;
	modes = terminal->api_mouse_modes;
	if ((modes & MOUSE_ANY) != 0) return 1003;
	if ((modes & MOUSE_BUTTON) != 0) return 1002;
	if ((modes & MOUSE_NORMAL) != 0) return 1000;
	if ((modes & MOUSE_X10) != 0) return 9;
	return 0;
}

bool
st_cterm_mouse_sgr_pixels(const st_cterm_t *terminal)
{
	return terminal != NULL
	    && (terminal->api_mouse_modes & MOUSE_SGR_PIXELS) != 0;
}

bool
st_cterm_bracketed_paste(const st_cterm_t *terminal)
{
	return terminal != NULL
	    && (terminal->extattr & CTERM_EXTATTR_BRACKETPASTE) != 0;
}

int
st_cterm_set_encoding(st_cterm_t *terminal, enum st_cterm_encoding encoding,
	enum st_codepage codepage)
{
	int status;

	if (terminal == NULL || encoding < ST_CTERM_ENCODING_UTF8
	    || encoding > ST_CTERM_ENCODING_CODEPAGE
	    || codepage >= ST_CODEPAGE_COUNT
	    || (encoding == ST_CTERM_ENCODING_UTF8
	    && !utf8_supported((enum st_cterm_emulation)terminal->emulation)))
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_cterm_finish(terminal);
	if (status != 0)
		return status;
	if (encoding == ST_CTERM_ENCODING_UTF8 && terminal->screen.unicode == NULL) {
		status = st_unicode_presentation_create(&terminal->screen.unicode,
		    present_unicode, terminal);
		if (status != 0)
			return status;
	}
	else if (encoding == ST_CTERM_ENCODING_CODEPAGE)
		st_unicode_presentation_destroy(&terminal->screen.unicode);
	terminal->wire_encoding = encoding;
	terminal->screen.codepage = codepage;
	if (terminal->rip_engine != NULL) {
		terminal->rip_engine->raster.text_codepage = codepage;
		terminal->rip_engine->raster.unicode_text =
		    encoding == ST_CTERM_ENCODING_UTF8;
		terminal->rip_engine->parser.utf8_input =
		    encoding == ST_CTERM_ENCODING_UTF8;
	}
	return 0;
}

int
st_cterm_copy_history(st_cterm_t *terminal,
	struct st_console_cell **cells, uint32_t *columns, size_t *rows)
{
	struct st_console_cell *copy;
	struct st_console_rect visible;
	size_t line_count;
	size_t cell_count;
	int status;

	if (terminal == NULL || cells == NULL || columns == NULL || rows == NULL
	    || terminal->width <= 0 || terminal->height <= 0
	    || terminal->backfilled < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	line_count = (size_t)terminal->backfilled + (size_t)terminal->height;
	if ((size_t)terminal->width > SIZE_MAX / line_count
	    || (size_t)terminal->width * line_count > SIZE_MAX / sizeof(*copy))
		return IO_ERROR_OUT_OF_RANGE;
	cell_count = (size_t)terminal->width * line_count;
	copy = malloc(cell_count * sizeof(*copy));
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (int line = 0; line < terminal->backfilled; ++line) {
		int source = terminal->backstart + line;

		if (source >= terminal->backlines)
			source -= terminal->backlines;
		memcpy(copy + (size_t)line * terminal->width,
		    terminal->scrollback + (size_t)source * terminal->backwidth,
		    (size_t)terminal->width * sizeof(*copy));
	}
	visible = (struct st_console_rect){
		.x = (uint32_t)(terminal->x - 1),
		.y = (uint32_t)(terminal->y - 1),
		.width = (uint32_t)terminal->width,
		.height = (uint32_t)terminal->height,
	};
	status = st_console_read_cells(terminal->screen.console, &visible,
	    copy + (size_t)terminal->backfilled * terminal->width,
	    (size_t)terminal->width);
	if (status != 0) {
		free(copy);
		return status;
	}
	*cells = copy;
	*columns = (uint32_t)terminal->width;
	*rows = line_count;
	return 0;
}

int
st_cterm_scrollback_size(const st_cterm_t *terminal,
	uint32_t *columns, size_t *rows)
{
	if (terminal == NULL || columns == NULL || rows == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*columns = terminal->scrollback != NULL && terminal->backwidth > 0
	    ? (uint32_t)terminal->backwidth : 0;
	*rows = terminal->scrollback != NULL && terminal->backfilled > 0
	    ? (size_t)terminal->backfilled : 0;
	return 0;
}

int
st_cterm_copy_scrollback_rows(const st_cterm_t *terminal,
	struct st_console_cell *destination, uint32_t destination_columns,
	size_t destination_rows, size_t source_row, size_t row_count,
	size_t destination_row)
{
	size_t columns;
	size_t available;

	if (terminal == NULL || destination == NULL
	    || (destination_rows != 0
	    && destination_columns > SIZE_MAX / destination_rows)
	    || terminal->backfilled < 0 || terminal->backlines < 0
	    || terminal->backwidth < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (terminal->scrollback == NULL || terminal->backlines == 0
	    || terminal->backwidth == 0 || source_row >= (size_t)terminal->backfilled
	    || destination_row >= destination_rows || row_count == 0)
		return 0;
	available = (size_t)terminal->backfilled - source_row;
	if (row_count > available)
		row_count = available;
	available = destination_rows - destination_row;
	if (row_count > available)
		row_count = available;
	columns = (size_t)terminal->backwidth < destination_columns
	    ? (size_t)terminal->backwidth : destination_columns;
	if (columns == 0)
		return 0;
	for (size_t row = 0; row < row_count; ++row) {
		size_t physical = ((size_t)terminal->backstart + source_row + row)
		    % (size_t)terminal->backlines;

		memcpy(destination + (destination_row + row) * destination_columns,
		    terminal->scrollback + physical * terminal->backwidth,
		    columns * sizeof(*destination));
	}
	return (int)row_count;
}

static int
mapped_key(uint16_t key, uint32_t modifiers)
{
	bool shift = (modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0;
	bool control = (modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0;
	bool alt = (modifiers & (ST_CONSOLE_MODIFIER_ALT
	    | ST_CONSOLE_MODIFIER_GUI)) != 0;

	if (key >= ST_CONSOLE_KEY_F1 && key <= ST_CONSOLE_KEY_F12) {
		int number = key - ST_CONSOLE_KEY_F1 + 1;

		return alt ? CTERM_KEY_DOS_ALT_F(number)
		    : control ? CTERM_KEY_DOS_CTRL_F(number)
		    : shift ? CTERM_KEY_DOS_SHIFT_F(number) : CTERM_KEY_DOS_F(number);
	}
	/* Other Alt combinations are host shortcuts or have no reference key. */
	if (alt && key != ST_CONSOLE_KEY_UP && key != ST_CONSOLE_KEY_DOWN
	    && key != ST_CONSOLE_KEY_INSERT && key != ST_CONSOLE_KEY_DELETE)
		return 0xffff;
	switch (key) {
	case ST_CONSOLE_KEY_UP:
		return alt ? 0x9800 : control ? CTERM_KEY_DOS_CTRL_UP
		    : shift ? CTERM_KEY_DOS_SHIFT_UP : CTERM_KEY_DOS_UP;
	case ST_CONSOLE_KEY_DOWN:
		return alt ? 0xa000 : control ? CTERM_KEY_DOS_CTRL_DOWN
		    : shift ? CTERM_KEY_DOS_SHIFT_DOWN : CTERM_KEY_DOS_DOWN;
	case ST_CONSOLE_KEY_LEFT:
		return control ? CTERM_KEY_DOS_CTRL_LEFT
		    : shift ? CTERM_KEY_DOS_SHIFT_LEFT : CTERM_KEY_DOS_LEFT;
	case ST_CONSOLE_KEY_RIGHT:
		return control ? CTERM_KEY_DOS_CTRL_RIGHT
		    : shift ? CTERM_KEY_DOS_SHIFT_RIGHT : CTERM_KEY_DOS_RIGHT;
	case ST_CONSOLE_KEY_INSERT:
		return alt ? 0xa200 : control ? 0x0400 : shift ? 0x0500 : CTERM_KEY_DOS_IC;
	case ST_CONSOLE_KEY_DELETE:
		return alt ? 0xa300 : control ? 0x0600 : shift ? 0x0700 : CTERM_KEY_DOS_DC;
	case ST_CONSOLE_KEY_HOME:
		return control ? 0x7700 : shift ? 0x3700 : CTERM_KEY_DOS_HOME;
	case ST_CONSOLE_KEY_END:
		return control ? CTERM_KEY_DOS_CTRL_END
		    : shift ? CTERM_KEY_DOS_SHIFT_END : CTERM_KEY_DOS_END;
	case ST_CONSOLE_KEY_PAGE_UP:
		return control ? 0x8400 : shift ? 0x3900 : CTERM_KEY_DOS_PPAGE;
	case ST_CONSOLE_KEY_PAGE_DOWN:
		return control ? 0x7600 : shift ? 0x3300 : CTERM_KEY_DOS_NPAGE;
	case '\t': return control ? 0xffff : shift ? CTERM_KEY_DOS_BACKTAB : key;
	case '\r': return control ? '\n' : key;
	case '\b': return control ? 0x7f : key;
	default: return key;
	}
}

bool
st_cterm_handle_key(st_cterm_t *terminal, uint16_t key_code,
    uint32_t modifiers)
{
	struct cterm_rip_engine *engine;
	int doorway_key;

	if (terminal == NULL)
		return false;
	engine = terminal->rip_engine;
	if (engine != NULL && engine->tab_enabled && key_code == '\t') {
		/* The historical handler passes false for Tab, true for Backtab. */
		cterm_rip_engine_next_tab(engine,
		    (modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0);
		return true;
	}
	if (engine != NULL && engine->tab_highlight != NULL) {
		struct cterm_rip_mouse_field *field = engine->tab_highlight;

		cterm_rip_engine_clear_tab(engine);
		if (key_code == '\r' || key_code == '\n') {
			cterm_render_record_error(terminal,
			    rip_activate_field(terminal, field));
			return true;
		}
	}
	if (engine != NULL && key_code != 0 && key_code <= 0x7f
	    && st_cterm_handle_rip_text(terminal, key_code))
		return true;
	doorway_key = mapped_key(key_code, modifiers);
	if (doorway_key >= 0 && doorway_key < 32
	    && (modifiers & (ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT
	    | ST_CONSOLE_MODIFIER_GUI)) == 0
	    && terminal->wire_encoding == ST_CTERM_ENCODING_CODEPAGE) {
		uint8_t byte;

		/* Ordinary control keys arrive as Unicode C0 values, whereas the
		 * reference byte-key tables expect e.g. ATASCII Return as 0x9b.
		 * Explicit Ctrl combinations retain their control-key identity. */
		if (st_codepage_from_unicode(&byte, terminal->screen.codepage,
		    (uint32_t)doorway_key))
			doorway_key = byte;
	}
	return cterm_handle_key(terminal, doorway_key) == CTERM_KEY_HANDLED;
}

bool
st_cterm_handle_text_key(st_cterm_t *terminal, uint32_t codepoint)
{
	uint8_t byte;

	if (terminal == NULL)
		return false;
	if (st_cterm_handle_rip_text(terminal, codepoint))
		return true;
	if (terminal->wire_encoding == ST_CTERM_ENCODING_UTF8) {
		if (codepoint >= 0x80)
			return false;
		byte = (uint8_t)codepoint;
	}
	else if (!st_codepage_from_unicode(&byte, terminal->screen.codepage, codepoint))
		return false;
	/* An encodable byte rejected by this emulation is deliberately silent,
	 * not an invitation to bypass its keyboard policy with a raw send. */
	(void)cterm_handle_key(terminal, byte);
	return true;
}

bool
st_cterm_handle_rip_text(st_cterm_t *terminal, uint32_t codepoint)
{
	struct cterm_rip_engine *engine;

	if (terminal == NULL || (engine = terminal->rip_engine) == NULL)
		return false;
	if (engine->tab_highlight != NULL) {
		cterm_rip_engine_clear_tab(engine);
		return false;
	}
	if (!engine->hotkeys_enabled)
		return false;
	if (codepoint >= 'a' && codepoint <= 'z')
		codepoint -= 'a' - 'A';
	for (struct cterm_rip_mouse_field *field = engine->mouse_fields;
	    field != NULL; field = field->next) {
		int hotkey = field->drawing.hotkey;
		uint32_t decoded;

		if (!field->is_button || hotkey < 0 || hotkey > 0xff)
			continue;
		if (hotkey >= 'a' && hotkey <= 'z')
			hotkey -= 'a' - 'A';
		decoded = hotkey < 0x80 ? (uint32_t)hotkey
		    : st_codepage_to_unicode(terminal->screen.codepage,
		    (uint8_t)hotkey, false);
		if (decoded != codepoint)
			continue;
		cterm_render_record_error(terminal,
		    rip_activate_field(terminal, field));
		return true;
	}
	return false;
}

bool
st_cterm_handle_doorway_key(st_cterm_t *terminal, uint8_t scan_code)
{
	if (terminal != NULL && terminal->rip_engine != NULL
	    && terminal->rip_engine->tab_enabled && scan_code == 0x0f) {
		cterm_rip_engine_next_tab(terminal->rip_engine, true);
		return true;
	}
	if (terminal != NULL && terminal->rip_engine != NULL)
		cterm_rip_engine_clear_tab(terminal->rip_engine);
	return terminal != NULL
	    && cterm_handle_key(terminal, (int)scan_code << 8)
	    == CTERM_KEY_HANDLED;
}

bool
st_cterm_handle_physical_keys(st_cterm_t *terminal,
	const struct st_console_event *events, size_t count)
{
	if (terminal == NULL || (events == NULL && count != 0))
		return false;
	for (size_t i = 0; i < count; ++i) {
		if (events[i].type != ST_CONSOLE_EVENT_PHYSICAL_KEY)
			return false;
	}
	return cterm_pk_events(terminal, events, count);
}

static int
mouse_button_code(uint32_t button)
{
	if (button == ST_CONSOLE_MOUSE_BUTTON_LEFT)
		return 0;
	if (button == ST_CONSOLE_MOUSE_BUTTON_MIDDLE)
		return 1;
	if (button == ST_CONSOLE_MOUSE_BUTTON_RIGHT)
		return 2;
	if (button == ST_CONSOLE_MOUSE_BUTTON_X1)
		return 128;
	if (button == ST_CONSOLE_MOUSE_BUTTON_X2)
		return 129;
	return -1;
}

static size_t
append_mouse_utf8(char *buffer, size_t offset, uint32_t value)
{
	ptrdiff_t count = st_utf8_encode_one(buffer + offset, 16 - offset, value);

	return count < 0 ? 0 : offset + (size_t)count;
}

static int
rip_append_command_bytes(uint8_t **output, size_t *capacity,
    size_t *used, const void *bytes, size_t size)
{
	if (size > SIZE_MAX - *used)
		return IO_ERROR_OUT_OF_RANGE;
	if (*used + size > *capacity) {
		size_t grown = *capacity;
		uint8_t *replacement;

		while (grown < *used + size) {
			if (grown > SIZE_MAX / 2) {
				grown = *used + size;
				break;
			}
			grown *= 2;
		}
		replacement = realloc(*output, grown);
		if (replacement == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		*output = replacement;
		*capacity = grown;
	}
	memcpy(*output + *used, bytes, size);
	*used += size;
	return 0;
}

static int
rip_compat_frame(st_cterm_t *terminal)
{
	/* The legacy COMPAT variable resets the coordinate frame only. */
	struct cterm_rip_raster *raster = &terminal->rip_engine->raster;
	uint32_t width, height;
	int status = st_console_pixel_size(raster->console, &width, &height);

	if (status != 0)
		return status;
	raster->screen_width = width;
	raster->screen_height = height;
	raster->x_dim = 640;
	raster->y_dim = 350;
	raster->x_max = width < 640 ? (int)width : 640;
	raster->y_max = height < 350 ? (int)height : 350;
	raster->viewport.sx = 0;
	raster->viewport.sy = 0;
	raster->viewport.ex = 639;
	raster->viewport.ey = 349;
	return 0;
}

static int
rip_send_mouse_command(st_cterm_t *terminal, const char *command)
{
	size_t length = strlen(command);
	size_t capacity;
	uint8_t *output;
	size_t used = 0;
	int status = 0;

	if (length > (SIZE_MAX - 1) / 2)
		return IO_ERROR_OUT_OF_RANGE;
	capacity = length * 2 + 1;
	output = malloc(capacity);
	if (output == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < length; i++) {
		uint8_t value = (uint8_t)command[i];

		if (value == '$') {
			const char *end = strchr(command + i + 1, '$');
			size_t name_length;

			if (end == NULL)
				continue;
			name_length = (size_t)(end - command - i - 1);
			if (rip_variable_token_equal(command + i + 1,
			    name_length, "COMPAT"))
				status = rip_compat_frame(terminal);
			else {
				char name[13];
				char answer[RIP_QUERY_VALUE_SIZE];
				struct cterm_rip_user_var *variable = NULL;
				bool handled;

				status = rip_query_variable(terminal,
				    command + i + 1, name_length, answer,
				    &handled);
				if (status != 0)
					break;
				if (handled)
					status = rip_append_command_bytes(&output,
					    &capacity, &used, answer,
					    strlen(answer));
				if (status != 0)
					break;
				if (!handled) {
					status = rip_state_variable(terminal,
					    command + i + 1, name_length, &handled);
					if (status != 0)
						break;
					if (!handled && name_length >= 1
					    && name_length <= 12) {
						memcpy(name, command + i + 1,
						    name_length);
						name[name_length] = '\0';
						variable = rip_find_variable(
						    terminal->rip_engine, name);
					}
					if (!handled && variable != NULL
					    && variable->value != NULL)
						status = rip_append_command_bytes(&output,
						    &capacity, &used, variable->value,
						    strlen(variable->value));
				}
			}
			if (status != 0)
				break;
			i = (size_t)(end - command);
			continue;
		}
		if ((value == '^' || value == '`') && i + 1 < length) {
			uint8_t next = (uint8_t)command[i + 1];

			if (next == value) {
				status = rip_append_command_bytes(&output,
				    &capacity, &used, &value, 1);
				if (status != 0)
					break;
				i++;
				continue;
			}
			if (next >= 'a' && next <= 'z')
				next -= 'a' - 'A';
			if (next >= '@' && next <= '_') {
				uint8_t control = next - '@';

				status = rip_append_command_bytes(&output,
				    &capacity, &used, &control, 1);
				if (status != 0)
					break;
				i++;
				continue;
			}
		}
		status = rip_append_command_bytes(&output, &capacity,
		    &used, &value, 1);
		if (status != 0)
			break;
	}
	if (status == 0 && used != 0)
		emit_user((const char *)output, used, terminal);
	free(output);
	return status;
}

static int
rip_activate_field(st_cterm_t *terminal,
    const struct cterm_rip_mouse_field *field)
{
	bool reset_after = field->is_button && field->reset_after;
	int status = rip_send_mouse_command(terminal, field->command);

	/* The command can clear or replace the field list. */
	if (status == 0 && reset_after
	    && terminal->rip_engine->mouse_fields != NULL)
		status = rip_host_command(terminal, 0, 0, '*', "");
	return status;
}

static int
rip_redraw_button(struct cterm_rip_engine *engine,
    struct cterm_rip_mouse_field *field, bool inverted)
{
	int status = cterm_rip_raster_draw_button(&engine->raster,
	    &field->drawing, inverted);

	field->x2 = field->drawing.x2;
	field->y2 = field->drawing.y2;
	return status;
}

static bool
rip_handle_mouse(st_cterm_t *terminal,
    const struct st_console_event *event, uint32_t changed)
{
	struct cterm_rip_engine *engine = terminal->rip_engine;
	struct cterm_rip_mouse_field *field;
	uint32_t cell_width, cell_height;
	int x = event->data.mouse.pixel_x;
	int y = event->data.mouse.pixel_y;
	int status;
	bool text_query;
	bool graphics_query;

	if (engine == NULL)
		return false;
	if (x < 0 || y < 0) {
		if (event->data.mouse.x < 0 || event->data.mouse.y < 0
		    || st_console_cell_pixel_size(terminal->screen.console,
		    &cell_width, &cell_height) != 0
		    || cell_width == 0 || cell_height == 0
		    || (uint32_t)event->data.mouse.x > INT_MAX / cell_width
		    || (uint32_t)event->data.mouse.y > INT_MAX / cell_height)
			return false;
		x = (int)((uint32_t)event->data.mouse.x * cell_width);
		y = (int)((uint32_t)event->data.mouse.y * cell_height);
	}
	x = cterm_rip_raster_unmap_x(&engine->raster, x);
	y = cterm_rip_raster_unmap_y(&engine->raster, y);
	engine->last_mouse_x = x;
	engine->last_mouse_y = y;
	engine->last_mouse_buttons = event->data.mouse.buttons;
	if (event->data.mouse.action != ST_CONSOLE_MOUSE_BUTTON
	    || (changed & ST_CONSOLE_MOUSE_BUTTON_LEFT) == 0)
		return false;
	text_query = engine->text_click != NULL
	    && event->data.mouse.x >= terminal->x + terminal->left_margin - 1
	    && event->data.mouse.x <= terminal->x + terminal->right_margin - 1
	    && event->data.mouse.y >= terminal->y + terminal->top_margin - 1
	    && event->data.mouse.y <= terminal->y + terminal->bottom_margin - 1;
	graphics_query = engine->graphics_click != NULL
	    && x >= engine->raster.viewport.sx
	    && x <= engine->raster.viewport.ex
	    && y >= engine->raster.viewport.sy
	    && y <= engine->raster.viewport.ey;
	if ((event->data.mouse.buttons & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0) {
		for (field = engine->mouse_fields; field != NULL;
		    field = field->next) {
			if (x >= field->x1 && x <= field->x2
			    && y >= field->y1 && y <= field->y2)
				break;
		}
		engine->pressed_field = field;
		if (field == NULL)
			return text_query || graphics_query;
		if (field->is_button) {
			const int flags = field->drawing.style.flags;

			if ((flags & 16384) != 0) {
				for (struct cterm_rip_mouse_field *other =
				    engine->mouse_fields; other != NULL;
				    other = other->next) {
					if (other == field || !other->is_button
					    || !other->selected
					    || (other->drawing.style.flags & 16384) == 0
					    || other->drawing.style.group
					    != field->drawing.style.group)
						continue;
					other->selected = false;
					status = rip_redraw_button(engine, other,
					    false);
					cterm_render_record_error(terminal, status);
				}
				if (!field->selected) {
					field->selected = true;
					status = rip_redraw_button(engine, field,
					    true);
					cterm_render_record_error(terminal, status);
				}
			}
			else if ((field->drawing.style.flags2 & 1) != 0) {
				field->selected = !field->selected;
				status = rip_redraw_button(engine, field,
				    field->selected);
				cterm_render_record_error(terminal, status);
			}
			else if (field->invert) {
				status = rip_redraw_button(engine, field,
				    !field->selected);
				cterm_render_record_error(terminal, status);
			}
		}
		else if (field->invert) {
			status = cterm_rip_raster_invert_rectangle(
			    &engine->raster, field->x1, field->y1,
			    field->x2, field->y2);
			cterm_render_record_error(terminal, status);
		}
		return true;
	}
	field = engine->pressed_field;
	engine->pressed_field = NULL;
	if (field == NULL) {
		if (text_query) {
			engine->mouse_query.x = event->data.mouse.x;
			engine->mouse_query.y = event->data.mouse.y;
			engine->mouse_query.buttons =
			    event->data.mouse.buttons | ST_CONSOLE_MOUSE_BUTTON_LEFT;
			engine->mouse_query.text = true;
			engine->mouse_query.active = true;
			status = rip_send_mouse_command(terminal,
			    engine->text_click);
			engine->mouse_query.active = false;
			cterm_render_record_error(terminal, status);
		}
		if (graphics_query) {
			engine->mouse_query.x = x;
			engine->mouse_query.y = y;
			engine->mouse_query.buttons =
			    event->data.mouse.buttons | ST_CONSOLE_MOUSE_BUTTON_LEFT;
			engine->mouse_query.text = false;
			engine->mouse_query.active = true;
			status = rip_send_mouse_command(terminal,
			    engine->graphics_click);
			engine->mouse_query.active = false;
			cterm_render_record_error(terminal, status);
		}
		return text_query || graphics_query;
	}
	if (field->is_button && field->invert
	    && (field->drawing.style.flags & 16384) == 0
	    && (field->drawing.style.flags2 & 1) == 0) {
		status = rip_redraw_button(engine, field, field->selected);
		cterm_render_record_error(terminal, status);
	}
	else if (!field->is_button && field->invert) {
		status = cterm_rip_raster_invert_rectangle(&engine->raster,
		    field->x1, field->y1, field->x2, field->y2);
		cterm_render_record_error(terminal, status);
	}
	status = rip_send_mouse_command(terminal, field->command);
	cterm_render_record_error(terminal, status);
	if (field->reset_after) {
		status = rip_host_command(terminal, 0, 0, '*', "");
		cterm_render_record_error(terminal, status);
	}
	return true;
}

bool
st_cterm_handle_mouse(st_cterm_t *terminal,
	const struct st_console_event *event)
{
	uint16_t modes;
	uint32_t changed;
	uint32_t button = 0;
	uint32_t x;
	uint32_t y;
	int code;
	bool release = false;
	bool report;
	char sequence[96];
	int length;

	if (terminal == NULL || event == NULL
	    || event->type != ST_CONSOLE_EVENT_MOUSE)
		return false;
	modes = terminal->api_mouse_modes;
	changed = terminal->api_mouse_buttons ^ event->data.mouse.buttons;
	terminal->api_mouse_buttons = event->data.mouse.buttons;
	if (rip_handle_mouse(terminal, event, changed))
		return true;
	if (event->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL) {
		if ((modes & (MOUSE_X10 | MOUSE_NORMAL | MOUSE_BUTTON | MOUSE_ANY))
		    == 0 && (modes & MOUSE_ALT_SCROLL) != 0) {
			int key = event->data.mouse.wheel_y > 0 ? CTERM_KEY_DOS_UP
			    : CTERM_KEY_DOS_DOWN;
			return cterm_handle_key(terminal, key) == CTERM_KEY_HANDLED;
		}
		code = event->data.mouse.wheel_y > 0 ? 64 : 65;
	}
	else if (event->data.mouse.action == ST_CONSOLE_MOUSE_MOVE) {
		report = (modes & MOUSE_ANY) != 0
		    || ((modes & MOUSE_BUTTON) != 0
		    && event->data.mouse.buttons != 0);
		if (!report)
			return false;
		button = event->data.mouse.buttons & (~event->data.mouse.buttons + 1U);
		code = mouse_button_code(button);
		if (code < 0)
			code = 3;
		code += 32;
	}
	else {
		if (changed == 0)
			return false;
		button = changed & (~changed + 1U);
		code = mouse_button_code(button);
		if (code < 0)
			return false;
		release = (event->data.mouse.buttons & button) == 0;
		if (release && (modes & MOUSE_SGR) == 0)
			code = 3;
		if ((modes & MOUSE_X10) != 0 && release)
			return false;
	}
	if ((modes & (MOUSE_X10 | MOUSE_NORMAL | MOUSE_BUTTON | MOUSE_ANY)) == 0)
		return false;
	if ((event->data.mouse.modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0)
		code += 4;
	if ((event->data.mouse.modifiers & ST_CONSOLE_MODIFIER_ALT) != 0)
		code += 8;
	if ((event->data.mouse.modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0)
		code += 16;
	if ((modes & MOUSE_SGR_PIXELS) != 0 && event->data.mouse.pixel_x >= 0
	    && event->data.mouse.pixel_y >= 0) {
		x = (uint32_t)event->data.mouse.pixel_x + 1;
		y = (uint32_t)event->data.mouse.pixel_y + 1;
	}
	else {
		x = event->data.mouse.x < 0 ? 1U : (uint32_t)event->data.mouse.x + 1;
		y = event->data.mouse.y < 0 ? 1U : (uint32_t)event->data.mouse.y + 1;
	}
	if ((modes & (MOUSE_SGR | MOUSE_SGR_PIXELS)) != 0)
		length = snprintf(sequence, sizeof(sequence), "\x1b[<%d;%u;%u%c",
		    code, x, y, release ? 'm' : 'M');
	else if ((modes & MOUSE_URXVT) != 0)
		length = snprintf(sequence, sizeof(sequence), "\x1b[%d;%u;%uM",
		    code + 32, x, y);
	else if ((modes & MOUSE_UTF8) != 0) {
		size_t offset = 3;

		memcpy(sequence, "\x1b[M", 3);
		offset = append_mouse_utf8(sequence, offset, (uint32_t)code + 32);
		if (offset != 0)
			offset = append_mouse_utf8(sequence, offset, x + 32);
		if (offset != 0)
			offset = append_mouse_utf8(sequence, offset, y + 32);
		length = (int)offset;
	}
	else {
		if (code + 32 > UINT8_MAX || x + 32 > UINT8_MAX
		    || y + 32 > UINT8_MAX)
			return false;
		sequence[0] = '\x1b'; sequence[1] = '['; sequence[2] = 'M';
		sequence[3] = (char)(code + 32);
		sequence[4] = (char)(x + 32);
		sequence[5] = (char)(y + 32);
		length = 6;
	}
	if (length <= 0 || (size_t)length >= sizeof(sequence))
		return false;
	emit_user(sequence, (size_t)length, terminal);
	return true;
}

void
st_cterm_sync_mouse_buttons(st_cterm_t *terminal, uint32_t buttons)
{
	if (terminal != NULL)
		terminal->api_mouse_buttons = buttons;
}

bool
st_cterm_handle_focus(st_cterm_t *terminal, bool focused)
{
	static const char gained[] = "\x1b[I";
	static const char lost[] = "\x1b[O";

	if (terminal == NULL || (terminal->api_mouse_modes & MOUSE_FOCUS) == 0)
		return false;
	emit_user(focused ? gained : lost, 3, terminal);
	return true;
}
