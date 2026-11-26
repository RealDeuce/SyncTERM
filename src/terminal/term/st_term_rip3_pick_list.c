/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_builtin_fonts.h"
#include "st_rip3_geometry.h"
#include "st_rip3_query.h"
#include "st_term_rip3_pick_list.h"
#include "st_unicode_compare.h"
#include "st_utf8.h"

enum {
	CELL_WIDTH  = 8,
	CELL_HEIGHT = 16,
	MAX_VISIBLE = 18
};

struct st_term_rip3_pick_list {
	const struct st_rip3_pick_list *list;
	st_console_t                   *console, *view;
	struct st_pixel_buffer         *saved;
	struct st_console_rect          area;
	struct st_console_pixel_cursor  cursor;
	struct st_console_cell         *cells;
	uint32_t                        columns, rows, heading, visible;
	uint16_t                        font;
	size_t                          first, selected, pressed;
	bool                            cursor_hidden, adopted;
};

static uint32_t
axis_offset(uint32_t available, uint32_t extent, uint32_t device, uint32_t world, bool has_position,
    uint16_t value)
{
	uint32_t limit = available > extent ? available - extent : 0;
	if (!has_position || world == 0)
		return limit / 2;
	uint64_t pixel = (uint64_t)value * device / world;
	return pixel > limit ? limit : (uint32_t)pixel;
}

static size_t
next_scalar(const char *bytes, size_t length, uint32_t *scalar)
{
	size_t consumed;
	if (st_utf8_decode_one(scalar, &consumed, bytes, length) == 0)
		return consumed;
	*scalar = 0xfffd;
	return 1;
}

static uint8_t
glyph(const struct st_term_rip3_pick_list *dialog, struct st_console_cell *cell, uint32_t scalar)
{
	uint8_t columns;
	cell->font = dialog->font;
	if (st_console_font_lookup(dialog->view, cell->font, scalar, &cell->glyph_font, &cell->glyph, &columns)
	    == 0)
		return columns;
	(void)st_console_font_lookup_fallback(dialog->view, cell->font, 1, &cell->glyph_font, &cell->glyph);
	return 1;
}

static uint32_t
text_width(const char *text)
{
	size_t   length = strlen(text);
	uint32_t width  = 0;
	for (size_t at = 0; at < length;) {
		uint32_t scalar;
		at += next_scalar(text + at, length - at, &scalar);
		++width;
	}
	return width;
}

static void
draw_row(struct st_term_rip3_pick_list *dialog, uint32_t row, const struct st_rip3_pick_list_text *text,
    uint32_t background, char indicator)
{
	uint32_t                pad   = dialog->columns > 2 ? 1 : 0;
	uint32_t                end   = dialog->columns - pad;
	struct st_console_cell *cells = dialog->cells + (size_t)row * dialog->columns;
	struct st_console_cell  blank = {.foreground = ST_CONSOLE_COLOR_RGB_VALUE(241, 245, 249),
	    .background                              = background};
	(void)glyph(dialog, &blank, ' ');
	for (uint32_t i = 0; i < dialog->columns; ++i)
		cells[i] = blank;
	if (indicator != 0 && pad != 0)
		(void)glyph(dialog, &cells[end], (uint32_t)indicator);
	size_t length = strlen(text->bytes);
	for (size_t at = 0, character = 0; at < length && pad < end; ++character) {
		uint32_t scalar;
		at += next_scalar(text->bytes + at, length - at, &scalar);
		struct st_console_cell cell = blank;
		if ((text->highlighted & (UINT64_C(1) << character)) != 0)
			cell.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 220, 121);
		uint8_t columns = glyph(dialog, &cell, scalar);
		if (columns > end - pad)
			break;
		cells[pad++] = cell;
		if (columns == 2) {
			cell.flags |= ST_CONSOLE_CELL_CONTINUATION;
			cells[pad++] = cell;
		}
	}
}

static int
draw(struct st_term_rip3_pick_list *dialog)
{
	if (dialog->heading != 0)
		draw_row(dialog, 0, &dialog->list->question, ST_CONSOLE_COLOR_RGB_VALUE(41, 57, 91), 0);
	for (uint32_t row = 0; row < dialog->visible; ++row) {
		size_t   index     = dialog->first + row;
		uint32_t color     = index == dialog->selected ? ST_CONSOLE_COLOR_RGB_VALUE(35, 84, 161)
		                                               : ST_CONSOLE_COLOR_RGB_VALUE(25, 34, 53);
		char     indicator = row == 0 && index != 0                                          ? '^'
		                     : row == dialog->visible - 1 && index + 1 < dialog->list->count ? 'v'
		                                                                                     : 0;
		draw_row(dialog, dialog->heading + row, &dialog->list->labels[index], color, indicator);
	}
	const struct st_console_rect grid = {0, 0, dialog->columns, dialog->rows};
	return st_console_write_complete_cells(dialog->view, &grid, dialog->cells, dialog->columns);
}

