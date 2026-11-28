/* CTerm-private rendering support for modern console components. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define CTERM_RENDER_IMPLEMENTATION
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "cterm_vt52.h"
#include "io_error.h"
#include "st_base64.h"
#include "st_builtin_fonts.h"
#include "st_time.h"
#include "st_utf8.h"

bool
cterm_log_enabled(const struct cterminal *cterm, int level)
{
	if (cterm == NULL)
		return false;
	while (cterm->parent != NULL)
		cterm = cterm->parent;
	return cterm->api_diagnostic != NULL && level <= cterm->api_log_level;
}

void
cterm_log(struct cterminal *cterm, int level, const char *format, ...)
{
	char    message[1024];
	va_list arguments;
	if (!cterm_log_enabled(cterm, level))
		return;
	while (cterm->parent != NULL)
		cterm = cterm->parent;
	va_start(arguments, format);
	(void)vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	cterm->api_diagnostic(cterm->api_diagnostic_context, (enum st_cterm_log_level)level, message);
}

void
cterm_log_sequence(struct cterminal *cterm, const char *reason)
{
	char parameters[sizeof(cterm->seq_params)];
	char intro[2] = {0}, intermediate[2] = {0}, final[2] = {0};
	if (!cterm_log_enabled(cterm, ST_CTERM_LOG_WARNING))
		return;
	/* Handlers see parameters after ';' separators have become NULs. */
	size_t length = (size_t)cterm->seq_param_len;
	if (length >= sizeof(parameters))
		length = sizeof(parameters) - 1;
	for (size_t i = 0; i < length; ++i)
		parameters[i] = cterm->seq_params[i] == '\0' ? ';' : cterm->seq_params[i];
	parameters[length] = '\0';
	unsigned value     = (cterm->seq_key >> SEQ_INTRO_SHIFT) & SEQ_INTRO_MASK;
	if (value >= 32)
		intro[0] = (char)(value + 0x20);
	value = (cterm->seq_key >> SEQ_INTERM_SHIFT) & SEQ_INTERM_MASK;
	const char *intermediates = intermediate;
	if (value == SEQ_INTERM_MASK)
		intermediates = "<multiple intermediates>";
	else if (value != 0)
		intermediate[0] = (char)(value + 0x1f);
	value = (cterm->seq_key >> SEQ_FINAL_SHIFT) & SEQ_FINAL_MASK;
	if (value != 0)
		final[0] = (char)(value + 0x2f);
	cterm_log(cterm, ST_CTERM_LOG_WARNING, "%s: ESC %s%s%s%s (key=0x%08" PRIX32 ")", reason, intro, parameters,
	    intermediates, final, cterm->seq_key);
}

void
cterm_render_record_error(struct cterminal *cterm, int error)
{
	if (cterm != NULL && error != 0 && cterm->screen.error == 0) {
		cterm->screen.error = error;
		cterm_log(cterm, ST_CTERM_LOG_ERROR, "Rendering failed (%d)", error);
	}
}

