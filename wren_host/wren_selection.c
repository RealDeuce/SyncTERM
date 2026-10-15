/* Console text selection used by the Wren menu and REPL. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "st_console.h"
#include "st_cterm.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct text_selection {
	st_console_t *console;
	struct st_console_cell *original;
	struct st_console_cell *displayed;
	uint8_t *selected;
	uint32_t *palette;
	uint32_t columns;
	uint32_t rows;
	int32_t start_x;
	int32_t start_y;
	int32_t end_x;
	int32_t end_y;
	bool rectangular;
};

static uint32_t
colour_value(const struct text_selection *selection, uint32_t colour)
{
	if (ST_CONSOLE_COLOR_IS_RGB(colour))
		return colour & UINT32_C(0xffffff);
	return colour <= UINT16_MAX ? selection->palette[colour] : 0;
}

static uint8_t
nearest_legacy_colour(const struct text_selection *selection, uint32_t colour,
	uint8_t count)
{
	uint32_t value = colour_value(selection, colour);
	uint32_t best_distance = UINT32_MAX;
	uint8_t best = 0;

	for (uint8_t index = 0; index < count; ++index) {
		uint32_t candidate = selection->palette[index];
		int32_t red = (int32_t)((value >> 16) & UINT32_C(0xff))
		    - (int32_t)((candidate >> 16) & UINT32_C(0xff));
		int32_t green = (int32_t)((value >> 8) & UINT32_C(0xff))
		    - (int32_t)((candidate >> 8) & UINT32_C(0xff));
		int32_t blue = (int32_t)(value & UINT32_C(0xff))
		    - (int32_t)(candidate & UINT32_C(0xff));
		uint32_t distance = (uint32_t)(red * red + green * green
		    + blue * blue);

		if (distance < best_distance) {
			best_distance = distance;
			best = index;
		}
	}
	return best;
}

static void
highlight_cell(const struct text_selection *selection,
	struct st_console_cell *cell)
{
	uint32_t foreground = cell->foreground;
	uint32_t background = cell->background;
	uint8_t selected_background;

	/* Apply reverse before replacing it with the selection colours. */
	if ((cell->flags & ST_CONSOLE_CELL_REVERSE) != 0) {
		uint32_t temporary = foreground;

		foreground = background;
		background = temporary;
		cell->flags &= ~ST_CONSOLE_CELL_REVERSE;
	}
	selected_background = nearest_legacy_colour(selection, background, 8)
	    == 4 ? 3 : 4;
	if (nearest_legacy_colour(selection, foreground, 16)
	    == selected_background)
		foreground = selected_background | 8;
	cell->foreground = foreground;
	cell->background = selected_background;
}

static uint32_t
clamp_coordinate(int32_t value, uint32_t limit)
{
	if (value < 0)
		return 0;
	if ((uint32_t)value >= limit)
		return limit - 1;
	return (uint32_t)value;
}

static bool
cell_selected(const struct text_selection *selection, uint32_t x, uint32_t y)
{
	uint32_t start_x = clamp_coordinate(selection->start_x,
	    selection->columns);
	uint32_t start_y = clamp_coordinate(selection->start_y, selection->rows);
	uint32_t end_x = clamp_coordinate(selection->end_x, selection->columns);
	uint32_t end_y = clamp_coordinate(selection->end_y, selection->rows);

	if (selection->rectangular) {
		uint32_t left = start_x < end_x ? start_x : end_x;
		uint32_t right = start_x > end_x ? start_x : end_x;
		uint32_t top = start_y < end_y ? start_y : end_y;
		uint32_t bottom = start_y > end_y ? start_y : end_y;

		return x >= left && x <= right && y >= top && y <= bottom;
	}
	size_t position = (size_t)y * selection->columns + x;
	size_t start = (size_t)start_y * selection->columns + start_x;
	size_t end = (size_t)end_y * selection->columns + end_x;

	if (start > end) {
		size_t temporary = start;

		start = end;
		end = temporary;
	}
	return position >= start && position <= end;
}

