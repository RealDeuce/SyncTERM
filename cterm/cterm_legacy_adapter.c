/* CTerm-private bridge from the extracted parser to modern components. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define ST_CTERM_ADAPTER_IMPLEMENTATION
#include "cterm_legacy_adapter.h"
#include "cterm.h"

#include "io_error.h"
#include "st_base64.h"
#include "st_utf8.h"

#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void
st_cterm_adapter_record_error(struct cterminal *cterm, int error)
{
	if (cterm != NULL && error != 0 && cterm->screen.error == 0)
		cterm->screen.error = error;
}

int
st_cterm_adapter_take_error(struct cterminal *cterm)
{
	int error;

	if (cterm == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error = cterm->screen.error;
	cterm->screen.error = 0;
	return error;
}

static bool
adapter_succeeded(struct cterminal *cterm, int status)
{
	if (status != 0)
		st_cterm_adapter_record_error(cterm, status);
	return status == 0;
}

void
st_cterm_adapter_attr2palette(struct cterminal *cterm, uint8_t attribute,
	uint32_t *foreground, uint32_t *background)
{
	if (foreground != NULL)
		*foreground = cterm->screen.mode_palette[attribute & 15U];
	if (background != NULL)
		*background = cterm->screen.mode_palette[(attribute >> 4) & 7U];
}

static uint16_t
font_identifier(const struct cterminal *cterm, uint8_t selector)
{
	if (cterm->screen.font_valid[selector])
		return cterm->screen.font_identifiers[selector];
	return cterm->screen.fonts.normal;
}

static uint32_t
cell_flags(const struct cterminal *cterm, const struct vmem_cell *source)
{
	uint32_t flags = ST_CONSOLE_CELL_LEGACY_FOREGROUND
	    | ST_CONSOLE_CELL_LEGACY_BACKGROUND;

	if ((source->bg & CIOLIB_BG_PRESTEL) != 0
	    && (source->legacy_attr & 8U) != 0) {
		flags |= ST_CONSOLE_CELL_CONCEAL;
		if (cterm->screen.prestel_reveal)
			flags |= ST_CONSOLE_CELL_REVEAL;
	}
	else if ((source->legacy_attr & 8U) != 0)
		flags |= ST_CONSOLE_CELL_BRIGHT;
	if ((source->legacy_attr & BLINK) != 0)
		flags |= ST_CONSOLE_CELL_BLINK;
	if ((source->bg & CIOLIB_BG_PIXEL_GRAPHICS) != 0)
		flags |= ST_CONSOLE_CELL_PIXEL_GRAPHICS;
	if ((source->bg & CIOLIB_BG_DOUBLE_HEIGHT) != 0)
		flags |= ST_CONSOLE_CELL_DOUBLE_HEIGHT;
	if ((source->bg & CIOLIB_BG_PRESTEL) != 0)
		flags |= ST_CONSOLE_CELL_SAA5050;
	if ((source->bg & CIOLIB_BG_REVEAL) != 0)
		flags |= ST_CONSOLE_CELL_REVEAL;
	if ((source->bg & CIOLIB_BG_SEPARATED) != 0)
		flags |= ST_CONSOLE_CELL_SEPARATED;
	if ((source->bg & CIOLIB_BG_PRESTEL_TERMINAL) != 0)
		flags |= ST_CONSOLE_CELL_TERMINAL_FONT;
	if ((source->fg & CIOLIB_FG_PRESTEL_CTRL_MASK) != 0)
		flags |= ST_CONSOLE_CELL_SAA5050_CONTROL(
		    source->fg >> CIOLIB_FG_PRESTEL_CTRL_SHIFT);
	return flags;
}

static int
modern_cell(struct cterminal *cterm, const struct vmem_cell *source,
	struct st_console_cell *destination)
{
	uint16_t selected = font_identifier(cterm, source->font);
	uint32_t scalar = st_codepage_to_unicode(cterm->screen.codepage,
	    source->ch, true);
	uint8_t columns;

	*destination = (struct st_console_cell){
		.foreground = source->fg & ~CIOLIB_FG_PRESTEL_CTRL_MASK,
		.background = source->bg & ~(CIOLIB_BG_DOUBLE_HEIGHT
		    | CIOLIB_BG_PRESTEL | CIOLIB_BG_PIXEL_GRAPHICS
		    | CIOLIB_BG_REVEAL | CIOLIB_BG_DIRTY | CIOLIB_BG_SEPARATED
		    | CIOLIB_BG_PRESTEL_TERMINAL),
		.flags = cell_flags(cterm, source),
		.font = selected,
		.glyph_font = selected,
		.hyperlink = source->hyperlink_id,
	};
	return st_console_font_lookup(cterm->screen.console, selected, scalar,
	    &destination->glyph_font, &destination->glyph, &columns);
}

int
st_cterm_adapter_cells_to_modern(struct cterminal *cterm,
	const struct vmem_cell *source, struct st_console_cell *destination,
	size_t count)
{
	if (cterm == NULL || (count != 0
	    && (source == NULL || destination == NULL)))
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < count; ++i) {
		int status = modern_cell(cterm, &source[i], &destination[i]);

		if (status != 0)
			return status;
	}
	return 0;
}

static void
legacy_cell(struct cterminal *cterm, const struct st_console_cell *source,
	struct vmem_cell *destination)
{
	char text[8];
	size_t required = 0;
	uint32_t scalar = UINT32_C(0xfffd);
	size_t consumed;
	uint8_t byte = '?';
	uint32_t foreground = source->foreground & 15U;
	uint32_t background = source->background & 7U;
	static const uint8_t inverse[16] = {
		0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
	};

	memset(destination, 0, sizeof(*destination));
	if (st_console_font_glyph_text(cterm->screen.console, source->glyph_font,
	    source->glyph, text, sizeof(text), &required) == 0 && required != 0)
		(void)st_utf8_decode_one(&scalar, &consumed, text, required);
	(void)st_codepage_from_unicode(&byte, cterm->screen.codepage, scalar);
	destination->ch = byte;
	destination->font = 0;
	for (size_t i = 0; i < UINT8_MAX + 1U; ++i) {
		if (cterm->screen.font_valid[i]
		    && cterm->screen.font_identifiers[i] == source->font) {
			destination->font = (uint8_t)i;
			break;
		}
	}
	if (foreground < 16)
		foreground = inverse[foreground];
	if (background < 16)
		background = inverse[background];
	destination->legacy_attr = (uint8_t)((foreground & 7U)
	    | ((background & 7U) << 4));
	if ((source->flags & ST_CONSOLE_CELL_BRIGHT) != 0)
		destination->legacy_attr |= 8U;
	if ((source->flags & ST_CONSOLE_CELL_CONCEAL) != 0)
		destination->legacy_attr |= 8U;
	if ((source->flags & ST_CONSOLE_CELL_BLINK) != 0)
		destination->legacy_attr |= BLINK;
	destination->fg = source->foreground;
	destination->bg = source->background;
	if ((source->flags & ST_CONSOLE_CELL_DOUBLE_HEIGHT) != 0)
		destination->bg |= CIOLIB_BG_DOUBLE_HEIGHT;
	if ((source->flags & ST_CONSOLE_CELL_SAA5050) != 0)
		destination->bg |= CIOLIB_BG_PRESTEL;
	if ((source->flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0)
		destination->bg |= CIOLIB_BG_PIXEL_GRAPHICS;
	if ((source->flags & ST_CONSOLE_CELL_REVEAL) != 0)
		destination->bg |= CIOLIB_BG_REVEAL;
	if ((source->flags & ST_CONSOLE_CELL_SEPARATED) != 0)
		destination->bg |= CIOLIB_BG_SEPARATED;
	if ((source->flags & ST_CONSOLE_CELL_TERMINAL_FONT) != 0)
		destination->bg |= CIOLIB_BG_PRESTEL_TERMINAL;
	destination->fg |= ((source->flags
	    & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK)
	    >> ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT)
	    << CIOLIB_FG_PRESTEL_CTRL_SHIFT;
	destination->hyperlink_id = source->hyperlink;
}

void
st_cterm_adapter_gettextinfo(struct cterminal *cterm, struct text_info *info)
{
	struct st_console_rect window = {0};
	uint32_t columns = 0;
	uint32_t rows = 0;
	uint32_t x = 0;
	uint32_t y = 0;
	int status;

	status = st_console_size(cterm->screen.console, &columns, &rows);
	if (status == 0)
		status = st_console_get_window(cterm->screen.console, &window);
	if (status == 0)
		status = st_console_get_position(cterm->screen.console, &x, &y);
	if (!adapter_succeeded(cterm, status)) {
		*info = (struct text_info){0};
		return;
	}
	if (columns > UINT8_MAX || rows > UINT8_MAX
	    || window.x >= UINT8_MAX || window.y >= UINT8_MAX
	    || window.width > UINT8_MAX - window.x
	    || window.height > UINT8_MAX - window.y
	    || x < window.x || y < window.y
	    || x - window.x >= UINT8_MAX || y - window.y >= UINT8_MAX) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_RANGE);
		*info = (struct text_info){0};
		return;
	}
	*info = (struct text_info){
		.winleft = (uint8_t)(window.x + 1),
		.wintop = (uint8_t)(window.y + 1),
		.winright = (uint8_t)(window.x + window.width),
		.winbottom = (uint8_t)(window.y + window.height),
		.attribute = cterm->attr,
		.normattr = (uint8_t)cterm->screen.video_params[0].default_attr,
		.currmode = (uint8_t)cterm->screen.video_params[0].mode,
		.screenheight = (uint8_t)rows,
		.screenwidth = (uint8_t)columns,
		.curx = (uint8_t)(x - window.x + 1),
		.cury = (uint8_t)(y - window.y + 1),
	};
}

int
st_cterm_adapter_getvideoflags(struct cterminal *cterm)
{
	uint32_t flags = 0;
	(void)adapter_succeeded(cterm,
	    st_console_get_video_flags(cterm->screen.console, &flags));
	return (int)flags;
}

void
st_cterm_adapter_setvideoflags(struct cterminal *cterm, int flags)
{
	(void)adapter_succeeded(cterm, st_console_set_video_flags(
	    cterm->screen.console, (uint32_t)flags & ST_CONSOLE_VIDEO_FLAGS));
}

void
st_cterm_adapter_window(struct cterminal *cterm, int left, int top,
	int right, int bottom)
{
	struct st_console_rect window;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return;
	window = (struct st_console_rect){
		.x = (uint32_t)(left - 1),
		.y = (uint32_t)(top - 1),
		.width = (uint32_t)(right - left + 1),
		.height = (uint32_t)(bottom - top + 1),
	};
	(void)adapter_succeeded(cterm,
	    st_console_set_window(cterm->screen.console, &window));
}

void
st_cterm_adapter_gotoxy(struct cterminal *cterm, int x, int y)
{
	struct st_console_rect window;

	if (x < 1 || y < 1)
		return;
	if (!adapter_succeeded(cterm,
	    st_console_get_window(cterm->screen.console, &window)))
		return;
	(void)adapter_succeeded(cterm, st_console_set_position(
	    cterm->screen.console, window.x + (uint32_t)x - 1,
	    window.y + (uint32_t)y - 1));
}

int
st_cterm_adapter_wherex(struct cterminal *cterm)
{
	struct st_console_rect window;
	uint32_t x, y;

	if (st_console_get_window(cterm->screen.console, &window) != 0
	    || st_console_get_position(cterm->screen.console, &x, &y) != 0) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_IO);
		return 1;
	}
	return (int)(x - window.x + 1);
}

int
st_cterm_adapter_wherey(struct cterminal *cterm)
{
	struct st_console_rect window;
	uint32_t x, y;

	if (st_console_get_window(cterm->screen.console, &window) != 0
	    || st_console_get_position(cterm->screen.console, &x, &y) != 0) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_IO);
		return 1;
	}
	return (int)(y - window.y + 1);
}

void
st_cterm_adapter_textattr(struct cterminal *cterm, int attribute)
{
	struct st_console_style style;

	if (!adapter_succeeded(cterm,
	    st_console_get_style(cterm->screen.console, &style)))
		return;
	cterm->attr = (uint8_t)attribute;
	st_cterm_adapter_attr2palette(cterm, cterm->attr, &style.foreground,
	    &style.background);
	style.flags = ST_CONSOLE_CELL_LEGACY_FOREGROUND
	    | ST_CONSOLE_CELL_LEGACY_BACKGROUND;
	if ((cterm->attr & 8U) != 0)
		style.flags |= ST_CONSOLE_CELL_BRIGHT;
	if ((cterm->attr & BLINK) != 0)
		style.flags |= ST_CONSOLE_CELL_BLINK;
	style.font = font_identifier(cterm,
	    st_cterm_adapter_attrfont(cterm, cterm->attr));
	style.hyperlink = cterm->screen.current_hyperlink;
	(void)adapter_succeeded(cterm,
	    st_console_set_style(cterm->screen.console, &style));
}

void
st_cterm_adapter_setcolour(struct cterminal *cterm, uint32_t foreground,
	uint32_t background)
{
	struct st_console_style style;

	if (!adapter_succeeded(cterm,
	    st_console_get_style(cterm->screen.console, &style)))
		return;
	style.foreground = foreground;
	style.background = background;
	(void)adapter_succeeded(cterm,
	    st_console_set_style(cterm->screen.console, &style));
}

void
st_cterm_adapter_cursor(struct cterminal *cterm, int cursor)
{
	(void)adapter_succeeded(cterm, st_console_set_cursor(cterm->screen.console,
	    cursor != _NOCURSOR, ST_CONSOLE_CURSOR_BLOCK));
}

void
st_cterm_adapter_getcustomcursor(struct cterminal *cterm, int *start,
	int *end, int *range, int *blink, int *visible)
{
	struct st_console_custom_cursor cursor;

	if (!adapter_succeeded(cterm,
	    st_console_get_custom_cursor(cterm->screen.console, &cursor)))
		cursor = (struct st_console_custom_cursor){0};
	*start = (int)cursor.start;
	*end = (int)cursor.end;
	*range = (int)cursor.range;
	*blink = cursor.blink;
	*visible = cursor.visible;
}

void
st_cterm_adapter_setcustomcursor(struct cterminal *cterm, int start,
	int end, int range, int blink, int visible)
{
	struct st_console_custom_cursor cursor;

	if (start < 0 || end < 0 || range < 0)
		return;
	cursor = (struct st_console_custom_cursor){
		.start = (uint32_t)start,
		.end = (uint32_t)end,
		.range = (uint32_t)range,
		.blink = blink != 0,
		.visible = visible != 0,
	};
	(void)adapter_succeeded(cterm,
	    st_console_set_custom_cursor(cterm->screen.console, &cursor));
}

int
st_cterm_adapter_putch(struct cterminal *cterm, int value)
{
	struct st_console_rect window;
	struct st_console_rect destination;
	struct st_console_cell cell;
	struct vmem_cell source;
	uint32_t x;
	uint32_t y;
	uint32_t right;
	uint32_t bottom;
	int status;

	if (value == 7) {
		audio_engine_t *engine = NULL;
		if (cterm->screen.audio != NULL
		    && st_audio_runtime_acquire(cterm->screen.audio, &engine) == 0)
			(void)audio_beep(engine, 800.0, 100);
		return value;
	}
	status = st_console_get_window(cterm->screen.console, &window);
	if (status == 0)
		status = st_console_get_position(cterm->screen.console, &x, &y);
	if (!adapter_succeeded(cterm, status))
		return -1;
	right = window.x + window.width;
	bottom = window.y + window.height;
	switch ((uint8_t)value) {
	case '\r':
		status = st_console_set_position(cterm->screen.console, window.x, y);
		break;
	case '\n':
		if (y + 1 < bottom)
			status = st_console_set_position(cterm->screen.console, x, y + 1);
		else if (cterm->screen.wscroll != 0)
			status = st_console_scroll(cterm->screen.console, 1, ' ');
		else
			status = 0;
		break;
	case '\b':
		if (x == window.x)
			return value;
		--x;
		status = st_console_set_position(cterm->screen.console, x, y);
		if (status != 0)
			break;
		source = (struct vmem_cell){
			.ch = ' ', .legacy_attr = cterm->attr,
			.fg = cterm->fg_color, .bg = cterm->bg_color,
			.font = st_cterm_adapter_attrfont(cterm, cterm->attr),
			.hyperlink_id = cterm->screen.current_hyperlink,
		};
		status = modern_cell(cterm, &source, &cell);
		if (status == 0) {
			destination = (struct st_console_rect){x, y, 1, 1};
			status = st_console_write_complete_cells(cterm->screen.console,
			    &destination, &cell, 1);
		}
		break;
	case '\t': {
		uint32_t spaces = 8 - ((x - window.x) % 8);

		status = 0;
		while (spaces-- != 0 && status == 0)
			status = st_cterm_adapter_putch(cterm, ' ') < 0
			    ? IO_ERROR_IO : 0;
		break;
	}
	default:
		source = (struct vmem_cell){
			.ch = (uint8_t)value, .legacy_attr = cterm->attr,
			.fg = cterm->fg_color, .bg = cterm->bg_color,
			.font = st_cterm_adapter_attrfont(cterm, cterm->attr),
			.hyperlink_id = cterm->screen.current_hyperlink,
		};
		status = modern_cell(cterm, &source, &cell);
		if (status == 0) {
			destination = (struct st_console_rect){x, y, 1, 1};
			status = st_console_write_complete_cells(cterm->screen.console,
			    &destination, &cell, 1);
		}
		if (status != 0)
			break;
		if (x + 1 < right)
			status = st_console_set_position(cterm->screen.console, x + 1, y);
		else if (y + 1 < bottom)
			status = st_console_set_position(cterm->screen.console,
			    window.x, y + 1);
		else {
			if (cterm->screen.wscroll != 0)
				status = st_console_scroll(cterm->screen.console, 1, ' ');
			if (status == 0)
				status = st_console_set_position(cterm->screen.console,
				    window.x, bottom - 1);
		}
		break;
	}
	return adapter_succeeded(cterm, status) ? value : -1;
}

int
st_cterm_adapter_cputs(struct cterminal *cterm, const char *text)
{
	while (*text != '\0') {
		if (st_cterm_adapter_putch(cterm, (uint8_t)*text++) < 0)
			return -1;
	}
	return 0;
}

void
st_cterm_adapter_clrscr(struct cterminal *cterm)
{
	(void)adapter_succeeded(cterm,
	    st_console_clear_window(cterm->screen.console, ' '));
}

void
st_cterm_adapter_clreol(struct cterminal *cterm)
{
	(void)adapter_succeeded(cterm,
	    st_console_clear_line(cterm->screen.console, ' '));
}

int
st_cterm_adapter_puttext(struct cterminal *cterm, int left, int top,
	int right, int bottom, const void *bytes)
{
	const uint8_t *input = bytes;
	size_t count;
	struct vmem_cell *cells;
	int status;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return 0;
	count = (size_t)(right - left + 1) * (size_t)(bottom - top + 1);
	cells = calloc(count, sizeof(*cells));
	if (cells == NULL) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return 0;
	}
	for (size_t i = 0; i < count; ++i) {
		cells[i].ch = input[i * 2];
		cells[i].legacy_attr = input[i * 2 + 1];
		cells[i].font = st_cterm_adapter_attrfont(cterm,
		    cells[i].legacy_attr);
		st_cterm_adapter_attr2palette(cterm, cells[i].legacy_attr,
		    &cells[i].fg, &cells[i].bg);
		cells[i].hyperlink_id = cterm->screen.current_hyperlink;
	}
	status = st_cterm_adapter_vmem_puttext(cterm, left, top, right, bottom,
	    cells);
	free(cells);
	return status;
}

int
st_cterm_adapter_vmem_puttext(struct cterminal *cterm, int left, int top,
	int right, int bottom, const struct vmem_cell *cells)
{
	struct st_console_rect rect;
	struct st_console_cell *modern;
	size_t count;
	int status = 0;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return 0;
	rect = (struct st_console_rect){(uint32_t)(left - 1),
	    (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	count = (size_t)rect.width * rect.height;
	modern = calloc(count, sizeof(*modern));
	if (modern == NULL) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return 0;
	}
	for (size_t i = 0; i < count; ++i) {
		status = modern_cell(cterm, &cells[i], &modern[i]);
		if (status != 0)
			break;
	}
	if (status == 0)
		status = st_console_write_complete_cells(cterm->screen.console,
		    &rect, modern, rect.width);
	free(modern);
	if (status != 0)
		st_cterm_adapter_record_error(cterm, status);
	return status == 0;
}

int
st_cterm_adapter_vmem_gettext(struct cterminal *cterm, int left, int top,
	int right, int bottom, struct vmem_cell *cells)
{
	struct st_console_rect rect;
	struct st_console_cell *modern;
	size_t count;
	int status;

	if (left < 1 || top < 1 || right < left || bottom < top)
		return 0;
	rect = (struct st_console_rect){(uint32_t)(left - 1),
	    (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	count = (size_t)rect.width * rect.height;
	modern = malloc(count * sizeof(*modern));
	if (modern == NULL) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return 0;
	}
	status = st_console_read_cells(cterm->screen.console, &rect, modern,
	    rect.width);
	if (status == 0) {
		for (size_t i = 0; i < count; ++i)
			legacy_cell(cterm, &modern[i], &cells[i]);
	}
	free(modern);
	if (status != 0)
		st_cterm_adapter_record_error(cterm, status);
	return status == 0;
}

int
st_cterm_adapter_movetext(struct cterminal *cterm, int left, int top,
	int right, int bottom, int destination_left, int destination_top)
{
	struct st_console_rect rect;

	if (left < 1 || top < 1 || right < left || bottom < top
	    || destination_left < 1 || destination_top < 1)
		return 0;
	rect = (struct st_console_rect){(uint32_t)(left - 1),
	    (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	return adapter_succeeded(cterm, st_console_move_cells(
	    cterm->screen.console, &rect, (uint32_t)(destination_left - 1),
	    (uint32_t)(destination_top - 1)));
}

int
st_cterm_adapter_movetext_clear(struct cterminal *cterm, int left, int top,
	int right, int bottom, int destination_left, int destination_top,
	const struct vmem_cell *fill)
{
	struct st_console_rect rect;
	struct st_console_cell modern;

	if (left < 1 || top < 1 || right < left || bottom < top
	    || destination_left < 1 || destination_top < 1)
		return 0;
	if (!adapter_succeeded(cterm, modern_cell(cterm, fill, &modern)))
		return 0;
	rect = (struct st_console_rect){(uint32_t)(left - 1),
	    (uint32_t)(top - 1), (uint32_t)(right - left + 1),
	    (uint32_t)(bottom - top + 1)};
	return adapter_succeeded(cterm, st_console_move_cells_clear(
	    cterm->screen.console, &rect,
	    (uint32_t)(destination_left - 1), (uint32_t)(destination_top - 1),
	    &modern));
}

struct ciolib_pixels *
st_cterm_adapter_getpixels(struct cterminal *cterm, uint32_t left,
	uint32_t top, uint32_t right, uint32_t bottom, int force)
{
	struct st_console_rect rect;
	struct st_pixel_buffer *modern = NULL;
	struct ciolib_pixels *result;

	if (right < left || bottom < top)
		return NULL;
	rect = (struct st_console_rect){left, top, right - left + 1,
	    bottom - top + 1};
	int status = st_console_get_pixels(&modern, cterm->screen.console, &rect,
	    force != 0);
	if (!adapter_succeeded(cterm, status))
		return NULL;
	result = malloc(sizeof(*result));
	if (result == NULL) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		st_pixel_buffer_destroy(modern);
		return NULL;
	}
	result->pixels = modern->primary;
	result->pixelsb = modern->alternate;
	result->width = modern->width;
	result->height = modern->height;
	free(modern);
	return result;
}

void
st_cterm_adapter_freepixels(struct ciolib_pixels *pixels)
{
	if (pixels == NULL)
		return;
	free(pixels->pixels);
	free(pixels->pixelsb);
	free(pixels);
}

int
st_cterm_adapter_setpixels(struct cterminal *cterm, uint32_t left,
	uint32_t top, uint32_t right, uint32_t bottom, uint32_t x_offset,
	uint32_t y_offset, uint32_t mask_x, uint32_t mask_y,
	struct ciolib_pixels *pixels, struct ciolib_mask *mask)
{
	struct st_pixel_buffer source;
	struct st_pixel_mask modern_mask;
	struct st_pixel_blit blit;
	const struct st_pixel_mask *mask_pointer = NULL;

	if (pixels == NULL || pixels->pixels == NULL || right < left
	    || bottom < top) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	source = (struct st_pixel_buffer){pixels->pixels, pixels->pixelsb,
	    pixels->width, pixels->height};
	blit = (struct st_pixel_blit){
		.source_x = x_offset,
		.source_y = y_offset,
		.width = right - left + 1,
		.height = bottom - top + 1,
		.destination_x = (int32_t)left,
		.destination_y = (int32_t)top,
		.scale_x = 1,
		.scale_y = 1,
		.mask_x = mask_x,
		.mask_y = mask_y,
	};
	if (mask != NULL) {
		modern_mask = (struct st_pixel_mask){mask->bits, mask->width,
		    mask->height};
		mask_pointer = &modern_mask;
	}
	return adapter_succeeded(cterm, st_console_blit_pixels(
	    cterm->screen.console, &source, mask_pointer, &blit));
}

int
st_cterm_adapter_setpalette(struct cterminal *cterm, uint32_t entry,
	uint16_t red, uint16_t green, uint16_t blue)
{
	uint32_t colour;

	if (entry > UINT16_MAX)
		return 0;
	colour = ((uint32_t)(red >> 8) << 16)
	    | ((uint32_t)(green >> 8) << 8) | (blue >> 8);
	return adapter_succeeded(cterm, st_console_set_palette(
	    cterm->screen.console, (uint16_t)entry, &colour, 1));
}

int
st_cterm_adapter_getpalette(struct cterminal *cterm, uint32_t entry,
	uint8_t *red, uint8_t *green, uint8_t *blue)
{
	uint32_t colour;

	if (entry > UINT16_MAX)
		return 0;
	if (!adapter_succeeded(cterm, st_console_get_palette(
	    cterm->screen.console, (uint16_t)entry, &colour, 1)))
		return 0;
	*red = (uint8_t)(colour >> 16);
	*green = (uint8_t)(colour >> 8);
	*blue = (uint8_t)colour;
	return 1;
}

uint32_t
st_cterm_adapter_map_rgb(uint16_t red, uint16_t green, uint16_t blue)
{
	return ST_CONSOLE_COLOR_RGB_VALUE(red >> 8, green >> 8, blue >> 8);
}

void
st_cterm_adapter_replace_font(struct cterminal *cterm, uint8_t selector,
	const char *name, const void *data, size_t size)
{
	uint32_t width;
	uint32_t height;
	uint16_t identifier;
	struct st_console_font font;

	(void)name;
	if (size == 0 || size % 256 != 0)
		return;
	if (!adapter_succeeded(cterm, st_console_cell_pixel_size(
	    cterm->screen.console, &width, &height)))
		return;
	font = (struct st_console_font){
		.bitmap = data,
		.bitmap_size = size,
		.width = width,
		.height = height,
		.glyph_count = 256,
		.row_stride = ((size_t)width + 7) / 8,
		.glyph_stride = size / 256,
	};
	if (font.row_stride * height != font.glyph_stride)
		return;
	if (!adapter_succeeded(cterm, st_console_register_font_copy(
	    cterm->screen.console, &font, &identifier)))
		return;
	cterm->screen.font_identifiers[selector] = identifier;
	cterm->screen.font_valid[selector] = true;
}

int
st_cterm_adapter_get_modepalette(struct cterminal *cterm, uint32_t *palette)
{
	for (uint32_t i = 0; i < 16; ++i)
		palette[i] = cterm->screen.mode_palette[i];
	return 1;
}

int
st_cterm_adapter_set_modepalette(struct cterminal *cterm,
	const uint32_t *palette)
{
	if (palette == NULL)
		return 0;
	for (size_t i = 0; i < 16; ++i) {
		if (palette[i] > UINT16_MAX)
			return 0;
	}
	memcpy(cterm->screen.mode_palette, palette,
	    sizeof(cterm->screen.mode_palette));
	return 1;
}

int
st_cterm_adapter_find_vmode(struct cterminal *cterm, int mode)
{
	struct video_params *parameters = &cterm->screen.video_params[0];
	uint32_t columns;
	uint32_t rows;
	uint32_t width;
	uint32_t height;
	uint32_t aspect_width;
	uint32_t aspect_height;
	int status;

	if (mode != parameters->mode)
		return -1;
	status = st_console_size(cterm->screen.console, &columns, &rows);
	if (status == 0)
		status = st_console_cell_pixel_size(cterm->screen.console, &width,
		    &height);
	if (status == 0)
		status = st_console_get_aspect_ratio(cterm->screen.console,
		    &aspect_width, &aspect_height);
	if (!adapter_succeeded(cterm, status))
		return -1;
	if (width == 0 || height == 0 || columns > UINT8_MAX
	    || rows > UINT8_MAX || width > INT_MAX
	    || height > INT_MAX || aspect_width > INT_MAX
	    || aspect_height > INT_MAX || columns > INT_MAX / width
	    || rows > INT_MAX / height) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_RANGE);
		return -1;
	}
	parameters->cols = (int)columns;
	parameters->rows = (int)rows;
	parameters->charwidth = (int)width;
	parameters->charheight = (int)height;
	parameters->aspect_width = (int)aspect_width;
	parameters->aspect_height = (int)aspect_height;
	parameters->xres = (int)(columns * width);
	parameters->yres = (int)(rows * height);
	return 0;
}

int
st_cterm_adapter_set_hold_update(struct cterminal *cterm, int hold)
{
	int normalized = hold != 0;
	int status = 0;

	if (normalized == (cterm->screen.hold_update != 0))
		return cterm->screen.hold_update;
	if (normalized)
		status = st_console_begin_update(cterm->screen.console);
	else
		status = st_console_end_update(cterm->screen.console);
	if (adapter_succeeded(cterm, status))
		cterm->screen.hold_update = normalized;
	return cterm->screen.hold_update;
}

int
st_cterm_adapter_hold_update(const struct cterminal *cterm)
{
	return cterm->screen.hold_update;
}

int
st_cterm_adapter_default_palette(uint32_t entry, uint8_t *red,
	uint8_t *green, uint8_t *blue)
{
	uint32_t colour;

	if (entry >= TOTAL_DAC_SIZE || red == NULL || green == NULL || blue == NULL
	    || st_console_get_default_palette((uint16_t)entry, &colour, 1) != 0)
		return 0;
	*red = (uint8_t)(colour >> 16);
	*green = (uint8_t)(colour >> 8);
	*blue = (uint8_t)colour;
	return 1;
}

int
st_cterm_adapter_getfont(struct cterminal *cterm, int slot)
{
	if (slot < 0 || slot >= 5)
		return 0;
	return cterm->screen.selectors[slot];
}

int
st_cterm_adapter_setfont(struct cterminal *cterm, int selector, int force,
	int slot)
{
	(void)force;
	if (selector < 0 || selector > UINT8_MAX || slot < 0 || slot >= 5
	    || !cterm->screen.font_valid[selector])
		return -1;
	cterm->screen.selectors[slot] = (uint16_t)selector;
	return selector;
}

uint8_t
st_cterm_adapter_attrfont(struct cterminal *cterm, uint8_t attribute)
{
	int slot = 1;
	uint32_t flags = (uint32_t)st_cterm_adapter_getvideoflags(cterm);

	if ((attribute & 8U) != 0 && (flags & CIOLIB_VIDEO_ALTCHARS) != 0)
		slot += 1;
	if ((attribute & BLINK) != 0
	    && (flags & CIOLIB_VIDEO_BLINKALTCHARS) != 0)
		slot += 2;
	return (uint8_t)cterm->screen.selectors[slot];
}

uint16_t
st_cterm_adapter_add_hyperlink(struct cterminal *cterm, const char *uri,
	const char *parameter)
{
	uint16_t identifier = 0;
	int status;

	if (cterm->screen.hyperlinks != NULL && uri != NULL) {
		status = st_hyperlink_registry_add(cterm->screen.hyperlinks, uri,
		    strlen(uri), parameter, parameter == NULL ? 0 : strlen(parameter),
		    &identifier);
		st_cterm_adapter_record_error(cterm, status);
	}
	return identifier;
}

static char *
hyperlink_part(struct cterminal *cterm, uint16_t identifier, bool parameter)
{
	struct st_hyperlink link;
	char *result;

	if (cterm->screen.hyperlinks == NULL
	    || st_hyperlink_registry_copy(cterm->screen.hyperlinks, identifier,
	    &link) != 0)
		return NULL;
	result = parameter ? link.parameter : link.uri;
	if (parameter)
		free(link.uri);
	else
		free(link.parameter);
	return result;
}

char *
st_cterm_adapter_hyperlink_url(struct cterminal *cterm, uint16_t identifier)
{
	return hyperlink_part(cterm, identifier, false);
}

char *
st_cterm_adapter_hyperlink_params(struct cterminal *cterm,
	uint16_t identifier)
{
	return hyperlink_part(cterm, identifier, true);
}

void
st_cterm_adapter_set_hyperlink(struct cterminal *cterm, uint16_t identifier)
{
	struct st_console_style style;

	cterm->screen.current_hyperlink = identifier;
	if (adapter_succeeded(cterm,
	    st_console_get_style(cterm->screen.console, &style))) {
		style.hyperlink = identifier;
		(void)adapter_succeeded(cterm,
		    st_console_set_style(cterm->screen.console, &style));
	}
}

uint16_t
st_cterm_adapter_get_hyperlink(struct cterminal *cterm)
{
	return cterm->screen.current_hyperlink;
}

uint64_t
st_cterm_adapter_timer64(void)
{
	struct timespec now;

	if (timespec_get(&now, TIME_UTC) != TIME_UTC)
		return 0;
	return (uint64_t)now.tv_sec * UINT64_C(1000)
	    + (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

uint16_t
st_cterm_adapter_crc16(const void *data, size_t size, uint16_t initial)
{
	const uint8_t *bytes = data;
	uint16_t crc = initial;

	for (size_t i = 0; i < size; ++i) {
		crc ^= (uint16_t)bytes[i] << 8;
		for (unsigned bit = 0; bit < 8; ++bit)
			crc = (crc & UINT16_C(0x8000)) != 0
			    ? (uint16_t)((crc << 1) ^ UINT16_C(0x1021))
			    : (uint16_t)(crc << 1);
	}
	return crc;
}

int
st_cterm_adapter_base64_decode(void *destination, size_t capacity,
	const char *source)
{
	size_t size;

	if (!st_base64_decoded_size(source, strlen(source), &size)
	    || size > capacity || !st_base64_decode(destination, capacity,
	    source, strlen(source)))
		return -1;
	return size > INT32_MAX ? -1 : (int)size;
}

int
st_cterm_adapter_asprintf(char **result, const char *format, ...)
{
	va_list arguments;
	va_list copy;
	int length;
	char *buffer;

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
st_cterm_adapter_strnicmp(const char *left, const char *right, size_t count)
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

struct ciolib_screen *
st_cterm_adapter_savescreen(struct cterminal *cterm)
{
	struct ciolib_screen *screen;
	uint32_t columns, rows;

	if (!adapter_succeeded(cterm,
	    st_console_size(cterm->screen.console, &columns, &rows)))
		return NULL;
	screen = calloc(1, sizeof(*screen));
	if (screen == NULL) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		return NULL;
	}
	screen->vmem = calloc((size_t)columns * rows, sizeof(*screen->vmem));
	if (screen->vmem == NULL) {
		st_cterm_adapter_record_error(cterm, IO_ERROR_OUT_OF_MEMORY);
		free(screen);
		return NULL;
	}
	st_cterm_adapter_gettextinfo(cterm, &screen->text_info);
	if (!st_cterm_adapter_vmem_gettext(cterm, 1, 1, (int)columns,
	    (int)rows, screen->vmem)) {
		st_cterm_adapter_freescreen(screen);
		return NULL;
	}
	return screen;
}

void
st_cterm_adapter_freescreen(struct ciolib_screen *screen)
{
	if (screen != NULL) {
		free(screen->vmem);
		free(screen);
	}
}