int
cterm_render_take_error(struct cterminal *cterm)
{
	int error;

	if (cterm == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error               = cterm->screen.error;
	cterm->screen.error = 0;
	return error;
}

static bool
render_succeeded(struct cterminal *cterm, int status)
{
	if (status != 0)
		cterm_render_record_error(cterm, status);
	return status == 0;
}

static void
attribute_colours(struct cterminal *cterm, uint8_t attribute, uint32_t flags, uint32_t *foreground,
    uint32_t *background)
{
	unsigned fg    = attribute & 15U;
	unsigned bg    = (attribute >> 4) & 15U;

	if (cterm->device.sgr_palette != NULL) {
		fg &= cterm->device.pen_mask;
		bg &= cterm->device.pen_mask;
	}
	else {
		if ((flags & ST_CONSOLE_VIDEO_NO_BRIGHT) != 0)
			fg &= 7U;
		if ((flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) == 0)
			bg &= 7U;
	}
	if (foreground != NULL)
		*foreground = cterm->screen.mode_palette[fg];
	if (background != NULL)
		*background = cterm->screen.mode_palette[bg];
}

void
cterm_render_attribute_colours(struct cterminal *cterm, uint8_t attribute, uint32_t *foreground,
    uint32_t *background)
{
	attribute_colours(cterm, attribute, (uint32_t)cterm_render_get_video_flags(cterm), foreground, background);
}

/* Swapping the red and blue bits converts between CGA and ANSI order in either direction. */
const uint8_t cterm_ansi_colour_order[8] = {0, 4, 2, 6, 1, 5, 3, 7};

uint32_t
cterm_faint_base(const struct cterminal *cterm)
{
	return CTERM_FAINT_BASE + ((cterm->screen.capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) ? 16 : 0);
}

void
cterm_rendition_colours(struct cterminal *cterm, uint8_t attribute, uint32_t *foreground, uint32_t *background)
{
	if (cterm->faint && foreground != NULL) {
		*foreground = cterm_faint_base(cterm) + cterm_ansi_colour_order[attribute & 7];
		foreground = NULL;
	}
	if (foreground != NULL || background != NULL)
		cterm_render_attribute_colours(cterm, attribute, foreground, background);
}

static uint16_t
font_identifier(const struct cterminal *cterm, uint8_t selector)
{
	if (cterm->screen.font_valid[selector])
		return cterm->screen.font_identifiers[selector];
	return cterm->screen.fonts.normal;
}

static uint8_t
attribute_font(const struct cterminal *cterm, uint8_t attribute, uint32_t flags)
{
	/* IGS uses color-register bits, not ANSI intensity/blink font banks. */
	if (cterm->vt52_plane_mask != 0)
		return (uint8_t)cterm->screen.selectors[1];
	int slot = 1;

	if ((attribute & 8U) != 0 && (flags & ST_CONSOLE_VIDEO_ALT_CHARS) != 0)
		slot += 1;
	if ((attribute & CTERM_ATTR_BLINK) != 0 && (flags & ST_CONSOLE_VIDEO_BLINK_ALT_CHARS) != 0)
		slot += 2;
	return (uint8_t)cterm->screen.selectors[slot];
}

uint8_t
cterm_render_attribute_font(struct cterminal *cterm, uint8_t attribute)
{
	if ((attribute & (8U | CTERM_ATTR_BLINK)) == 0 || cterm->vt52_plane_mask != 0)
		return (uint8_t)cterm->screen.selectors[1];
	return attribute_font(cterm, attribute, (uint32_t)cterm_render_get_video_flags(cterm));
}

static uint32_t
attribute_flags(const struct cterminal *cterm, uint8_t attribute, uint32_t flags)
{
	if ((flags & ST_CONSOLE_CELL_SAA5050) != 0 && (attribute & 8U) != 0) {
		flags |= ST_CONSOLE_CELL_CONCEAL;
		if (cterm->screen.prestel_reveal)
			flags |= ST_CONSOLE_CELL_REVEAL;
	}
	else if ((attribute & 8U) != 0)
		flags |= ST_CONSOLE_CELL_BRIGHT;
	if ((attribute & CTERM_ATTR_BLINK) != 0)
		flags |= ST_CONSOLE_CELL_BLINK;
	if (cterm->device.sgr_palette != NULL || cterm->vt52_plane_mask != 0)
		flags &= ~(ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK);
	if (!(flags & ST_CONSOLE_CELL_BLINK))
		flags &= ~ST_CONSOLE_CELL_FAST_BLINK;
	return flags;
}

int
cterm_render_make_cell(struct cterminal *cterm, struct st_console_cell *cell, uint8_t byte, uint8_t attribute,
    uint32_t foreground, uint32_t background, uint8_t font_selector, uint16_t hyperlink, uint32_t flags)
{
	uint16_t selected;
	uint32_t scalar;
	uint8_t  columns;

	if (cterm == NULL || cell == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	selected = font_identifier(cterm, font_selector);
	scalar   = st_codepage_to_unicode(cterm->screen.codepage, byte, true);
	/* CTerm has already resolved attributes through its mode palette. */
	*cell = (struct st_console_cell) {
	    .foreground = foreground,
	    .background = background,
	    .flags      = attribute_flags(cterm, attribute, flags),
	    .font       = selected,
	    .glyph_font = selected,
	    .hyperlink  = hyperlink,
	};
	return st_console_font_lookup(cterm->screen.console, selected, scalar, &cell->glyph_font, &cell->glyph,
	    &columns);
}

int
cterm_render_make_current_cell(struct cterminal *cterm, struct st_console_cell *cell, uint8_t byte,
    uint16_t hyperlink)
{
	return cterm_render_make_cell(cterm, cell, byte, cterm->attr, cterm->fg_color, cterm->bg_color,
	    cterm_render_attribute_font(cterm, cterm->attr), hyperlink, cterm->cell_flags);
}

uint8_t
cterm_render_cell_byte(struct cterminal *cterm, const struct st_console_cell *cell)
{
	char     text[8];
	size_t   required = 0;
	uint32_t scalar   = UINT32_C(0xfffd);
	size_t   consumed;
	uint8_t  byte = '?';

	if (st_console_font_glyph_text(cterm->screen.console, cell->glyph_font, cell->glyph, text, sizeof(text),
		&required)
	        == 0
	    && required != 0)
		(void)st_utf8_decode_one(&scalar, &consumed, text, required);
	if (st_codepage_from_unicode(&byte, cterm->screen.codepage, scalar)
	    && cterm->emulation == CTERM_EMULATION_ATASCII && cterm->screen.codepage == ST_CODEPAGE_ATASCII
	    && (cell->flags & ST_CONSOLE_CELL_REVERSE))
		byte ^= 0x80;
	return byte;
}

uint8_t
cterm_render_cell_font(struct cterminal *cterm, const struct st_console_cell *cell)
{
	for (size_t i = 0; i < UINT8_MAX + 1U; ++i) {
		if (cterm->screen.font_valid[i] && cterm->screen.font_identifiers[i] == cell->font)
			return (uint8_t)i;
	}
	return 0;
}

uint8_t
cterm_render_cell_attribute(struct cterminal *cterm, const struct st_console_cell *cell)
{
	uint32_t foreground = 0;
	uint32_t background = 0;
	uint8_t  attribute;

	for (size_t i = 0; i < 16; ++i) {
		if (cterm->screen.mode_palette[i] == cell->foreground) {
			foreground = (uint32_t)i;
			break;
		}
	}
	for (size_t i = 0; i < 8; ++i) {
		if (cterm->screen.mode_palette[i] == cell->background) {
			background = (uint32_t)i;
			break;
		}
	}
	uint32_t base = cterm_faint_base(cterm);
	if (cell->foreground - base < 8)
		foreground = cterm_ansi_colour_order[cell->foreground - base];
	if (cell->background - base < 8)
		background = cterm_ansi_colour_order[cell->background - base];
	attribute = (uint8_t)((foreground & 7U) | ((background & 7U) << 4));
	if ((cell->flags & (ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_CONCEAL)) != 0)
		attribute |= 8U;
	if ((cell->flags & ST_CONSOLE_CELL_BLINK) != 0)
		attribute |= CTERM_ATTR_BLINK;
	return attribute;
}

int
cterm_render_get_video_flags(struct cterminal *cterm)
{
	uint32_t flags = 0;
	(void)render_succeeded(cterm, st_console_get_video_flags(cterm->screen.console, &flags));
	return (int)flags;
}

void
cterm_render_set_video_flags(struct cterminal *cterm, int flags)
{
	(void)render_succeeded(cterm,
	    st_console_set_video_flags(cterm->screen.console, (uint32_t)flags & ST_CONSOLE_VIDEO_FLAGS));
}

void
cterm_render_set_window(struct cterminal *cterm, int left, int top, int right, int bottom)
{
	struct st_console_rect window;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return;
	window = (struct st_console_rect) {
	    .x      = (uint32_t)(left - 1),
	    .y      = (uint32_t)(top - 1),
	    .width  = (uint32_t)(right - left + 1),
	    .height = (uint32_t)(bottom - top + 1),
	};
	struct st_console_rect physical = window;
	bool                   rip_window = cterm->screen.addressing_window.width != 0;
	if (rip_window) {
		/* RIP g/H may position the cursor outside the ANSI addressing region. */
		physical = (struct st_console_rect) {(uint32_t)(cterm->x - 1), (uint32_t)(cterm->y - 1),
		    (uint32_t)cterm->width, (uint32_t)cterm->height};
	}
	if (!render_succeeded(cterm, st_console_set_window(cterm->screen.console, &physical)))
		return;
	if (rip_window)
		cterm->screen.addressing_window = window;
}

void
cterm_render_set_position(struct cterminal *cterm, int x, int y)
{
	struct st_console_rect window = cterm->screen.addressing_window;
	if (window.width != 0) {
		int64_t physical_x = (int64_t)window.x + x - 1;
		int64_t physical_y = (int64_t)window.y + y - 1;
		if (physical_x < cterm->x - 1 || physical_y < cterm->y - 1
		    || physical_x >= (int64_t)cterm->x - 1 + cterm->width
		    || physical_y >= (int64_t)cterm->y - 1 + cterm->height)
			return;
		(void)render_succeeded(cterm,
		    st_console_set_position(cterm->screen.console, (uint32_t)physical_x, (uint32_t)physical_y));
	}
	else if (x >= 1 && y >= 1)
		(void)render_succeeded(cterm,
		    st_console_set_window_position(cterm->screen.console, (uint32_t)x - 1, (uint32_t)y - 1));
}

void
cterm_render_get_position(struct cterminal *cterm, int *x, int *y)
{
	const struct st_console_rect *window = &cterm->screen.addressing_window;
	uint32_t                      screen_x, screen_y;
	int                           column = 1, row = 1;

	if (x == NULL && y == NULL)
		return;
	if (window->width != 0) {
		if (render_succeeded(cterm,
			st_console_get_position(cterm->screen.console, &screen_x, &screen_y))) {
			column += (int)screen_x - (int)window->x;
			row += (int)screen_y - (int)window->y;
		}
	}
	else if (render_succeeded(cterm,
		     st_console_get_window_position(cterm->screen.console, &screen_x, &screen_y))) {
		column += (int)screen_x;
		row += (int)screen_y;
	}
	if (x != NULL)
		*x = column;
	if (y != NULL)
		*y = row;
}

int
cterm_render_position_x(struct cterminal *cterm)
{
	int x;
	cterm_render_get_position(cterm, &x, NULL);
	return x;
}

int
cterm_render_position_y(struct cterminal *cterm)
{
	int y;
	cterm_render_get_position(cterm, NULL, &y);
	return y;
}

void
cterm_render_set_attribute(struct cterminal *cterm, int attribute)
{
	struct st_console_text_edit edit;
	if (!render_succeeded(cterm, st_console_begin_text_edit(cterm->screen.console, &edit)))
		return;
	cterm->attr = (uint8_t)attribute;
	attribute_colours(cterm, cterm->attr, edit.video_flags, &edit.style.foreground, &edit.style.background);
	edit.style.flags = attribute_flags(cterm, cterm->attr, cterm->cell_flags)
	                   | ST_CONSOLE_CELL_LEGACY_FOREGROUND | ST_CONSOLE_CELL_LEGACY_BACKGROUND;
	edit.style.font      = font_identifier(cterm, attribute_font(cterm, cterm->attr, edit.video_flags));
	edit.style.hyperlink = cterm->screen.current_hyperlink;
	(void)render_succeeded(cterm, st_console_end_text_edit(&edit, NULL));
}

static void
set_colours(const struct cterminal *cterm, struct st_console_style *style, uint32_t foreground,
    uint32_t background)
{
	style->foreground = foreground;
	style->background = background;
	style->flags &= ~(ST_CONSOLE_CELL_SAA5050 | ST_CONSOLE_CELL_DOUBLE_HEIGHT | ST_CONSOLE_CELL_SEPARATED
			  | ST_CONSOLE_CELL_TERMINAL_FONT | ST_CONSOLE_CELL_PIXEL_GRAPHICS | ST_CONSOLE_CELL_REVEAL
			  | ST_CONSOLE_CELL_SAA5050_CONTROL_MASK | ST_CONSOLE_CELL_LEGACY_FOREGROUND
			  | ST_CONSOLE_CELL_LEGACY_BACKGROUND | ST_CONSOLE_CELL_UNDERLINE | ST_CONSOLE_CELL_FAST_BLINK);
	style->flags |= cterm->cell_flags;
}

void
cterm_render_set_colours(struct cterminal *cterm, uint32_t foreground, uint32_t background)
{
	struct st_console_text_edit edit;
	if (!render_succeeded(cterm, st_console_begin_text_edit(cterm->screen.console, &edit)))
		return;
	set_colours(cterm, &edit.style, foreground, background);
	(void)render_succeeded(cterm, st_console_end_text_edit(&edit, NULL));
}

static void
text_cursor(const struct cterminal *cterm, int cursor, struct st_console_custom_cursor *custom)
{
	custom->visible = cursor != CTERM_CURSOR_HIDDEN;
	if (cterm->cursor_style != 0)
		custom->blink = (cterm->cursor_style & 1) != 0;
	if (cterm->cursor_style == 3 || cterm->cursor_style == 4) {
		custom->start = custom->range - (custom->range > 8 ? 2U : 1U);
		custom->end   = custom->range - 1;
	}
	else if (cursor == CTERM_CURSOR_BLOCK || cterm->cursor_style == 1 || cterm->cursor_style == 2) {
		custom->start = 0;
		custom->end   = custom->range - 1;
	}
}

void
cterm_render_set_cursor(struct cterminal *cterm, int cursor)
{
	struct st_console_text_edit edit;
	if (cterm->vt52_plane_mask != 0) {
		cterm_vt52_render_cursor(cterm, cursor);
		return;
	}
	if (cterm->device.cursor_mode != NULL) {
		cterm->device.cursor_mode(cterm->device.context, cursor == CTERM_CURSOR_HIDDEN);
		return;
	}
	if (!render_succeeded(cterm, st_console_begin_text_edit(cterm->screen.console, &edit)))
		return;
	text_cursor(cterm, cursor, &edit.default_cursor);
	(void)render_succeeded(cterm, st_console_end_text_edit(&edit, &edit.default_cursor));
}

int
cterm_render_prepare_text(struct cterminal *cterm, struct st_console_style *style,
    struct st_console_text_glyph *glyph)
{
	struct st_console_text_edit edit;
	int                         status = st_console_begin_text_edit(cterm->screen.console, &edit);
	if (status != 0)
		return status;
	struct cterm_render_state *screen = &cterm->screen;
	struct st_console_style   *cached = &screen->prepared_text.style;
	uint16_t font           = font_identifier(cterm, attribute_font(cterm, cterm->attr, edit.video_flags));
	bool     device_colours = cterm->device.sgr_palette != NULL || cterm->vt52_plane_mask != 0;
	if (!screen->prepared_text.valid || screen->prepared_text.attr != cterm->attr
	    || screen->prepared_text.cell_flags != cterm->cell_flags
	    || screen->prepared_text.reveal != screen->prestel_reveal
	    || screen->prepared_text.device_colours != device_colours || cached->font != font
	    || cached->hyperlink != screen->current_hyperlink || cached->foreground != cterm->fg_color
	    || cached->background != cterm->bg_color) {
		cached->flags = attribute_flags(cterm, cterm->attr, cterm->cell_flags);
		set_colours(cterm, cached, cterm->fg_color, cterm->bg_color);
		cached->font                         = font;
		cached->hyperlink                    = screen->current_hyperlink;
		screen->prepared_text.attr           = cterm->attr;
		screen->prepared_text.cell_flags     = cterm->cell_flags;
		screen->prepared_text.reveal         = screen->prestel_reveal;
		screen->prepared_text.device_colours = device_colours;
		screen->prepared_text.valid          = true;
	}
	edit.style = *cached;
	if (cterm->vt52_plane_mask != 0 || cterm->device.cursor_mode != NULL) {
		status = st_console_end_text_edit(&edit, NULL);
		if (status == 0)
			cterm_render_set_cursor(cterm, cterm->cursor);
	}
	else {
		text_cursor(cterm, cterm->cursor, &edit.default_cursor);
		edit.glyph = glyph;
		status = st_console_end_text_edit(&edit, &edit.default_cursor);
	}
	if (style != NULL)
		*style = edit.style;
	return status;
}

bool
cterm_render_get_custom_cursor(struct cterminal *cterm, struct st_console_custom_cursor *cursor)
{
	return render_succeeded(cterm, st_console_get_custom_cursor(cterm->screen.console, cursor));
}

bool
cterm_render_set_custom_cursor(struct cterminal *cterm, const struct st_console_custom_cursor *cursor)
{
	return render_succeeded(cterm, st_console_set_custom_cursor(cterm->screen.console, cursor));
}

void
cterm_render_beep(struct cterminal *cterm)
{
	if (cterm->device.bell != NULL) {
		cterm_render_record_error(cterm, cterm->device.bell(cterm->device.context));
		return;
	}
	audio_engine_t *engine = NULL;
	int             hold   = cterm_render_hold_update(cterm);
	/* audio_beep drains its stream, including the platform-beep fallback. */
	(void)cterm_render_set_hold_update(cterm, 0);
	if (cterm->screen.audio != NULL && st_audio_runtime_acquire(cterm->screen.audio, &engine) == 0)
		(void)audio_beep(engine, 440.0, 100);
	(void)cterm_render_set_hold_update(cterm, hold);
}

void
cterm_render_clear_window(struct cterminal *cterm)
{
	if (cterm->screen.addressing_window.width != 0) {
		struct st_console_rect window = cterm->screen.addressing_window;
		struct st_console_cell blank;
		if (render_succeeded(cterm, cterm_render_make_current_cell(cterm, &blank, ' ', 0))
		    && render_succeeded(cterm, st_console_fill(cterm->screen.console, &window, &blank)))
			cterm_render_set_position(cterm, 1, 1);
		return;
	}
	(void)render_succeeded(cterm, st_console_clear_window(cterm->screen.console, ' '));
}

void
cterm_render_clear_line(struct cterminal *cterm)
{
	if (cterm->screen.addressing_window.width != 0) {
		struct st_console_rect window = cterm->screen.addressing_window;
		struct st_console_cell blank;
		uint32_t               x, y;
		if (!render_succeeded(cterm, st_console_get_position(cterm->screen.console, &x, &y))
		    || x >= window.x + window.width)
			return;
		window = (struct st_console_rect) {x, y, window.x + window.width - x, 1};
		if (render_succeeded(cterm, cterm_render_make_current_cell(cterm, &blank, ' ', 0)))
			(void)render_succeeded(cterm, st_console_fill(cterm->screen.console, &window, &blank));
		return;
	}
	(void)render_succeeded(cterm, st_console_clear_line(cterm->screen.console, ' '));
}

int
cterm_render_fill_blank(struct cterminal *cterm, int left, int top, int right, int bottom, uint32_t background)
{
	if (left < 1 || top < 1 || right < left || bottom < top)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_console_cell blank;
	int                    status = cterm_render_make_current_cell(cterm, &blank, ' ', 0);
	if (status != 0)
		return status;
	blank.background                  = background;
	const struct st_console_rect rect = {
	    .x      = (uint32_t)(left - 1),
	    .y      = (uint32_t)(top - 1),
	    .width  = (uint32_t)(right - left + 1),
	    .height = (uint32_t)(bottom - top + 1),
	};
	return st_console_fill(cterm->screen.console, &rect, &blank);
}

int
cterm_render_write_cells(struct cterminal *cterm, int left, int top, int right, int bottom,
    const struct st_console_cell *cells)
{
	struct st_console_rect rect;
	int                    status;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return 0;
	rect   = (struct st_console_rect) {(uint32_t)(left - 1), (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	status = st_console_write_complete_cells(cterm->screen.console, &rect, cells, rect.width);
	if (status != 0)
		cterm_render_record_error(cterm, status);
	return status == 0;
}

int
cterm_render_read_cells(struct cterminal *cterm, int left, int top, int right, int bottom,
    struct st_console_cell *cells)
{
	struct st_console_rect rect;
	int                    status;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return 0;
	rect   = (struct st_console_rect) {(uint32_t)(left - 1), (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	status = st_console_read_cells(cterm->screen.console, &rect, cells, rect.width);
	if (status != 0)
		cterm_render_record_error(cterm, status);
	return status == 0;
}

int
cterm_render_move_cells(struct cterminal *cterm, int left, int top, int right, int bottom, int destination_left,
    int destination_top)
{
	struct st_console_rect rect;

	if (left < 1 || top < 1 || right < left || bottom < top || destination_left < 1 || destination_top < 1)
		return 0;
	rect = (struct st_console_rect) {(uint32_t)(left - 1), (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	return render_succeeded(cterm, st_console_move_cells(cterm->screen.console, &rect,
					   (uint32_t)(destination_left - 1), (uint32_t)(destination_top - 1)));
}

int
cterm_render_move_cells_clear(struct cterminal *cterm, int left, int top, int right, int bottom,
    int destination_left, int destination_top, const struct st_console_cell *fill)
{
	struct st_console_rect rect;

	if (left < 1 || top < 1 || right < left || bottom < top || destination_left < 1 || destination_top < 1)
		return 0;
	rect = (struct st_console_rect) {(uint32_t)(left - 1), (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	return render_succeeded(cterm,
	    st_console_move_cells_clear(cterm->screen.console, &rect, (uint32_t)(destination_left - 1),
		(uint32_t)(destination_top - 1), fill));
}

int
cterm_render_blit_pixels(struct cterminal *cterm, const struct st_pixel_buffer *pixels,
    const struct st_pixel_mask *mask, const struct st_pixel_blit *blit)
{
	if (pixels == NULL || pixels->primary == NULL || blit == NULL) {
		cterm_render_record_error(cterm, IO_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	return render_succeeded(cterm, st_console_blit_pixels(cterm->screen.console, pixels, mask, blit));
}

int
cterm_render_set_palette(struct cterminal *cterm, uint32_t entry, uint16_t red, uint16_t green, uint16_t blue)
{
	uint32_t colour;

	if (entry > UINT16_MAX)
		return 0;
	colour = ((uint32_t)(red >> 8) << 16) | ((uint32_t)(green >> 8) << 8) | (blue >> 8);
	return render_succeeded(cterm, st_console_set_palette(cterm->screen.console, (uint16_t)entry, &colour, 1));
}

int
cterm_render_get_palette(struct cterminal *cterm, uint32_t entry, uint8_t *red, uint8_t *green, uint8_t *blue)
{
	uint32_t colour;

	if (entry > UINT16_MAX)
		return 0;
	if (!render_succeeded(cterm, st_console_get_palette(cterm->screen.console, (uint16_t)entry, &colour, 1)))
		return 0;
	*red   = (uint8_t)(colour >> 16);
	*green = (uint8_t)(colour >> 8);
	*blue  = (uint8_t)colour;
	return 1;
}

uint32_t
cterm_render_map_rgb(uint16_t red, uint16_t green, uint16_t blue)
{
	return ST_CONSOLE_COLOR_RGB_VALUE(red >> 8, green >> 8, blue >> 8);
}

static int
font_variant(uint32_t width, uint32_t height)
{
	if (width == 12 && height == 20)
		return 3;
	if (width != 8)
		return -1;
	if (height == 8)
		return 0;
	if (height == 14)
		return 1;
	if (height == 16)
		return 2;
	return -1;
}

void
cterm_render_refresh_remote_fonts(struct cterminal *cterm)
{
	uint32_t width, height;
	int      variant;

	if (!render_succeeded(cterm, st_console_cell_pixel_size(cterm->screen.console, &width, &height)))
		return;
	variant = font_variant(width, height);
	for (size_t selector = 0; selector < 256; ++selector) {
		if (cterm->screen.remote_fonts[selector].present == 0)
			continue;
		if (variant >= 0 && (cterm->screen.remote_fonts[selector].present & (1U << variant)) != 0) {
			cterm->screen.font_identifiers[selector] =
			    cterm->screen.remote_fonts[selector].identifiers[variant];
			cterm->screen.font_valid[selector] = true;
			cterm->screen.font_codepages[selector] =
			    cterm->screen.remote_fonts[selector].codepages[variant];
		}
		else {
			cterm->screen.font_identifiers[selector] = cterm->screen.remote_fonts[selector].original;
			cterm->screen.font_valid[selector] = cterm->screen.remote_fonts[selector].original_valid;
			cterm->screen.font_codepages[selector] =
			    cterm->screen.remote_fonts[selector].original_codepage;
		}
	}
}

void
cterm_render_replace_font(struct cterminal *cterm, uint8_t selector, char *name, void *data, size_t size)
{
	uint32_t               width = 8, height;
	uint16_t               identifier;
	struct st_console_font font;
	int                    variant;

	(void)name;
	while (cterm->parent != NULL)
		cterm = cterm->parent;
	switch (size) {
		case 2048:
			height = 8;
			break;
		case 3584:
			height = 14;
			break;
		case 4096:
			height = 16;
			break;
		case 10240:
			width  = 12;
			height = 20;
			break;
		default:
			goto done;
	}
	variant = font_variant(width, height);
	font    = (struct st_console_font) {
	    .bitmap       = data,
	    .bitmap_size  = size,
	    .width        = width,
	    .height       = height,
	    .glyph_count  = 256,
	    .row_stride   = ((size_t)width + 7) / 8,
	    .glyph_stride = size / 256,
	};
	if (!st_builtin_font_map_codepage(cterm->screen.codepage, &font))
		goto done;
	if (!render_succeeded(cterm, st_console_register_font_copy(cterm->screen.console, &font, &identifier)))
		goto done;
	if (cterm->screen.remote_fonts[selector].present == 0) {
		cterm->screen.remote_fonts[selector].original          = cterm->screen.font_identifiers[selector];
		cterm->screen.remote_fonts[selector].original_valid    = cterm->screen.font_valid[selector];
		cterm->screen.remote_fonts[selector].original_codepage = cterm->screen.font_codepages[selector];
	}
	cterm->screen.remote_fonts[selector].identifiers[variant] = identifier;
	cterm->screen.remote_fonts[selector].codepages[variant]   = cterm->screen.codepage;
	cterm->screen.remote_fonts[selector].present |= (uint8_t)(1U << variant);
	cterm_render_refresh_remote_fonts(cterm);
	if (cterm->status_sub != NULL) {
		cterm->status_sub->screen.remote_fonts[selector] = cterm->screen.remote_fonts[selector];
		cterm_render_refresh_remote_fonts(cterm->status_sub);
	}
done:
	free(data);
	free(name);
}

int
cterm_render_get_modepalette(struct cterminal *cterm, uint32_t *palette)
{
	for (uint32_t i = 0; i < 16; ++i)
		palette[i] = cterm->screen.mode_palette[i];
	return 1;
}

int
cterm_render_set_modepalette(struct cterminal *cterm, const uint32_t *palette)
{
	if (palette == NULL)
		return 0;
	for (size_t i = 0; i < 16; ++i) {
		if (palette[i] > UINT16_MAX)
			return 0;
	}
	memcpy(cterm->screen.mode_palette, palette, sizeof(cterm->screen.mode_palette));
	return 1;
}

int
cterm_render_set_hold_update(struct cterminal *cterm, int hold)
{
	int normalized = hold != 0;
	int status     = 0;

	if (normalized == (cterm->screen.hold_update != 0))
		return cterm->screen.hold_update;
	if (normalized)
		status = st_console_begin_update(cterm->screen.console);
	else
		status = st_console_end_update(cterm->screen.console);
	if (render_succeeded(cterm, status))
		cterm->screen.hold_update = normalized;
	return cterm->screen.hold_update;
}

int
cterm_render_hold_update(const struct cterminal *cterm)
{
	return cterm->screen.hold_update;
}

uint32_t
cterm_default_colour(uint16_t entry, bool yellow_is_yellow)
{
	uint32_t colour;
	(void)st_console_get_default_palette(entry, &colour, 1);
	if (yellow_is_yellow && entry == 3)
		colour = (colour & 0xff00ff) | ((colour >> 8) & 0xff00);
	return colour;
}

int
cterm_render_font_selector(struct cterminal *cterm, int slot)
{
	if (slot < 0 || slot >= 5)
		return 0;
	return cterm->screen.selectors[slot];
}

int
cterm_render_set_font_selector(struct cterminal *cterm, int selector, int force, int slot)
{
	(void)force;
	if (selector < 0 || selector > UINT8_MAX || slot < 0 || slot >= 5 || !cterm->screen.font_valid[selector])
		return 0;
	cterm->screen.selectors[slot] = (uint16_t)selector;
	return 1;
}

uint16_t
cterm_render_add_hyperlink(struct cterminal *cterm, const char *uri, const char *parameter)
{
	uint16_t identifier = 0;
	int      status;

	if (cterm->screen.hyperlinks != NULL && uri != NULL) {
		status = st_hyperlink_registry_add(cterm->screen.hyperlinks, uri, strlen(uri), parameter,
		    parameter == NULL ? 0 : strlen(parameter), &identifier);
		cterm_render_record_error(cterm, status);
	}
	return identifier;
}

static char *
hyperlink_part(struct cterminal *cterm, uint16_t identifier, bool parameter)
{
	struct st_hyperlink link;
	char               *result;

	if (cterm->screen.hyperlinks == NULL
	    || st_hyperlink_registry_copy(cterm->screen.hyperlinks, identifier, &link) != 0)
		return NULL;
	result = parameter ? link.parameter : link.uri;
	if (parameter)
		free(link.uri);
	else
		free(link.parameter);
	return result;
}

char *
cterm_render_hyperlink_url(struct cterminal *cterm, uint16_t identifier)
{
	return hyperlink_part(cterm, identifier, false);
}

char *
cterm_render_hyperlink_params(struct cterminal *cterm, uint16_t identifier)
{
	return hyperlink_part(cterm, identifier, true);
}

void
cterm_render_set_hyperlink(struct cterminal *cterm, uint16_t identifier)
{
	struct st_console_text_edit edit;

	cterm->screen.current_hyperlink = identifier;
	if (render_succeeded(cterm, st_console_begin_text_edit(cterm->screen.console, &edit))) {
		edit.style.hyperlink = identifier;
		(void)render_succeeded(cterm, st_console_end_text_edit(&edit, NULL));
	}
}

uint16_t
cterm_render_get_hyperlink(struct cterminal *cterm)
{
	return cterm->screen.current_hyperlink;
}

uint64_t
cterm_render_timer64(void)
{
	uint64_t now;

	if (st_time_monotonic_ms(&now) != 0)
		return 0;
	return now;
}

uint16_t
cterm_render_crc16(const void *data, size_t size, uint16_t initial)
{
	const uint8_t *bytes = data;
	uint16_t       crc   = initial;

	for (size_t i = 0; i < size; ++i) {
		crc ^= (uint16_t)bytes[i] << 8;
		for (unsigned bit = 0; bit < 8; ++bit)
			crc = (crc & UINT16_C(0x8000)) != 0 ? (uint16_t)((crc << 1) ^ UINT16_C(0x1021))
			                                    : (uint16_t)(crc << 1);
	}
	return crc;
}

int
cterm_render_base64_decode(void *destination, size_t capacity, const char *source)
{
	size_t size;

	if (!st_base64_decoded_size(source, strlen(source), &size) || size > capacity
	    || !st_base64_decode(destination, capacity, source, strlen(source)))
		return -1;
	return size > INT32_MAX ? -1 : (int)size;
}

int
cterm_render_asprintf(char **result, const char *format, ...)
{
	va_list arguments;
	va_list copy;
	int     length;
	char   *buffer;

	va_start(arguments, format);
	va_copy(copy, arguments);
	length = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (length < 0 || (size_t)length == SIZE_MAX) {
		va_end(arguments);
		return -1;
	}
	buffer = malloc((size_t)length + 1);
	if (buffer == NULL) {
		va_end(arguments);
		return -1;
	}
	if (vsnprintf(buffer, (size_t)length + 1, format, arguments) != length) {
		free(buffer);
		buffer = NULL;
		length = -1;
	}
	va_end(arguments);
	*result = buffer;
	return length;
}

int
cterm_render_casecmp_n(const char *left, const char *right, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		unsigned char a = (unsigned char)left[i];
		unsigned char b = (unsigned char)right[i];

		if (a >= 'A' && a <= 'Z')
			a = (unsigned char)(a + ('a' - 'A'));
		if (b >= 'A' && b <= 'Z')
			b = (unsigned char)(b + ('a' - 'A'));
		if (a != b || a == '\0')
			return (int)a - (int)b;
	}
	return 0;
}

void
cterm_render_set_prestel_reveal(struct cterminal *cterm, bool reveal)
{
	struct st_console_cell *cells;
	struct st_console_rect  rect;
	uint32_t                columns;
	uint32_t                rows;
	size_t                  count;
	int                     status;

	status = st_console_size(cterm->screen.console, &columns, &rows);
	if (status != 0 || columns == 0 || rows == 0 || (size_t)columns > SIZE_MAX / rows
	    || (size_t)columns * rows > SIZE_MAX / sizeof(*cells)) {
		cterm_render_record_error(cterm, status != 0 ? status : IO_ERROR_OUT_OF_RANGE);
		return;
	}
	count = (size_t)columns * rows;
	cells = malloc(count * sizeof(*cells));
	if (cells == NULL) {
		cterm_render_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return;
	}
	rect   = (struct st_console_rect) {0, 0, columns, rows};
	status = st_console_read_cells(cterm->screen.console, &rect, cells, columns);
	for (size_t i = 0; status == 0 && i < count; ++i) {
		if ((cells[i].flags & ST_CONSOLE_CELL_CONCEAL) == 0)
			continue;
		if (reveal)
			cells[i].flags |= ST_CONSOLE_CELL_REVEAL;
		else
			cells[i].flags &= ~ST_CONSOLE_CELL_REVEAL;
	}
	if (status == 0)
		status = st_console_write_complete_cells(cterm->screen.console, &rect, cells, columns);
	free(cells);
	if (render_succeeded(cterm, status))
		cterm->screen.prestel_reveal = reveal;
}