int
st_term_rip3_pick_list_close(struct st_term_rip3_pick_list *dialog)
{
	if (dialog == NULL)
		return 0;
	int status = !dialog->adopted ? 0 : st_console_raster_view_restore_transient(dialog->view, dialog->saved);
	if (dialog->view != NULL)
		st_console_destroy(dialog->view);
	if (dialog->cursor_hidden) {
		int restored = st_console_set_pixel_cursor(dialog->console, &dialog->cursor);
		if (status == 0)
			status = restored;
	}
	st_pixel_buffer_destroy(dialog->saved);
	free(dialog->cells);
	free(dialog);
	return status;
}

int
st_term_rip3_pick_list_open(struct st_term_rip3_pick_list **result, st_console_t *console,
    const struct st_rip3_pick_list *list, const struct st_rip3_frame *frame)
{
	uint32_t width, height;
	int      status = st_console_pixel_size(console, &width, &height);
	if (status != 0)
		return status;
	uint32_t available_rows = height / CELL_HEIGHT, available_columns = width / CELL_WIDTH;
	if (available_rows == 0 || available_columns == 0)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_term_rip3_pick_list *dialog = calloc(1, sizeof(*dialog));
	if (dialog == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	dialog->list    = list;
	dialog->console = console;
	dialog->pressed = SIZE_MAX;
	dialog->heading = available_rows > 1 && list->question.bytes[0] != '\0';
	dialog->visible = available_rows - dialog->heading;
	if (dialog->visible > MAX_VISIBLE)
		dialog->visible = MAX_VISIBLE;
	if (dialog->visible > list->count)
		dialog->visible = (uint32_t)list->count;
	dialog->rows = dialog->heading + dialog->visible;

	/* The UI's bundled 8x16 font measures one cell per displayed scalar. */
	uint32_t columns = text_width(list->question.bytes);
	for (size_t i = 0; i < list->count; ++i) {
		uint32_t label = text_width(list->labels[i].bytes);
		if (label > columns)
			columns = label;
	}
	columns += 2;
	dialog->columns     = columns < available_columns ? columns : available_columns;
	dialog->area.width  = dialog->columns * CELL_WIDTH;
	dialog->area.height = dialog->rows * CELL_HEIGHT;
	dialog->area.x =
	    axis_offset(width, dialog->area.width, frame->device_width, frame->world_width, list->has_x, list->x);
	dialog->area.y = axis_offset(height, dialog->area.height, frame->device_height, frame->world_height,
	    list->has_y, list->y);
	status = st_console_create_raster_view(&dialog->view, console, dialog->columns, dialog->rows, CELL_WIDTH,
	    CELL_HEIGHT, dialog->area.x, dialog->area.y);
	if (status == 0) {
		struct st_console_font font;
		status = st_builtin_font_get(0, CELL_WIDTH, CELL_HEIGHT, &font)
		             ? st_console_register_static_font(dialog->view, &font, &dialog->font)
		             : IO_ERROR_NOT_SUPPORTED;
	}
	if (status == 0) {
		dialog->cells = malloc((size_t)dialog->columns * dialog->rows * sizeof(*dialog->cells));
		if (dialog->cells == NULL)
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status == 0)
		status = st_console_get_pixels(&dialog->saved, console, &dialog->area, false);
	if (status == 0)
		status = st_console_get_pixel_cursor(console, &dialog->cursor);
	if (status == 0) {
		const struct st_console_pixel_cursor hidden = {.enabled = true};
		dialog->cursor_hidden                       = true;
		status                                      = st_console_set_pixel_cursor(console, &hidden);
	}
	if (status == 0)
		status = st_console_raster_view_adopt_transient(dialog->view);
	if (status == 0) {
		dialog->adopted = true;
		status          = draw(dialog);
	}
	if (status != 0) {
		(void)st_term_rip3_pick_list_close(dialog);
		return status;
	}
	*result = dialog;
	return 0;
}

static int
move_to(struct st_term_rip3_pick_list *dialog, int64_t index)
{
	size_t selected = index < 0                                ? 0
	                  : (uint64_t)index >= dialog->list->count ? dialog->list->count - 1
	                                                           : (size_t)index;
	if (selected == dialog->selected)
		return 0;
	dialog->selected = selected;
	if (selected < dialog->first)
		dialog->first = selected;
	else if (selected >= dialog->first + dialog->visible)
		dialog->first = selected - dialog->visible + 1;
	return draw(dialog);
}

static size_t
hotkey(const struct st_rip3_pick_list *list, uint32_t scalar)
{
	char      key[4];
	ptrdiff_t key_length = st_utf8_encode_one(key, sizeof(key), scalar);
	if (key_length <= 0)
		return SIZE_MAX;
	for (size_t index = 0; index < list->count; ++index) {
		const struct st_rip3_pick_list_text *label = &list->labels[index];
		if (label->highlighted == 0)
			continue;
		size_t length = strlen(label->bytes), at = 0;
		for (uint64_t marked = label->highlighted; at < length; marked >>= 1) {
			uint32_t codepoint;
			size_t   count = next_scalar(label->bytes + at, length - at, &codepoint);
			if ((marked & 1) != 0) {
				bool equal;
				if (st_unicode_caseless_equal(&equal, key, (size_t)key_length, label->bytes + at,
					count)
				        == 0
				    && equal)
					return index;
				break;
			}
			at += count;
		}
	}
	return SIZE_MAX;
}

int
st_term_rip3_pick_list_key(struct st_term_rip3_pick_list *dialog, const struct st_console_event *event,
    uint32_t codepage_scalar, size_t *choice)
{
	if (event->type == ST_CONSOLE_EVENT_TEXT) {
		if (!(event->data.text.modifiers
		        & (ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)))
			*choice = hotkey(dialog->list, event->data.text.codepoint);
		return 0;
	}
	if (event->type == ST_CONSOLE_EVENT_CODEPAGE_BYTE) {
		*choice = hotkey(dialog->list, codepage_scalar);
		return 0;
	}
	if (event->type != ST_CONSOLE_EVENT_KEY_CODE
	    || (event->data.key.modifiers
	        & (ST_CONSOLE_MODIFIER_CONTROL | ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)))
		return 0;
	int64_t selected = (int64_t)dialog->selected;
	switch (event->data.key.code) {
		case ST_CONSOLE_KEY_UP:
			return move_to(dialog, selected - 1);
		case ST_CONSOLE_KEY_DOWN:
			return move_to(dialog, selected + 1);
		case ST_CONSOLE_KEY_PAGE_UP:
			return move_to(dialog, selected - dialog->visible);
		case ST_CONSOLE_KEY_PAGE_DOWN:
			return move_to(dialog, selected + dialog->visible);
		case ST_CONSOLE_KEY_HOME:
			return move_to(dialog, 0);
		case ST_CONSOLE_KEY_END:
			return move_to(dialog, (int64_t)dialog->list->count - 1);
		case 13:
			*choice = dialog->selected;
			break;
		case 27:
			if (!dialog->list->required)
				*choice = dialog->list->count;
			break;
		default:
			if (event->data.key.code < 128)
				*choice = hotkey(dialog->list, event->data.key.code);
			break;
	}
	return 0;
}

int
st_term_rip3_pick_list_mouse(struct st_term_rip3_pick_list *dialog, const struct st_console_event *event,
    size_t *choice)
{
	int64_t x = (int64_t)event->data.mouse.pixel_x - dialog->area.x;
	int64_t y = (int64_t)event->data.mouse.pixel_y - dialog->area.y;
	if (event->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL)
		return move_to(dialog, (int64_t)dialog->selected - event->data.mouse.wheel_y);
	if (event->data.mouse.action != ST_CONSOLE_MOUSE_BUTTON)
		return 0;
	size_t target = SIZE_MAX;
	if (x >= 0 && x < dialog->area.width && y >= dialog->heading * CELL_HEIGHT && y < dialog->area.height)
		target = dialog->first + (size_t)y / CELL_HEIGHT - dialog->heading;
	if ((event->data.mouse.buttons & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0) {
		dialog->pressed = target;
		if (target != SIZE_MAX)
			return move_to(dialog, (int64_t)target);
	}
	else {
		if (target != SIZE_MAX && target == dialog->pressed)
			*choice = target;
		dialog->pressed = SIZE_MAX;
	}
	return 0;
}

void
st_term_rip3_pick_list_disarm(struct st_term_rip3_pick_list *dialog)
{
	if (dialog != NULL)
		dialog->pressed = SIZE_MAX;
}
