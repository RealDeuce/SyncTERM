/* Console text selection used by the Wren menu and REPL. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "st_console.h"
#include "st_utf8.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct text_selection {
	st_console_t *console;
	struct st_console_cell *original;
	struct st_console_cell *displayed;
	uint8_t *selected;
	uint32_t columns;
	uint32_t rows;
	int32_t start_x;
	int32_t start_y;
	int32_t end_x;
	int32_t end_y;
	bool rectangular;
};

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
				selection->displayed[offset].flags ^=
				    ST_CONSOLE_CELL_REVERSE;
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
append_cell(char *text, size_t capacity, size_t *length,
	const struct st_console_cell *cell)
{
	char encoded[4];
	uint32_t codepoint;
	ptrdiff_t amount;

	if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0)
		return 0;
	codepoint = cell->codepoint == 0 ? ' ' : cell->codepoint;
	amount = st_utf8_encode_one(encoded, sizeof(encoded), codepoint);
	if (amount < 0)
		amount = st_utf8_encode_one(encoded, sizeof(encoded), UINT32_C(0xfffd));
	if (amount < 0 || (size_t)amount > capacity - *length)
		return IO_ERROR_OUT_OF_RANGE;
	for (ptrdiff_t index = 0; index < amount; ++index)
		text[(*length)++] = encoded[index];
	return 0;
}

static int
copy_selection(struct text_selection *selection)
{
	size_t count = (size_t)selection->columns * selection->rows;
	size_t capacity;
	size_t length = 0;
	char *text;
	int result = 0;

	if (count > (SIZE_MAX - 1) / 4)
		return IO_ERROR_OUT_OF_RANGE;
	capacity = count * 4;
	if ((size_t)selection->rows > SIZE_MAX - capacity - 1)
		return IO_ERROR_OUT_OF_RANGE;
	capacity += (size_t)selection->rows + 1;
	text = malloc(capacity);
	if (text == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (uint32_t y = 0; y < selection->rows; ++y) {
		size_t last_text = length;
		bool have_row = false;

		for (uint32_t x = 0; x < selection->columns; ++x) {
			size_t offset = (size_t)y * selection->columns + x;
			const struct st_console_cell *cell =
			    &selection->original[offset];

			if (!cell_selected(selection, x, y))
				continue;
			have_row = true;
			result = append_cell(text, capacity, &length, cell);
			if (result != 0)
				goto done;
			if (cell->codepoint != 0 && cell->codepoint != ' '
			    && (cell->flags & ST_CONSOLE_CELL_CONTINUATION) == 0)
				last_text = length;
		}
		if (!have_row)
			continue;
		if (selection->rectangular
		    || cell_selected(selection, selection->columns - 1, y)) {
			length = last_text;
			text[length++] = '\n';
		}
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
	size_t count;
	bool copy = false;
	int result;

	if (context == NULL || context->console == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!context->mouse_left_pressed || !context->mouse_left_dragging)
		return 0;
	result = st_console_size(context->console, &selection.columns,
	    &selection.rows);
	if (result != 0 || selection.columns == 0 || selection.rows == 0)
		return result != 0 ? result : IO_ERROR_OUT_OF_RANGE;
	if (selection.columns > SIZE_MAX / selection.rows)
		return IO_ERROR_OUT_OF_RANGE;
	count = (size_t)selection.columns * selection.rows;
	if (count > SIZE_MAX / sizeof(*selection.original))
		return IO_ERROR_OUT_OF_RANGE;
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
	selection.start_x = context->mouse_drag_start_x;
	selection.start_y = context->mouse_drag_start_y;
	selection.end_x = context->mouse_drag_end_x;
	selection.end_y = context->mouse_drag_end_y;
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
			context->mouse_drag_end_x = event.data.mouse.x;
			context->mouse_drag_end_y = event.data.mouse.y;
			result = paint_selection(&selection, false);
		}
		else if (event.data.mouse.action == ST_CONSOLE_MOUSE_BUTTON
		    && (event.data.mouse.buttons
		    & ST_CONSOLE_MOUSE_BUTTON_LEFT) == 0) {
			selection.end_x = event.data.mouse.x;
			selection.end_y = event.data.mouse.y;
			context->mouse_drag_end_x = event.data.mouse.x;
			context->mouse_drag_end_y = event.data.mouse.y;
			context->mouse_left_pressed = false;
			context->mouse_left_dragging = false;
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
	free(selection.selected);
	free(selection.displayed);
	free(selection.original);
	return result;
}