static int
paint_selection(struct text_selection *selection, bool clear)
{
	int result = st_console_begin_update(selection->console);

	if (result != 0)
		return result;
	for (uint32_t y = 0; y < selection->rows && result == 0; ++y) {
		bool changed = false;

		for (uint32_t x = 0; x < selection->columns; ++x) {
			size_t offset = (size_t)y * selection->columns + x;
			bool selected = !clear && cell_selected(selection, x, y);

			if (selection->selected[offset] == selected)
				continue;
			selection->selected[offset] = selected;
			selection->displayed[offset] = selection->original[offset];
			if (selected)
				highlight_cell(selection, &selection->displayed[offset]);
			changed = true;
		}
		if (changed) {
			struct st_console_rect row = {
				.x = 0,
				.y = y,
				.width = selection->columns,
				.height = 1,
			};
			result = st_console_write_cells(selection->console, &row,
			    &selection->displayed[(size_t)y * selection->columns],
			    selection->columns);
		}
	}
	int end_result = st_console_end_update(selection->console);

	if (result == 0)
		result = end_result;
	if (result == 0)
		result = st_console_present(selection->console);
	return result;
}

static int
append_cell(st_console_t *console, char **text, size_t *capacity,
	size_t *length, const struct st_console_cell *cell, bool *nonspace)
{
	static const char replacement[] = "\xef\xbf\xbd";
	size_t amount;
	int status;

	*nonspace = false;
	if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
		return 0;
	status = st_console_font_glyph_text(console, cell->glyph_font,
	    cell->glyph, NULL, 0, &amount);
	if (status == IO_ERROR_NOT_FOUND)
		amount = sizeof(replacement) - 1;
	else if (status != IO_ERROR_OUT_OF_RANGE && status != 0)
		return status;
	if (amount > SIZE_MAX - *length - 1)
		return IO_ERROR_OUT_OF_RANGE;
	if (*length + amount + 1 > *capacity) {
		size_t fresh_capacity = *capacity == 0 ? 256 : *capacity;
		char *fresh;

		while (fresh_capacity < *length + amount + 1) {
			if (fresh_capacity > SIZE_MAX / 2) {
				fresh_capacity = *length + amount + 1;
				break;
			}
			fresh_capacity *= 2;
		}
		fresh = realloc(*text, fresh_capacity);
		if (fresh == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		*text = fresh;
		*capacity = fresh_capacity;
	}
	if (status == IO_ERROR_NOT_FOUND)
		memcpy(*text + *length, replacement, amount);
	else {
		status = st_console_font_glyph_text(console, cell->glyph_font,
		    cell->glyph, *text + *length, amount, &amount);
		if (status != 0)
			return status;
	}
	*nonspace = amount != 1 || (*text)[*length] != ' ';
	*length += amount;
	return 0;
}

static int
copy_selection(struct text_selection *selection)
{
	size_t capacity = 0;
	size_t length = 0;
	char *text = NULL;
	int result = 0;

	for (uint32_t y = 0; y < selection->rows; ++y) {
		size_t last_text = length;
		bool have_row = false;

		for (uint32_t x = 0; x < selection->columns; ++x) {
			size_t offset = (size_t)y * selection->columns + x;
			const struct st_console_cell *cell =
			    &selection->original[offset];
			bool nonspace;

			if (!cell_selected(selection, x, y))
				continue;
			have_row = true;
			result = append_cell(selection->console, &text, &capacity,
			    &length, cell, &nonspace);
			if (result != 0)
				goto done;
			if (nonspace)
				last_text = length;
		}
		if (!have_row)
			continue;
		if (selection->rectangular
		    || cell_selected(selection, selection->columns - 1, y)) {
			length = last_text;
			if (length + 2 > capacity) {
				char *fresh = realloc(text, length + 2);

				if (fresh == NULL) {
					result = IO_ERROR_OUT_OF_MEMORY;
					goto done;
				}
				text = fresh;
				capacity = length + 2;
			}
			text[length++] = '\n';
		}
	}
	if (length + 1 > capacity) {
		char *fresh = realloc(text, length + 1);

		if (fresh == NULL) {
			result = IO_ERROR_OUT_OF_MEMORY;
			goto done;
		}
		text = fresh;
	}
	text[length] = '\0';
	/* A console without a host clipboard simply cannot publish the result. */
	(void)st_console_set_clipboard_text(selection->console, text);

done:
	free(text);
	return result;
}

int
st_wren_mouse_drag_select(struct st_wren_context *context, bool rectangular)
{
	struct text_selection selection = {
		.console = context == NULL ? NULL : context->console,
		.rectangular = rectangular,
	};
	struct st_console_event event;
	struct st_console_rect screen;
	struct st_wren_mouse_gesture *gesture;
	size_t count;
	bool copy = false;
	int result;

	if (context == NULL || context->console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	gesture = &context->mouse_gestures[0];
	if (!gesture->pressed || !gesture->dragging)
		return 0;
	result = st_console_size(context->console, &selection.columns,
	    &selection.rows);
	if (result != 0 || selection.columns == 0 || selection.rows == 0)
		return result != 0 ? result : IO_ERROR_OUT_OF_RANGE;
	selection.palette = malloc((UINT16_MAX + 1U)
	    * sizeof(selection.palette[0]));
	if (selection.palette == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	result = st_console_get_palette(context->console, 0, selection.palette,
	    UINT16_MAX + 1U);
	if (result != 0)
		goto done;
	if (selection.columns > SIZE_MAX / selection.rows) {
		result = IO_ERROR_OUT_OF_RANGE;
		goto done;
	}
	count = (size_t)selection.columns * selection.rows;
	if (count > SIZE_MAX / sizeof(*selection.original)) {
		result = IO_ERROR_OUT_OF_RANGE;
		goto done;
	}
	selection.original = malloc(count * sizeof(*selection.original));
	selection.displayed = malloc(count * sizeof(*selection.displayed));
	selection.selected = calloc(count, sizeof(*selection.selected));
	if (selection.original == NULL || selection.displayed == NULL
	    || selection.selected == NULL) {
		result = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	screen = (struct st_console_rect){0, 0, selection.columns, selection.rows};
	result = st_console_read_cells(context->console, &screen,
	    selection.original, selection.columns);
	if (result != 0)
		goto done;
	for (size_t index = 0; index < count; ++index)
		selection.displayed[index] = selection.original[index];
	selection.start_x = gesture->start_x;
	selection.start_y = gesture->start_y;
	selection.end_x = gesture->end_x;
	selection.end_y = gesture->end_y;
	result = paint_selection(&selection, false);
	while (result == 0) {
		result = st_console_event_wait(context->console, &event);
		if (result != 0)
			break;
		if (event.type != ST_CONSOLE_EVENT_MOUSE) {
			result = st_console_post_event(context->console, &event);
			break;
		}
		context->mouse_buttons = event.data.mouse.buttons;
		if (event.data.mouse.action == ST_CONSOLE_MOUSE_MOVE
		    && (event.data.mouse.buttons
		    & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0) {
			selection.end_x = event.data.mouse.x;
			selection.end_y = event.data.mouse.y;
			gesture->end_x = event.data.mouse.x;
			gesture->end_y = event.data.mouse.y;
			result = paint_selection(&selection, false);
		}
		else if (event.data.mouse.action == ST_CONSOLE_MOUSE_BUTTON
		    && (event.data.mouse.buttons
		    & ST_CONSOLE_MOUSE_BUTTON_LEFT) == 0) {
			st_cterm_sync_mouse_buttons(context->cterm,
			    event.data.mouse.buttons);
			selection.end_x = event.data.mouse.x;
			selection.end_y = event.data.mouse.y;
			gesture->end_x = event.data.mouse.x;
			gesture->end_y = event.data.mouse.y;
			gesture->pressed = false;
			gesture->dragging = false;
			copy = true;
			break;
		}
	}
	{
		int restore_result = paint_selection(&selection, true);

		if (result == 0)
			result = restore_result;
	}
	if (result == 0 && copy)
		result = copy_selection(&selection);

done:
	free(selection.palette);
	free(selection.selected);
	free(selection.displayed);
	free(selection.original);
	return result;
}
