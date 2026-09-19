/* Wren bindings for the console, input, and menu-safe host facilities. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"
#include "st_app_paths.h"
#include "st_builtin_fonts.h"
#include "st_console.h"
#include "st_codepage.h"
#include "st_settings.h"
#include "st_theme.h"
#include "st_time.h"
#include "st_utf8.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum foreign_kind {
	FOREIGN_GENERIC,
	FOREIGN_CELL,
	FOREIGN_SURFACE,
	FOREIGN_SNAPSHOT,
	FOREIGN_KEY_EVENT,
	FOREIGN_PHYSICAL_KEY_EVENT,
	FOREIGN_MOUSE_EVENT,
	FOREIGN_CURSOR,
};

struct foreign_header {
	enum foreign_kind kind;
};

struct surface_storage {
	size_t references;
	size_t count;
	uint32_t width;
	uint32_t height;
	struct st_console_cell *cells;
};

struct foreign_surface {
	struct foreign_header header;
	struct surface_storage *storage;
};

struct foreign_cell {
	struct foreign_header header;
	struct surface_storage *storage;
	size_t index;
};

struct foreign_snapshot {
	struct foreign_header header;
	st_console_snapshot_t *snapshot;
};

struct foreign_key_event {
	struct foreign_header header;
	uint16_t code;
	uint32_t codepoint;
	bool has_codepoint;
};

struct foreign_physical_key_event {
	struct foreign_header header;
	uint16_t evdev;
	bool pressed;
};

struct foreign_mouse_event {
	struct foreign_header header;
	int event;
	uint32_t buttons;
	uint32_t modifiers;
	int32_t start_x;
	int32_t start_y;
	int32_t end_x;
	int32_t end_y;
	int32_t start_pixel_x;
	int32_t start_pixel_y;
	int32_t end_pixel_x;
	int32_t end_pixel_y;
};

struct foreign_cursor {
	struct foreign_header header;
	struct st_console_custom_cursor cursor;
};

struct binding {
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static bool
slot_integer(WrenVM *vm, int slot, int64_t minimum, int64_t maximum,
	int64_t *result)
{
	double value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM) {
		st_wren_abort(vm, "expected an integer");
		return false;
	}
	value = wrenGetSlotDouble(vm, slot);
	if (!isfinite(value) || trunc(value) != value || value < (double)minimum
	    || value > (double)maximum) {
		st_wren_abort(vm, "integer is out of range");
		return false;
	}
	*result = (int64_t)value;
	return true;
}

static struct foreign_header *
foreign_kind(WrenVM *vm, int slot, enum foreign_kind kind)
{
	struct foreign_header *header;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_FOREIGN) {
		st_wren_abort(vm, "expected a foreign value");
		return NULL;
	}
	header = wrenGetSlotForeign(vm, slot);
	if (header == NULL || header->kind != kind) {
		st_wren_abort(vm, "foreign value has the wrong type");
		return NULL;
	}
	return header;
}

static void
storage_release(struct surface_storage *storage)
{
	if (storage == NULL)
		return;
	if (--storage->references == 0) {
		free(storage->cells);
		free(storage);
	}
}

static struct surface_storage *
storage_create(uint32_t width, uint32_t height,
	const struct st_console_style *style)
{
	struct surface_storage *storage;
	struct st_console_cell blank;

	if (height != 0 && width > SIZE_MAX / height)
		return NULL;
	storage = calloc(1, sizeof(*storage));
	if (storage == NULL)
		return NULL;
	storage->count = (size_t)width * height;
	if (storage->count != 0) {
		storage->cells = calloc(storage->count, sizeof(storage->cells[0]));
		if (storage->cells == NULL) {
			free(storage);
			return NULL;
		}
	}
	storage->references = 1;
	storage->width = width;
	storage->height = height;
	blank = (struct st_console_cell){
		.codepoint = ' ',
		.foreground = style->foreground,
		.background = style->background,
		.flags = style->flags & ST_CONSOLE_CELL_PUBLIC_FLAGS,
		.font = style->font,
		.hyperlink = style->hyperlink,
	};
	for (size_t i = 0; i < storage->count; ++i)
		storage->cells[i] = blank;
	return storage;
}

static struct st_console_cell *
cell_value(WrenVM *vm, int slot)
{
	struct foreign_cell *cell = (struct foreign_cell *)foreign_kind(vm, slot,
	    FOREIGN_CELL);

	if (cell == NULL || cell->storage == NULL
	    || cell->index >= cell->storage->count)
		return NULL;
	return &cell->storage->cells[cell->index];
}

static struct foreign_cell *
foreign_cell_value(WrenVM *vm, int slot)
{
	struct foreign_cell *cell = (struct foreign_cell *)foreign_kind(vm, slot,
	    FOREIGN_CELL);

	if (cell == NULL || cell->storage == NULL
	    || cell->index >= cell->storage->count)
		return NULL;
	return cell;
}

static void
clear_cell_pair(WrenVM *vm, struct foreign_cell *foreign)
{
	struct surface_storage *storage = foreign->storage;
	struct st_console_cell *cell = &storage->cells[foreign->index];
	struct st_console_cell blank = *cell;
	uint32_t glyph = ' ';
	uint16_t glyph_font = blank.font;
	uint8_t columns;

	blank.codepoint = ' ';
	blank.flags &= ~(ST_CONSOLE_CELL_CONTINUATION
	    | ST_CONSOLE_CELL_EXPLICIT_GLYPH | ST_CONSOLE_CELL_FALLBACK_FONT);
	(void)st_console_font_lookup(st_wren_context_from_vm(vm)->console,
	    blank.font, ' ', &glyph_font, &glyph, &columns);
	blank.glyph = glyph;
	if (glyph_font != blank.font)
		blank.flags |= ST_CONSOLE_CELL_FALLBACK_FONT;
	if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0
	    && foreign->index % storage->width != 0)
		storage->cells[foreign->index - 1] = blank;
	else if (foreign->index + 1 < storage->count
	    && foreign->index % storage->width + 1 < storage->width
	    && (storage->cells[foreign->index + 1].flags
	    & ST_CONSOLE_CELL_CONTINUATION) != 0)
		storage->cells[foreign->index + 1] = blank;
	*cell = blank;
}

static void
set_cell_codepoint(WrenVM *vm, struct foreign_cell *foreign,
	uint32_t codepoint, bool raw, uint8_t raw_glyph)
{
	struct st_console_cell value = foreign->storage->cells[foreign->index];
	uint32_t glyph = raw_glyph;
	uint16_t glyph_font = value.font;
	uint8_t columns = 1;

	clear_cell_pair(vm, foreign);
	value.codepoint = codepoint;
	value.flags &= ~(ST_CONSOLE_CELL_CONTINUATION
	    | ST_CONSOLE_CELL_EXPLICIT_GLYPH | ST_CONSOLE_CELL_FALLBACK_FONT);
	if (raw)
		value.flags |= ST_CONSOLE_CELL_EXPLICIT_GLYPH;
	else if (st_console_font_lookup(
	    st_wren_context_from_vm(vm)->console, value.font, codepoint,
	    &glyph_font, &glyph, &columns) != 0) {
		glyph = '?';
		glyph_font = value.font;
		columns = 1;
	}
	value.glyph = glyph;
	if (glyph_font != value.font)
		value.flags |= ST_CONSOLE_CELL_FALLBACK_FONT;
	foreign->storage->cells[foreign->index] = value;
	if (columns == 2 && foreign->storage->width != 0
	    && foreign->index % foreign->storage->width + 1
	    < foreign->storage->width) {
		value.flags |= ST_CONSOLE_CELL_CONTINUATION;
		foreign->storage->cells[foreign->index + 1] = value;
	}
}

static uint8_t
resolved_cell_columns(WrenVM *vm, const struct st_console_cell *cell)
{
	uint8_t columns = 1;

	if ((cell->flags & ST_CONSOLE_CELL_EXPLICIT_GLYPH) != 0) {
		(void)st_console_font_glyph_columns(
		    st_wren_context_from_vm(vm)->console,
		    (cell->flags & ST_CONSOLE_CELL_FALLBACK_FONT) != 0
		    ? 0 : cell->font,
		    cell->glyph, &columns);
	}
	else {
		uint16_t glyph_font;
		uint32_t glyph;

		(void)st_console_font_lookup(
		    st_wren_context_from_vm(vm)->console, cell->font,
		    cell->codepoint, &glyph_font, &glyph, &columns);
	}
	return columns;
}

static void
allocate_generic(WrenVM *vm)
{
	struct foreign_header *header = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*header));

	header->kind = FOREIGN_GENERIC;
}

static void
allocate_cell(WrenVM *vm)
{
	struct st_console_style style;
	struct foreign_cell *cell = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*cell));

	memset(cell, 0, sizeof(*cell));
	cell->header.kind = FOREIGN_CELL;
	if (st_console_get_style(st_wren_context_from_vm(vm)->console, &style) != 0)
		style = (struct st_console_style){.foreground = 7};
	cell->storage = storage_create(1, 1, &style);
	if (cell->storage == NULL)
		st_wren_abort(vm, "Cell.new: out of memory");
}

static void
allocate_surface(WrenVM *vm)
{
	struct st_console_style style;
	struct foreign_surface *surface;
	int64_t width;
	int64_t height;

	if (!slot_integer(vm, 1, 0, UINT32_MAX, &width)
	    || !slot_integer(vm, 2, 0, UINT32_MAX, &height))
		return;
	surface = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*surface));
	memset(surface, 0, sizeof(*surface));
	surface->header.kind = FOREIGN_SURFACE;
	if (st_console_get_style(st_wren_context_from_vm(vm)->console, &style) != 0)
		style = (struct st_console_style){.foreground = 7};
	surface->storage = storage_create((uint32_t)width, (uint32_t)height,
	    &style);
	if (surface->storage == NULL)
		st_wren_abort(vm, "Surface.new: dimensions are too large");
}

static uint16_t
legacy_key_code(uint16_t key, uint32_t modifiers)
{
	bool shift = (modifiers & ST_CONSOLE_MODIFIER_SHIFT) != 0;
	bool control = (modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0;
	bool alt = (modifiers & (ST_CONSOLE_MODIFIER_ALT
	    | ST_CONSOLE_MODIFIER_GUI)) != 0;
	static const uint16_t plain_f[] = {
		0x3b00, 0x3c00, 0x3d00, 0x3e00, 0x3f00, 0x4000,
		0x4100, 0x4200, 0x4300, 0x4400, 0x8500, 0x8600,
	};
	static const uint16_t shift_f[] = {
		0x5400, 0x5500, 0x5600, 0x5700, 0x5800, 0x5900,
		0x5a00, 0x5b00, 0x5c00, 0x5d00, 0x8700, 0x8800,
	};
	static const uint16_t control_f[] = {
		0x5e00, 0x5f00, 0x6000, 0x6100, 0x6200, 0x6300,
		0x6400, 0x6500, 0x6600, 0x6700, 0x8900, 0x8a00,
	};
	static const uint16_t alt_f[] = {
		0x6800, 0x6900, 0x6a00, 0x6b00, 0x6c00, 0x6d00,
		0x6e00, 0x6f00, 0x7000, 0x7100, 0x8b00, 0x8c00,
	};

	if (key >= ST_CONSOLE_KEY_F1 && key <= ST_CONSOLE_KEY_F12) {
		size_t index = key - ST_CONSOLE_KEY_F1;

		return alt ? alt_f[index] : control ? control_f[index] :
		    shift ? shift_f[index] : plain_f[index];
	}
	switch (key) {
	case ST_CONSOLE_KEY_UP:
		return alt ? 0x9800 : control ? 0x8d00 :
		    shift ? 0x91e0 : 0x4800;
	case ST_CONSOLE_KEY_DOWN:
		return alt ? 0xa000 : control ? 0x9100 :
		    shift ? 0x96e0 : 0x5000;
	case ST_CONSOLE_KEY_LEFT:
		return control ? 0x7300 : shift ? 0x93e0 : 0x4b00;
	case ST_CONSOLE_KEY_RIGHT:
		return control ? 0x7400 : shift ? 0x94e0 : 0x4d00;
	case ST_CONSOLE_KEY_INSERT:
		return alt ? 0xa200 : control ? 0x0400 :
		    shift ? 0x0500 : 0x5200;
	case ST_CONSOLE_KEY_DELETE:
		return alt ? 0xa300 : control ? 0x0600 :
		    shift ? 0x0700 : 0x5300;
	case ST_CONSOLE_KEY_HOME: return 0x4700;
	case ST_CONSOLE_KEY_END:
		return control ? 0x7500 : shift ? 0x95e0 : 0x4f00;
	case ST_CONSOLE_KEY_PAGE_UP: return 0x4900;
	case ST_CONSOLE_KEY_PAGE_DOWN: return 0x5100;
	case 9: return shift ? 0x0f00 : 9;
	default: return key;
	}
}

static uint16_t
modified_physical_key(uint16_t evdev, uint32_t modifiers)
{
	static const uint16_t alt_letters[26] = {
		0x1e00, 0x3000, 0x2e00, 0x2000, 0x1200, 0x2100, 0x2200,
		0x2300, 0x1700, 0x2400, 0x2500, 0x2600, 0x3200, 0x3100,
		0x1800, 0x1900, 0x1000, 0x1300, 0x1f00, 0x1400, 0x1600,
		0x2f00, 0x1100, 0x2d00, 0x1500, 0x2c00,
	};
	static const uint16_t letter_evdev[26] = {
		30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
		49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,
	};
	bool control = (modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0;
	bool alt = (modifiers & (ST_CONSOLE_MODIFIER_ALT
	    | ST_CONSOLE_MODIFIER_GUI)) != 0;

	if (control && evdev == 41)
		return 0x29e0;
	for (size_t i = 0; i < 26; ++i) {
		if (evdev == letter_evdev[i])
			return alt ? alt_letters[i] : control ? (uint16_t)(i + 1) : 0;
	}
	if (alt && evdev >= 2 && evdev <= 11)
		return evdev == 11 ? 0x8100 : (uint16_t)(0x7800
		    + (evdev - 2) * 0x0100);
	return 0;
}

static void
allocate_key_event(WrenVM *vm)
{
	struct foreign_key_event *event = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*event));
	int64_t code;

	memset(event, 0, sizeof(*event));
	event->header.kind = FOREIGN_KEY_EVENT;
	if (!slot_integer(vm, 1, 0, UINT16_MAX, &code))
		return;
	event->code = (uint16_t)code;
	if ((event->code & 0xff00U) == 0) {
		event->codepoint = st_codepage_to_unicode(ST_CODEPAGE_CP437,
		    (uint8_t)event->code, false);
		event->has_codepoint = true;
	}
}

static void
allocate_physical_key_event(WrenVM *vm)
{
	struct foreign_physical_key_event *event = wrenSetSlotNewForeign(vm, 0,
	    0, sizeof(*event));
	int64_t evdev;

	memset(event, 0, sizeof(*event));
	event->header.kind = FOREIGN_PHYSICAL_KEY_EVENT;
	if (!slot_integer(vm, 1, 0, UINT16_MAX, &evdev)
	    || wrenGetSlotType(vm, 2) != WREN_TYPE_BOOL)
		return;
	event->evdev = (uint16_t)evdev;
	event->pressed = wrenGetSlotBool(vm, 2);
}

static void
allocate_mouse_event(WrenVM *vm)
{
	struct foreign_mouse_event *event = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*event));
	int count = wrenGetSlotCount(vm) - 1;
	int64_t values[7] = {0};

	memset(event, 0, sizeof(*event));
	event->header.kind = FOREIGN_MOUSE_EVENT;
	if (count < 3 || count > 7) {
		st_wren_abort(vm, "MouseEvent.new expects 3 through 7 arguments");
		return;
	}
	for (int i = 0; i < count; ++i) {
		if (!slot_integer(vm, i + 1, INT32_MIN, UINT32_MAX, &values[i]))
			return;
	}
	event->event = (int)values[0];
	event->start_x = (int32_t)values[1];
	event->start_y = (int32_t)values[2];
	event->end_x = event->start_x;
	event->end_y = event->start_y;
	if (count == 4)
		event->modifiers = (uint32_t)values[3];
	else if (count >= 5) {
		event->end_x = (int32_t)values[3];
		event->end_y = (int32_t)values[4];
		if (count >= 6)
			event->modifiers = (uint32_t)values[5];
		if (count == 7)
			event->buttons = (uint32_t)values[6];
	}
}

static void
allocate_cursor(WrenVM *vm)
{
	struct foreign_cursor *cursor = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*cursor));

	cursor->header.kind = FOREIGN_CURSOR;
	if (st_console_get_custom_cursor(st_wren_context_from_vm(vm)->console, &cursor->cursor)
	    != 0) {
		cursor->cursor = (struct st_console_custom_cursor){
			.start = 6, .end = 7, .range = 8, .visible = true,
			.blink = true,
		};
	}
}

static void
finalize_foreign(void *data)
{
	struct foreign_header *header = data;

	if (header == NULL)
		return;
	switch (header->kind) {
	case FOREIGN_CELL:
		storage_release(((struct foreign_cell *)data)->storage);
		break;
	case FOREIGN_SURFACE:
		storage_release(((struct foreign_surface *)data)->storage);
		break;
	case FOREIGN_SNAPSHOT:
		st_console_snapshot_destroy(
		    ((struct foreign_snapshot *)data)->snapshot);
		break;
	default:
		break;
	}
}

static void
make_key_event(WrenVM *vm, int slot, uint16_t code, uint32_t codepoint,
	bool has_codepoint)
{
	wrenEnsureSlots(vm, slot + 2);
	wrenGetVariable(vm, "syncterm", "KeyEvent", slot + 1);
	struct foreign_key_event *event = wrenSetSlotNewForeign(vm, slot,
	    slot + 1, sizeof(*event));

	event->header.kind = FOREIGN_KEY_EVENT;
	event->code = code;
	event->codepoint = codepoint;
	event->has_codepoint = has_codepoint;
}

static void
make_mouse_event(WrenVM *vm, const struct st_console_event *source,
	int event_code, int32_t start_x, int32_t start_y)
{
	uint32_t cell_width = 0;
	uint32_t cell_height = 0;

	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "MouseEvent", 1);
	struct foreign_mouse_event *event = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*event));
	memset(event, 0, sizeof(*event));
	event->header.kind = FOREIGN_MOUSE_EVENT;
	event->event = event_code;
	event->buttons = source->data.mouse.buttons;
	event->start_x = start_x + 1;
	event->start_y = start_y + 1;
	event->end_x = source->data.mouse.x + 1;
	event->end_y = source->data.mouse.y + 1;
	if (st_console_cell_pixel_size(st_wren_context_from_vm(vm)->console, &cell_width,
	    &cell_height) == 0) {
		event->start_pixel_x = start_x * (int32_t)cell_width;
		event->start_pixel_y = start_y * (int32_t)cell_height;
		event->end_pixel_x = source->data.mouse.x * (int32_t)cell_width;
		event->end_pixel_y = source->data.mouse.y * (int32_t)cell_height;
	}
}

static int
mouse_event_code(struct st_wren_context *context,
	const struct st_console_event *event, int32_t *start_x, int32_t *start_y)
{
	uint32_t changed = context->mouse_buttons ^ event->data.mouse.buttons;
	uint32_t pressed = changed & event->data.mouse.buttons;
	int result = 0;

	*start_x = event->data.mouse.x;
	*start_y = event->data.mouse.y;
	if (event->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL)
		return event->data.mouse.wheel_y > 0 ? 28 : 37;
	if (event->data.mouse.action == ST_CONSOLE_MOUSE_MOVE) {
		if (!context->mouse_left_pressed
		    || (event->data.mouse.buttons
		    & ST_CONSOLE_MOUSE_BUTTON_LEFT) == 0)
			return 0;
		context->mouse_drag_end_x = event->data.mouse.x;
		context->mouse_drag_end_y = event->data.mouse.y;
		if (!context->mouse_left_dragging
		    && context->mouse_drag_end_x == context->mouse_drag_start_x
		    && context->mouse_drag_end_y == context->mouse_drag_start_y)
			return 0;
		*start_x = context->mouse_drag_start_x;
		*start_y = context->mouse_drag_start_y;
		if (!context->mouse_left_dragging) {
			context->mouse_left_dragging = true;
			return 7;
		}
		return 8;
	}
	context->mouse_buttons = event->data.mouse.buttons;
	if ((changed & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0) {
		if ((pressed & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0) {
			context->mouse_drag_start_x = event->data.mouse.x;
			context->mouse_drag_start_y = event->data.mouse.y;
			context->mouse_drag_end_x = event->data.mouse.x;
			context->mouse_drag_end_y = event->data.mouse.y;
			context->mouse_left_pressed = true;
			context->mouse_left_dragging = false;
			result = 1;
		}
		else {
			*start_x = context->mouse_drag_start_x;
			*start_y = context->mouse_drag_start_y;
			result = context->mouse_left_dragging ? 9 : 3;
			context->mouse_left_pressed = false;
			context->mouse_left_dragging = false;
		}
	}
	else if ((changed & ST_CONSOLE_MOUSE_BUTTON_MIDDLE) != 0)
		result = pressed & ST_CONSOLE_MOUSE_BUTTON_MIDDLE ? 10 : 12;
	else if ((changed & ST_CONSOLE_MOUSE_BUTTON_RIGHT) != 0)
		result = pressed & ST_CONSOLE_MOUSE_BUTTON_RIGHT ? 19 : 21;
	return result;
}

static bool
next_input_event(WrenVM *vm, bool wait, int64_t timeout_ms, bool at_exit)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_console_event event;
	int status;

	for (;;) {
		if (!wait)
			status = st_console_event_try(st_wren_context_from_vm(vm)->console, &event);
		else if (timeout_ms < 0)
			status = st_console_event_wait(st_wren_context_from_vm(vm)->console, &event);
		else {
			struct timespec deadline;

			status = st_time_utc_deadline_after_ms(&deadline, timeout_ms);
			if (status == 0)
				status = st_console_event_timedwait(st_wren_context_from_vm(vm)->console,
				    &event, &deadline);
		}
		if (status == IO_ERROR_WOULD_BLOCK || status == IO_ERROR_TIMED_OUT) {
			wrenSetSlotNull(vm, 0);
			return false;
		}
		if (status != 0) {
			st_wren_abort(vm, "input wait failed");
			return false;
		}
		switch (event.type) {
		case ST_CONSOLE_EVENT_KEY_CODE:
			make_key_event(vm, 0, legacy_key_code(event.data.key.code,
			    event.data.key.modifiers), 0, false);
			return true;
		case ST_CONSOLE_EVENT_TEXT: {
			uint8_t byte;
			uint16_t code = st_codepage_from_unicode(&byte,
			    ST_CODEPAGE_CP437, event.data.text.codepoint) ? byte : '?';

			make_key_event(vm, 0, code, event.data.text.codepoint, true);
			return true;
		}
		case ST_CONSOLE_EVENT_DOORWAY_KEY: {
			uint16_t code = (uint16_t)event.data.doorway_key.scan_code << 8;

			/* SyncTERM's application key contract uses a NUL low byte for
			 * both DoorWay extended-key frame forms. */
			make_key_event(vm, 0, code, 0, false);
			return true;
		}
		case ST_CONSOLE_EVENT_PHYSICAL_KEY: {
			uint16_t code;

			if (!event.data.physical_key.pressed)
				continue;
			code = modified_physical_key(event.data.physical_key.evdev,
			    event.data.physical_key.modifiers);
			if (code == 0)
				continue;
			make_key_event(vm, 0, code,
			    code <= 0x1f ? code : 0, code <= 0x1f);
			return true;
		}
		case ST_CONSOLE_EVENT_MOUSE: {
			struct st_wren_context *context = st_wren_context_from_vm(vm);
			int32_t start_x;
			int32_t start_y;
			int code = mouse_event_code(context, &event, &start_x, &start_y);

			if (event.data.mouse.action == ST_CONSOLE_MOUSE_BUTTON
			    && code == 0)
				continue;
			if (code < 64
			    && (context->mouse_events & (UINT64_C(1) << code)) == 0)
				continue;
			make_mouse_event(vm, &event, code, start_x, start_y);
			return true;
		}
		case ST_CONSOLE_EVENT_CLOSE:
			host->exit_requested = true;
			if (at_exit)
				continue;
			make_key_event(vm, 0, 0x7ee0, 0, false);
			return true;
		default:
			continue;
		}
	}
}

static void
fn_input_next(WrenVM *vm)
{
	(void)next_input_event(vm, true, -1, false);
}

static void
fn_input_next_timed(WrenVM *vm)
{
	int64_t milliseconds;

	if (!slot_integer(vm, 1, 0, INT_MAX, &milliseconds))
		return;
	(void)next_input_event(vm, true, milliseconds, false);
}

static void
fn_input_poll(WrenVM *vm)
{
	(void)next_input_event(vm, false, 0, false);
}

static void
fn_input_next_widget(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Input.nextForWidget_ expects a Bool");
		return;
	}
	(void)next_input_event(vm, true, -1, wrenGetSlotBool(vm, 1));
}

static void
fn_input_mouse_events(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0,
	    (double)st_wren_context_from_vm(vm)->mouse_events);
}

static void
fn_input_mouse_events_set(WrenVM *vm)
{
	int64_t value;

	if (slot_integer(vm, 1, 0, INT64_MAX, &value))
		st_wren_context_from_vm(vm)->mouse_events = (uint64_t)value;
}

static void
fn_input_mouse_event_enable(WrenVM *vm)
{
	int64_t value;

	if (slot_integer(vm, 1, 0, 63, &value))
		st_wren_context_from_vm(vm)->mouse_events |= UINT64_C(1) << value;
}

static void
fn_input_mouse_event_disable(WrenVM *vm)
{
	int64_t value;

	if (slot_integer(vm, 1, 0, 63, &value))
		st_wren_context_from_vm(vm)->mouse_events &= ~(UINT64_C(1) << value);
}

static void
fn_input_mouse_visible_set(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Input.mouseVisible expects a Bool");
		return;
	}
	(void)st_console_set_pointer_visible(
	    st_wren_context_from_vm(vm)->console, wrenGetSlotBool(vm, 1));
}

static void
fn_input_mouse_drag(WrenVM *vm)
{
	bool rectangular = false;
	int status;

	if (wrenGetSlotCount(vm) > 1) {
		if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
			st_wren_abort(vm, "Input.mousedrag expects a Bool");
			return;
		}
		rectangular = wrenGetSlotBool(vm, 1);
	}
	status = st_wren_mouse_drag_select(st_wren_context_from_vm(vm),
	    rectangular);
	if (status == IO_ERROR_OUT_OF_MEMORY)
		st_wren_abort(vm, "out of memory selecting text");
	else if (status != 0)
		st_wren_abort(vm, "text selection failed");
}

static void
fn_key_code(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);

	if (event != NULL)
		wrenSetSlotDouble(vm, 0, event->code);
}

static void
fn_key_codepoint(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);

	if (event == NULL)
		return;
	if (event->has_codepoint)
		wrenSetSlotDouble(vm, 0, event->codepoint);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_key_text(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);
	char encoded[4];
	ptrdiff_t length;

	if (event == NULL)
		return;
	if (!event->has_codepoint) {
		wrenSetSlotString(vm, 0, "");
		return;
	}
	length = st_utf8_encode_one(encoded, sizeof(encoded), event->codepoint);
	if (length < 0)
		wrenSetSlotString(vm, 0, "");
	else
		wrenSetSlotBytes(vm, 0, encoded, (size_t)length);
}

static void
fn_key_to_string(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);
	char text[40];

	if (event == NULL)
		return;
	(void)snprintf(text, sizeof(text), "KeyEvent(0x%04X)", event->code);
	wrenSetSlotString(vm, 0, text);
}

#define MOUSE_GETTER(name, field) \
	static void fn_mouse_##name(WrenVM *vm) \
	{ \
		struct foreign_mouse_event *event = \
		    (struct foreign_mouse_event *)foreign_kind(vm, 0, \
		    FOREIGN_MOUSE_EVENT); \
		if (event != NULL) \
			wrenSetSlotDouble(vm, 0, (double)event->field); \
	}

MOUSE_GETTER(event, event)
MOUSE_GETTER(buttons, buttons)
MOUSE_GETTER(modifiers, modifiers)
MOUSE_GETTER(start_x, start_x)
MOUSE_GETTER(start_y, start_y)
MOUSE_GETTER(end_x, end_x)
MOUSE_GETTER(end_y, end_y)
MOUSE_GETTER(start_pixel_x, start_pixel_x)
MOUSE_GETTER(start_pixel_y, start_pixel_y)
MOUSE_GETTER(end_pixel_x, end_pixel_x)
MOUSE_GETTER(end_pixel_y, end_pixel_y)

#undef MOUSE_GETTER

static void
fn_mouse_to_string(WrenVM *vm)
{
	struct foreign_mouse_event *event =
	    (struct foreign_mouse_event *)foreign_kind(vm, 0,
	    FOREIGN_MOUSE_EVENT);
	char text[80];

	if (event == NULL)
		return;
	(void)snprintf(text, sizeof(text), "MouseEvent(%d at %d,%d)",
	    event->event, event->start_x, event->start_y);
	wrenSetSlotString(vm, 0, text);
}

static void
fn_physical_evdev(WrenVM *vm)
{
	struct foreign_physical_key_event *event =
	    (struct foreign_physical_key_event *)foreign_kind(vm, 0,
	    FOREIGN_PHYSICAL_KEY_EVENT);

	if (event != NULL)
		wrenSetSlotDouble(vm, 0, event->evdev);
}

static void
fn_physical_pressed(WrenVM *vm)
{
	struct foreign_physical_key_event *event =
	    (struct foreign_physical_key_event *)foreign_kind(vm, 0,
	    FOREIGN_PHYSICAL_KEY_EVENT);

	if (event != NULL)
		wrenSetSlotBool(vm, 0, event->pressed);
}

static void
fn_screen_size(WrenVM *vm)
{
	uint32_t width;
	uint32_t height;

	if (st_console_size(st_wren_context_from_vm(vm)->console, &width,
	    &height) != 0) {
		st_wren_abort(vm, "Screen.size failed");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, width);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, height);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_screen_save(WrenVM *vm)
{
	st_console_snapshot_t *snapshot = NULL;

	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "Screen", 1);
	struct foreign_snapshot *foreign = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*foreign));
	foreign->header.kind = FOREIGN_SNAPSHOT;
	foreign->snapshot = NULL;
	if (st_console_snapshot_create(&snapshot, st_wren_context_from_vm(vm)->console) != 0) {
		st_wren_abort(vm, "Screen.save failed");
		return;
	}
	foreign->snapshot = snapshot;
}

static void
fn_screen_restore(WrenVM *vm)
{
	struct foreign_snapshot *snapshot = (struct foreign_snapshot *)foreign_kind(
	    vm, 1, FOREIGN_SNAPSHOT);

	if (snapshot != NULL && snapshot->snapshot != NULL
	    && st_console_snapshot_restore(st_wren_context_from_vm(vm)->console,
	    snapshot->snapshot) != 0)
		st_wren_abort(vm, "Screen.restore failed");
}

static struct foreign_surface *
new_surface(WrenVM *vm, int destination, uint32_t width, uint32_t height)
{
	struct st_console_style style;

	if (st_console_get_style(st_wren_context_from_vm(vm)->console, &style) != 0)
		style = (struct st_console_style){.foreground = 7};
	wrenEnsureSlots(vm, destination + 2);
	wrenGetVariable(vm, "syncterm", "Surface", destination + 1);
	struct foreign_surface *surface = wrenSetSlotNewForeign(vm, destination,
	    destination + 1, sizeof(*surface));
	surface->header.kind = FOREIGN_SURFACE;
	surface->storage = storage_create(width, height, &style);
	if (surface->storage == NULL) {
		st_wren_abort(vm, "out of memory creating Surface");
		return NULL;
	}
	return surface;
}

static bool
rect_arguments(WrenVM *vm, int first, struct st_console_rect *rect)
{
	int64_t values[4];

	for (size_t i = 0; i < 4; ++i) {
		if (!slot_integer(vm, first + (int)i, 1, UINT32_MAX, &values[i]))
			return false;
	}
	if (values[2] < values[0] || values[3] < values[1]) {
		st_wren_abort(vm, "rectangle is inverted");
		return false;
	}
	rect->x = (uint32_t)values[0] - 1;
	rect->y = (uint32_t)values[1] - 1;
	rect->width = (uint32_t)(values[2] - values[0] + 1);
	rect->height = (uint32_t)(values[3] - values[1] + 1);
	return true;
}

static void
fn_screen_read_rect(WrenVM *vm)
{
	struct st_console_rect rect;
	struct foreign_surface *surface;

	if (!rect_arguments(vm, 1, &rect))
		return;
	surface = new_surface(vm, 0, rect.width, rect.height);
	if (surface != NULL
	    && st_console_read_cells(st_wren_context_from_vm(vm)->console,
	    &rect, surface->storage->cells, rect.width) != 0)
		st_wren_abort(vm, "Screen.readRect failed");
}

static bool
clip_blit(const struct surface_storage *source, int64_t source_x,
	int64_t source_y, int64_t source_width, int64_t source_height,
	int64_t destination_x, int64_t destination_y, uint32_t destination_width,
	uint32_t destination_height, struct st_console_rect *source_rect,
	struct st_console_rect *destination_rect)
{
	if (source_width <= 0 || source_height <= 0)
		return false;
	if (source_x < 0) {
		destination_x -= source_x;
		source_width += source_x;
		source_x = 0;
	}
	if (source_y < 0) {
		destination_y -= source_y;
		source_height += source_y;
		source_y = 0;
	}
	if (source_x >= source->width || source_y >= source->height)
		return false;
	if (source_width > (int64_t)source->width - source_x)
		source_width = (int64_t)source->width - source_x;
	if (source_height > (int64_t)source->height - source_y)
		source_height = (int64_t)source->height - source_y;
	if (destination_x < 0) {
		source_x -= destination_x;
		source_width += destination_x;
		destination_x = 0;
	}
	if (destination_y < 0) {
		source_y -= destination_y;
		source_height += destination_y;
		destination_y = 0;
	}
	if (destination_x >= destination_width
	    || destination_y >= destination_height)
		return false;
	if (source_width > (int64_t)destination_width - destination_x)
		source_width = (int64_t)destination_width - destination_x;
	if (source_height > (int64_t)destination_height - destination_y)
		source_height = (int64_t)destination_height - destination_y;
	if (source_width <= 0 || source_height <= 0)
		return false;
	*source_rect = (struct st_console_rect){(uint32_t)source_x,
	    (uint32_t)source_y, (uint32_t)source_width, (uint32_t)source_height};
	*destination_rect = (struct st_console_rect){(uint32_t)destination_x,
	    (uint32_t)destination_y, (uint32_t)source_width,
	    (uint32_t)source_height};
	return true;
}

static void
screen_put_rect(WrenVM *vm, bool partial)
{
	struct foreign_surface *surface = (struct foreign_surface *)foreign_kind(
	    vm, 1, FOREIGN_SURFACE);
	struct st_console_rect source_rect;
	struct st_console_rect destination;
	uint32_t screen_width;
	uint32_t screen_height;
	int64_t values[6];

	if (surface == NULL || st_console_size(st_wren_context_from_vm(vm)->console, &screen_width,
	    &screen_height) != 0)
		return;
	if (partial) {
		for (size_t i = 0; i < 6; ++i) {
			if (!slot_integer(vm, (int)i + 2, INT32_MIN, INT32_MAX,
			    &values[i]))
				return;
		}
	}
	else {
		values[0] = 0;
		values[1] = 0;
		values[2] = surface->storage->width;
		values[3] = surface->storage->height;
		if (!slot_integer(vm, 2, INT32_MIN, INT32_MAX, &values[4])
		    || !slot_integer(vm, 3, INT32_MIN, INT32_MAX, &values[5]))
			return;
	}
	if (!clip_blit(surface->storage, values[0], values[1], values[2],
	    values[3], values[4] - 1, values[5] - 1, screen_width,
	    screen_height, &source_rect, &destination))
		return;
	if (st_console_write_complete_cells(st_wren_context_from_vm(vm)->console, &destination,
	    &surface->storage->cells[(size_t)source_rect.y
	    * surface->storage->width + source_rect.x], surface->storage->width)
	    != 0)
		st_wren_abort(vm, "Screen.putRect failed");
}

static void fn_screen_put_rect(WrenVM *vm) { screen_put_rect(vm, false); }
static void fn_screen_put_rect_partial(WrenVM *vm) { screen_put_rect(vm, true); }

static void
fn_screen_attr(WrenVM *vm)
{
	struct st_console_style style;

	if (st_console_get_style(st_wren_context_from_vm(vm)->console, &style)
	    != 0) {
		st_wren_abort(vm, "Screen.attr failed");
		return;
	}
	uint32_t value = (style.foreground & 0x0f)
	    | ((style.background & 0x07) << 4);
	if ((style.flags & ST_CONSOLE_CELL_BLINK) != 0)
		value |= 0x80;
	wrenSetSlotDouble(vm, 0, value);
}

static void
fn_screen_attr_set(WrenVM *vm)
{
	struct st_console_style style;
	int64_t value;

	if (!slot_integer(vm, 1, 0, 255, &value)
	    || st_console_get_style(st_wren_context_from_vm(vm)->console,
	    &style) != 0)
		return;
	style.foreground = (uint32_t)value & 0x0f;
	style.background = ((uint32_t)value >> 4) & 0x07;
	if (((uint32_t)value & 0x80) != 0)
		style.flags |= ST_CONSOLE_CELL_BLINK;
	else
		style.flags &= ~ST_CONSOLE_CELL_BLINK;
	(void)st_console_set_style(st_wren_context_from_vm(vm)->console,
	    &style);
}

static void
fn_screen_window_position(WrenVM *vm)
{
	uint32_t x;
	uint32_t y;

	if (st_console_get_position(st_wren_context_from_vm(vm)->console,
	    &x, &y) != 0)
		return;
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, x + 1);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, y + 1);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_screen_window_position_set(WrenVM *vm)
{
	int64_t x;
	int64_t y;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_LIST
	    || wrenGetListCount(vm, 1) < 2) {
		st_wren_abort(vm, "Screen.window.position expects [x, y]");
		return;
	}
	wrenEnsureSlots(vm, 3);
	wrenGetListElement(vm, 1, 0, 2);
	if (!slot_integer(vm, 2, 1, UINT32_MAX, &x))
		return;
	wrenGetListElement(vm, 1, 1, 2);
	if (!slot_integer(vm, 2, 1, UINT32_MAX, &y))
		return;
	(void)st_console_set_position(st_wren_context_from_vm(vm)->console,
	    (uint32_t)x - 1, (uint32_t)y - 1);
}

static void
fn_screen_window_bounds(WrenVM *vm)
{
	struct st_console_rect window;

	if (st_console_get_window(st_wren_context_from_vm(vm)->console,
	    &window) != 0)
		return;
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	uint32_t values[4] = {window.x + 1, window.y + 1,
	    window.x + window.width, window.y + window.height};
	for (size_t i = 0; i < 4; ++i) {
		wrenSetSlotDouble(vm, 1, values[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void
fn_screen_window_bounds_set(WrenVM *vm)
{
	struct st_console_rect window;
	int64_t values[4];

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_LIST
	    || wrenGetListCount(vm, 1) < 4) {
		st_wren_abort(vm, "Screen.window.bounds expects four values");
		return;
	}
	wrenEnsureSlots(vm, 3);
	for (size_t i = 0; i < 4; ++i) {
		wrenGetListElement(vm, 1, (int)i, 2);
		if (!slot_integer(vm, 2, 1, UINT32_MAX, &values[i]))
			return;
	}
	if (values[2] < values[0] || values[3] < values[1])
		return;
	window = (struct st_console_rect){(uint32_t)values[0] - 1,
	    (uint32_t)values[1] - 1, (uint32_t)(values[2] - values[0] + 1),
	    (uint32_t)(values[3] - values[1] + 1)};
	(void)st_console_set_window(st_wren_context_from_vm(vm)->console,
	    &window);
}

static void
fn_screen_window_clear(WrenVM *vm)
{
	(void)st_console_clear_window(st_wren_context_from_vm(vm)->console,
	    ' ');
}

static void
fn_screen_window_clear_line(WrenVM *vm)
{
	(void)st_console_clear_line(st_wren_context_from_vm(vm)->console, ' ');
}

static void
fn_screen_window_delete_line(WrenVM *vm)
{
	(void)st_console_delete_lines(st_wren_context_from_vm(vm)->console, 1,
	    ' ');
}

static void
fn_screen_window_insert_line(WrenVM *vm)
{
	(void)st_console_insert_lines(st_wren_context_from_vm(vm)->console, 1,
	    ' ');
}

static void
fn_screen_window_scroll(WrenVM *vm)
{
	(void)st_console_scroll(st_wren_context_from_vm(vm)->console, 1, ' ');
}

static void
fn_screen_window_put_char(WrenVM *vm)
{
	int64_t value;

	if (slot_integer(vm, 1, 0, UINT32_MAX, &value))
		(void)st_console_put_codepoint(
		    st_wren_context_from_vm(vm)->console, (uint32_t)value);
}

static void
fn_screen_window_print(WrenVM *vm)
{
	int length;
	const char *text;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Screen.window.print expects a String");
		return;
	}
	text = wrenGetSlotBytes(vm, 1, &length);
	(void)st_console_write_utf8(st_wren_context_from_vm(vm)->console,
	    text, (size_t)length);
}

static void
fn_screen_cell_pixel_size(WrenVM *vm)
{
	uint32_t width;
	uint32_t height;

	if (st_console_cell_pixel_size(st_wren_context_from_vm(vm)->console,
	    &width, &height) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, width);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, height);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_screen_pixel_size(WrenVM *vm)
{
	uint32_t width;
	uint32_t height;

	if (st_console_pixel_size(st_wren_context_from_vm(vm)->console,
	    &width, &height) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, width);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, height);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_screen_text_width(WrenVM *vm)
{
	const unsigned char *text;
	int length;
	int64_t selector;
	uint16_t identifier;
	size_t offset = 0;
	uint64_t width = 0;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING
	    || !slot_integer(vm, 2, 0, UINT8_MAX, &selector)) {
		st_wren_abort(vm, "Screen.textWidth expects a String and font");
		return;
	}
	if (!st_wren_font_identifier(st_wren_context_from_vm(vm), (uint16_t)selector, &identifier)) {
		st_wren_abort(vm, "font is not available");
		return;
	}
	text = (const unsigned char *)wrenGetSlotBytes(vm, 1, &length);
	while (offset < (size_t)length) {
		uint32_t codepoint;
		uint32_t glyph;
		uint16_t glyph_font;
		uint8_t columns = 1;
		size_t consumed;

		if (st_utf8_decode_one(&codepoint, &consumed, text + offset,
		    (size_t)length - offset) != 0) {
			st_wren_abort(vm, "text is not valid UTF-8");
			return;
		}
		(void)st_console_font_lookup(st_wren_context_from_vm(vm)->console, identifier,
		    codepoint, &glyph_font, &glyph, &columns);
		width += columns;
		offset += consumed;
	}
	wrenSetSlotDouble(vm, 0, (double)width);
}

static uint32_t
capabilities(WrenVM *vm)
{
	uint32_t result = 0;

	(void)st_console_capabilities(st_wren_context_from_vm(vm)->console,
	    &result);
	return result;
}

#define SUPPORT_GETTER(name, flag) \
	static void fn_support_##name(WrenVM *vm) \
	{ \
		wrenSetSlotBool(vm, 0, (capabilities(vm) & (flag)) != 0); \
	}

SUPPORT_GETTER(loadable_fonts, ST_CONSOLE_CAP_LOADABLE_FONTS)
SUPPORT_GETTER(palette_change, ST_CONSOLE_CAP_PALETTE_CHANGE)
SUPPORT_GETTER(pixels, ST_CONSOLE_CAP_PIXELS)
SUPPORT_GETTER(custom_cursor, ST_CONSOLE_CAP_CUSTOM_CURSOR)
SUPPORT_GETTER(window_title, ST_CONSOLE_CAP_WINDOW_TITLE)
SUPPORT_GETTER(extended_palette, ST_CONSOLE_CAP_EXTENDED_PALETTE)
SUPPORT_GETTER(blocky_scaling, ST_CONSOLE_CAP_BLOCKY_SCALING)
SUPPORT_GETTER(external_scaling, ST_CONSOLE_CAP_EXTERNAL_SCALING)

#undef SUPPORT_GETTER

static void fn_support_false(WrenVM *vm) { wrenSetSlotBool(vm, 0, false); }

static void
fn_codepage_encodes(WrenVM *vm)
{
	const unsigned char *text;
	uint32_t codepoint;
	size_t consumed;
	uint8_t encoded;
	int length;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Codepage.encodes_ expects a String");
		return;
	}
	text = (const unsigned char *)wrenGetSlotBytes(vm, 1, &length);
	if (length <= 0 || st_utf8_decode_one(&codepoint, &consumed, text,
	    (size_t)length) != 0) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	wrenSetSlotBool(vm, 0, st_codepage_from_unicode(&encoded,
	    ST_CODEPAGE_CP437, codepoint));
}

static enum st_codepage
font_codepage(uint16_t selector)
{
	return selector < ST_BUILTIN_FONT_COUNT
	    ? st_builtin_font_codepage(selector) : ST_CODEPAGE_CP437;
}

static void
fn_font_name(WrenVM *vm)
{
	int64_t selector;
	const char *name = NULL;

	if (!slot_integer(vm, 1, 0, UINT8_MAX, &selector))
		return;
	if ((size_t)selector < ST_BUILTIN_FONT_COUNT)
		name = st_builtin_font_name((size_t)selector);
	else if ((size_t)selector - ST_BUILTIN_FONT_COUNT
	    < st_settings_font_count())
		name = st_settings_font_at((size_t)selector
		    - ST_BUILTIN_FONT_COUNT)->name;
	wrenSetSlotString(vm, 0, name == NULL ? "" : name);
}

static void
fn_font_count(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, (double)(ST_BUILTIN_FONT_COUNT
	    + st_wren_context_from_vm(vm)->fonts.loaded_count));
}

static void
fn_font_available(WrenVM *vm)
{
	int64_t selector;

	if (slot_integer(vm, 1, 0, UINT8_MAX, &selector))
		wrenSetSlotBool(vm, 0, st_wren_context_from_vm(vm)
		    ->fonts.catalog_valid[selector]);
}

static void
fn_font_codepage(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	wrenSetSlotDouble(vm, 0, font_codepage(context->fonts.slots[0]));
}

static void
fn_font_codepage_of(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	int64_t slot;

	if (slot_integer(vm, 1, 0, 4, &slot))
		wrenSetSlotDouble(vm, 0,
		    font_codepage(context->fonts.slots[slot]));
}

static void
fn_screen_font_get(WrenVM *vm)
{
	int64_t slot;

	if (slot_integer(vm, 1, 0, 4, &slot))
		wrenSetSlotDouble(vm, 0,
		    st_wren_context_from_vm(vm)->fonts.slots[slot]);
}

static void
fn_screen_font_set(WrenVM *vm)
{
	int64_t slot;
	int64_t font;

	if (slot_integer(vm, 1, 0, 4, &slot)
	    && slot_integer(vm, 2, 0, UINT16_MAX, &font))
		st_wren_context_from_vm(vm)->fonts.slots[slot] = (uint16_t)font;
}

static void
fn_palette_get(WrenVM *vm)
{
	int64_t index;
	uint32_t colour;

	if (!slot_integer(vm, 1, 0, 255, &index))
		return;
	if (st_console_get_palette(st_wren_context_from_vm(vm)->console,
	    (uint16_t)index, &colour, 1) != 0)
		return;
	wrenSetSlotDouble(vm, 0, colour);
}

static void
fn_palette_set(WrenVM *vm)
{
	int64_t index;
	int64_t colour;
	uint32_t value;

	if (!slot_integer(vm, 1, 0, 255, &index)
	    || !slot_integer(vm, 2, 0, 0xffffff, &colour))
		return;
	value = (uint32_t)colour;
	(void)st_console_set_palette(st_wren_context_from_vm(vm)->console,
	    (uint16_t)index, &value, 1);
}

static void
surface_make_cell(WrenVM *vm, struct foreign_surface *surface, size_t index)
{
	if (surface == NULL || surface->storage == NULL
	    || index >= surface->storage->count) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	struct surface_storage *storage = surface->storage;
	wrenEnsureSlots(vm, 3);
	wrenGetVariable(vm, "syncterm", "Cell", 2);
	struct foreign_cell *cell = wrenSetSlotNewForeign(vm, 0, 2,
	    sizeof(*cell));
	cell->header.kind = FOREIGN_CELL;
	cell->storage = storage;
	cell->index = index;
	++storage->references;
}

static struct foreign_surface *
surface_value(WrenVM *vm, int slot)
{
	return (struct foreign_surface *)foreign_kind(vm, slot, FOREIGN_SURFACE);
}

static void
fn_surface_count(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);

	if (surface != NULL)
		wrenSetSlotDouble(vm, 0, (double)surface->storage->count);
}

static void
fn_surface_get(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	int64_t index;

	if (surface != NULL && slot_integer(vm, 1, 0,
	    surface->storage->count == 0 ? 0 :
	    (int64_t)surface->storage->count - 1, &index))
		surface_make_cell(vm, surface, (size_t)index);
}

static void
fn_surface_iterate(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	int64_t next;

	if (surface == NULL)
		return;
	if (surface->storage->count == 0) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (wrenGetSlotType(vm, 1) == WREN_TYPE_NULL) {
		wrenSetSlotDouble(vm, 0, 0);
		return;
	}
	if (!slot_integer(vm, 1, 0, INT64_MAX - 1, &next))
		return;
	++next;
	if ((uint64_t)next >= surface->storage->count)
		wrenSetSlotBool(vm, 0, false);
	else
		wrenSetSlotDouble(vm, 0, (double)next);
}

static void fn_surface_iterator_value(WrenVM *vm) { fn_surface_get(vm); }

static void
fn_surface_width(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);

	if (surface != NULL)
		wrenSetSlotDouble(vm, 0, surface->storage->width);
}

static void
fn_surface_height(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);

	if (surface != NULL)
		wrenSetSlotDouble(vm, 0, surface->storage->height);
}

static void
fn_surface_cell_at(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	int64_t x;
	int64_t y;

	if (surface == NULL || !slot_integer(vm, 1, INT32_MIN, INT32_MAX, &x)
	    || !slot_integer(vm, 2, INT32_MIN, INT32_MAX, &y))
		return;
	if (x < 0 || y < 0 || (uint64_t)x >= surface->storage->width
	    || (uint64_t)y >= surface->storage->height) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	surface_make_cell(vm, surface,
	    (size_t)y * surface->storage->width + (size_t)x);
}

static void
fn_surface_cell_at_into(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	struct foreign_cell *cell;
	struct surface_storage *storage;
	int64_t x;
	int64_t y;

	if (surface == NULL || !slot_integer(vm, 1, INT32_MIN, INT32_MAX, &x)
	    || !slot_integer(vm, 2, INT32_MIN, INT32_MAX, &y))
		return;
	cell = foreign_cell_value(vm, 3);
	if (cell == NULL)
		return;
	if (x < 0 || y < 0 || (uint64_t)x >= surface->storage->width
	    || (uint64_t)y >= surface->storage->height) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	storage = surface->storage;
	if (cell->storage != storage) {
		++storage->references;
		storage_release(cell->storage);
		cell->storage = storage;
	}
	cell->index = (size_t)y * storage->width + (size_t)x;
	wrenSetSlotBool(vm, 0, true);
}

static void
surface_copy(WrenVM *vm, bool partial)
{
	struct foreign_surface *destination = surface_value(vm, 0);
	struct foreign_surface *source = surface_value(vm, 1);
	struct st_console_rect source_rect;
	struct st_console_rect destination_rect;
	int64_t values[6];

	if (destination == NULL || source == NULL)
		return;
	if (partial) {
		for (size_t i = 0; i < 6; ++i) {
			if (!slot_integer(vm, (int)i + 2, INT32_MIN, INT32_MAX,
			    &values[i]))
				return;
		}
	}
	else {
		values[0] = 0;
		values[1] = 0;
		values[2] = source->storage->width;
		values[3] = source->storage->height;
		if (!slot_integer(vm, 2, INT32_MIN, INT32_MAX, &values[4])
		    || !slot_integer(vm, 3, INT32_MIN, INT32_MAX, &values[5]))
			return;
	}
	if (!clip_blit(source->storage, values[0], values[1], values[2],
	    values[3], values[4], values[5], destination->storage->width,
	    destination->storage->height, &source_rect, &destination_rect))
		return;
	for (uint32_t row = 0; row < source_rect.height; ++row) {
		size_t destination_index = (size_t)(destination_rect.y + row)
		    * destination->storage->width + destination_rect.x;
		struct foreign_cell boundary = {
			.header.kind = FOREIGN_CELL,
			.storage = destination->storage,
			.index = destination_index,
		};

		clear_cell_pair(vm, &boundary);
		boundary.index += destination_rect.width - 1;
		clear_cell_pair(vm, &boundary);
		memmove(&destination->storage->cells[
		    destination_index],
		    &source->storage->cells[(size_t)(source_rect.y + row)
		    * source->storage->width + source_rect.x],
		    (size_t)source_rect.width * sizeof(struct st_console_cell));
		boundary.index = destination_index;
		if ((destination->storage->cells[boundary.index].flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0)
			clear_cell_pair(vm, &boundary);
		boundary.index = destination_index + destination_rect.width - 1;
		{
			struct st_console_cell *last =
			    &destination->storage->cells[boundary.index];

			if (resolved_cell_columns(vm, last) == 2 && (last->flags
			    & ST_CONSOLE_CELL_CONTINUATION) == 0)
				clear_cell_pair(vm, &boundary);
		}
	}
}

static void fn_surface_put(WrenVM *vm) { surface_copy(vm, false); }
static void fn_surface_put_partial(WrenVM *vm) { surface_copy(vm, true); }

static void
fn_surface_fill(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	struct st_console_cell *cell;
	int64_t values[4];
	int64_t left;
	int64_t top;
	int64_t right;
	int64_t bottom;

	if (surface == NULL)
		return;
	for (size_t i = 0; i < 4; ++i) {
		if (!slot_integer(vm, (int)i + 1, INT32_MIN, INT32_MAX,
		    &values[i]))
			return;
	}
	cell = cell_value(vm, 5);
	if (cell == NULL)
		return;
	{
		if (resolved_cell_columns(vm, cell) != 1 || (cell->flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0) {
			st_wren_abort(vm,
			    "Surface.fill requires a one-column Cell");
			return;
		}
	}
	left = values[0] < 0 ? 0 : values[0];
	top = values[1] < 0 ? 0 : values[1];
	right = values[2] <= 0 ? left : left + values[2];
	bottom = values[3] <= 0 ? top : top + values[3];
	if (right > surface->storage->width)
		right = surface->storage->width;
	if (bottom > surface->storage->height)
		bottom = surface->storage->height;
	for (int64_t y = top; y < bottom; ++y) {
		for (int64_t x = left; x < right; ++x)
			surface->storage->cells[(size_t)y * surface->storage->width
			    + (size_t)x] = *cell;
	}
}

static void
fn_surface_to_string(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	char text[64];

	if (surface == NULL)
		return;
	(void)snprintf(text, sizeof(text), "Surface(%ux%u)",
	    surface->storage->width, surface->storage->height);
	wrenSetSlotString(vm, 0, text);
}

static void
fn_cell_ch(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	char encoded[4];
	uint32_t codepoint;
	ptrdiff_t length;

	if (cell == NULL)
		return;
	codepoint = cell->codepoint;
	length = st_utf8_encode_one(encoded, sizeof(encoded), codepoint);
	if (length < 0)
		wrenSetSlotString(vm, 0, "?");
	else
		wrenSetSlotBytes(vm, 0, encoded, (size_t)length);
}

static void
fn_cell_ch_set(WrenVM *vm)
{
	struct foreign_cell *foreign = foreign_cell_value(vm, 0);
	const unsigned char *text;
	uint32_t codepoint;
	size_t consumed;
	int length;

	if (foreign == NULL || wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		if (foreign != NULL)
			st_wren_abort(vm, "Cell.ch expects a String");
		return;
	}
	text = (const unsigned char *)wrenGetSlotBytes(vm, 1, &length);
	if (length == 0 || st_utf8_decode_one(&codepoint, &consumed,
	    text, (size_t)length) != 0) {
		st_wren_abort(vm, "Cell.ch expects one valid Unicode codepoint");
		return;
	}
	set_cell_codepoint(vm, foreign, codepoint, false, 0);
}

static void
fn_cell_byte(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	uint8_t value;

	if (cell == NULL)
		return;
	if (!st_codepage_from_unicode(&value, ST_CODEPAGE_CP437,
	    cell->codepoint))
		value = '?';
	wrenSetSlotDouble(vm, 0, value);
}

static void
fn_cell_byte_set(WrenVM *vm)
{
	struct foreign_cell *foreign = foreign_cell_value(vm, 0);
	int64_t value;

	if (foreign != NULL && slot_integer(vm, 1, 0, 255, &value))
		set_cell_codepoint(vm, foreign,
		    st_codepage_to_unicode(ST_CODEPAGE_CP437,
		    (uint8_t)value, true), true,
		    (uint8_t)value);
}

static void
fn_cell_font(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotDouble(vm, 0, st_wren_font_selector(
		    st_wren_context_from_vm(vm), cell->font));
}

static void
fn_cell_font_set(WrenVM *vm)
{
	struct foreign_cell *foreign = foreign_cell_value(vm, 0);
	int64_t value;
	uint16_t identifier;

	if (foreign != NULL && slot_integer(vm, 1, 0, UINT8_MAX, &value)) {
		if (!st_wren_font_identifier(st_wren_context_from_vm(vm),
		    (uint16_t)value, &identifier)) {
			st_wren_abort(vm, "font is not available");
			return;
		}
		foreign->storage->cells[foreign->index].font = identifier;
		set_cell_codepoint(vm, foreign,
		    foreign->storage->cells[foreign->index].codepoint, false, 0);
	}
}

static void
fn_cell_columns(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell == NULL)
		return;
	if ((cell->flags & ST_CONSOLE_CELL_CONTINUATION) != 0) {
		wrenSetSlotDouble(vm, 0, 0);
		return;
	}
	wrenSetSlotDouble(vm, 0, resolved_cell_columns(vm, cell));
}

static uint8_t
cell_legacy_attr(const struct st_console_cell *cell)
{
	uint8_t value = (uint8_t)((cell->foreground & 0x0f)
	    | ((cell->background & 0x07) << 4));

	if ((cell->flags & ST_CONSOLE_CELL_BLINK) != 0)
		value |= 0x80;
	return value;
}

static void
fn_cell_attr(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotDouble(vm, 0, cell_legacy_attr(cell));
}

static void
fn_cell_attr_set(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	int64_t value;

	if (cell == NULL || !slot_integer(vm, 1, 0, 255, &value))
		return;
	cell->foreground = (uint32_t)value & 0x0f;
	cell->background = ((uint32_t)value >> 4) & 0x07;
	if (((uint32_t)value & 0x80) != 0)
		cell->flags |= ST_CONSOLE_CELL_BLINK;
	else
		cell->flags &= ~ST_CONSOLE_CELL_BLINK;
}

static void
fn_cell_bright(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotBool(vm, 0, !ST_CONSOLE_COLOR_IS_RGB(cell->foreground)
		    && (cell->foreground & 8) != 0);
}

static void
fn_cell_bright_set(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell == NULL || wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL)
		return;
	if (!ST_CONSOLE_COLOR_IS_RGB(cell->foreground)) {
		if (wrenGetSlotBool(vm, 1))
			cell->foreground |= 8;
		else
			cell->foreground &= ~UINT32_C(8);
	}
}

static void
fn_cell_blink(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotBool(vm, 0,
		    (cell->flags & ST_CONSOLE_CELL_BLINK) != 0);
}

static void
fn_cell_blink_set(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell == NULL || wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL)
		return;
	if (wrenGetSlotBool(vm, 1))
		cell->flags |= ST_CONSOLE_CELL_BLINK;
	else
		cell->flags &= ~ST_CONSOLE_CELL_BLINK;
}

static void
fn_cell_pixel_graphics(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotBool(vm, 0,
		    (cell->flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0);
}

static void
cell_palette_get(WrenVM *vm, bool foreground)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	uint32_t value;

	if (cell == NULL)
		return;
	value = foreground ? cell->foreground : cell->background;
	if (ST_CONSOLE_COLOR_IS_RGB(value))
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotDouble(vm, 0, value & 0xff);
}

static void
cell_palette_set(WrenVM *vm, bool foreground)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	int64_t value;

	if (cell == NULL || !slot_integer(vm, 1, 0, 255, &value))
		return;
	if (foreground)
		cell->foreground = (uint32_t)value;
	else
		cell->background = (uint32_t)value;
}

static void
cell_rgb_get(WrenVM *vm, bool foreground)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	uint32_t value;

	if (cell == NULL)
		return;
	value = foreground ? cell->foreground : cell->background;
	if (!ST_CONSOLE_COLOR_IS_RGB(value))
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotDouble(vm, 0, value & 0xffffff);
}

static void
cell_rgb_set(WrenVM *vm, bool foreground)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	int64_t value;

	if (cell == NULL || !slot_integer(vm, 1, 0, 0xffffff, &value))
		return;
	if (foreground)
		cell->foreground = ST_CONSOLE_COLOR_RGB_TAG | (uint32_t)value;
	else
		cell->background = ST_CONSOLE_COLOR_RGB_TAG | (uint32_t)value;
}

static void fn_cell_fg_palette(WrenVM *vm) { cell_palette_get(vm, true); }
static void fn_cell_fg_palette_set(WrenVM *vm) { cell_palette_set(vm, true); }
static void fn_cell_bg_palette(WrenVM *vm) { cell_palette_get(vm, false); }
static void fn_cell_bg_palette_set(WrenVM *vm) { cell_palette_set(vm, false); }
static void fn_cell_fg_rgb(WrenVM *vm) { cell_rgb_get(vm, true); }
static void fn_cell_fg_rgb_set(WrenVM *vm) { cell_rgb_set(vm, true); }
static void fn_cell_bg_rgb(WrenVM *vm) { cell_rgb_get(vm, false); }
static void fn_cell_bg_rgb_set(WrenVM *vm) { cell_rgb_set(vm, false); }

static void
fn_cell_hyperlink(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotDouble(vm, 0, cell->hyperlink);
}

static void
fn_cell_hyperlink_set(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	int64_t value;

	if (cell != NULL && slot_integer(vm, 1, 0, UINT16_MAX, &value))
		cell->hyperlink = (uint16_t)value;
}

static void
fn_cell_equal(WrenVM *vm)
{
	struct st_console_cell *left = cell_value(vm, 0);
	struct st_console_cell *right = cell_value(vm, 1);

	if (left != NULL && right != NULL)
		wrenSetSlotBool(vm, 0, left->codepoint == right->codepoint
		    && left->glyph == right->glyph
		    && left->foreground == right->foreground
		    && left->background == right->background
		    && (left->flags & ST_CONSOLE_CELL_PUBLIC_FLAGS)
		    == (right->flags & ST_CONSOLE_CELL_PUBLIC_FLAGS)
		    && left->font == right->font
		    && left->hyperlink == right->hyperlink);
}

static void
fn_cell_to_string(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	char text[80];

	if (cell == NULL)
		return;
	(void)snprintf(text, sizeof(text), "Cell(ch=%u attr=0x%02X font=%u)",
	    cell->codepoint, cell_legacy_attr(cell), cell->font);
	wrenSetSlotString(vm, 0, text);
}

static void
fn_color_from_rgb(WrenVM *vm)
{
	int64_t red;
	int64_t green;
	int64_t blue;

	if (slot_integer(vm, 1, 0, 255, &red)
	    && slot_integer(vm, 2, 0, 255, &green)
	    && slot_integer(vm, 3, 0, 255, &blue))
		wrenSetSlotDouble(vm, 0,
		    (double)((red << 16) | (green << 8) | blue));
}

static void
fn_color_from_attr(WrenVM *vm)
{
	int64_t value;

	if (!slot_integer(vm, 1, 0, 255, &value))
		return;
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, value & 0x0f);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, (value >> 4) & 0x07);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_color_to_attr(WrenVM *vm)
{
	int64_t foreground;
	int64_t background;

	if (slot_integer(vm, 1, 0, 15, &foreground)
	    && slot_integer(vm, 2, 0, 15, &background))
		wrenSetSlotDouble(vm, 0, foreground | (background << 4));
}

static void
fn_clipboard_get(WrenVM *vm)
{
	char *text = NULL;

	if (st_console_get_clipboard_text(
	    st_wren_context_from_vm(vm)->console, &text) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenSetSlotString(vm, 0, text);
	st_console_free_text(text);
}

static void
fn_clipboard_set(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Clipboard.text expects a String");
		return;
	}
	(void)st_console_set_clipboard_text(
	    st_wren_context_from_vm(vm)->console, wrenGetSlotString(vm, 1));
}

static void
fn_cursor_preset(WrenVM *vm)
{
	int64_t preset;

	if (!slot_integer(vm, 1, 0, 2, &preset))
		return;
	wrenEnsureSlots(vm, 3);
	wrenGetVariable(vm, "syncterm", "CustomCursor", 2);
	struct foreign_cursor *cursor = wrenSetSlotNewForeign(vm, 0, 2,
	    sizeof(*cursor));
	cursor->header.kind = FOREIGN_CURSOR;
	if (st_console_get_default_cursor(st_wren_context_from_vm(vm)->console, &cursor->cursor)
	    != 0) {
		cursor->cursor = (struct st_console_custom_cursor){
			.start = 6, .end = 7, .range = 8, .visible = true,
			.blink = true,
		};
	}
	if (preset != 2) {
		cursor->cursor.start = 0;
		cursor->cursor.end = cursor->cursor.range - 1;
		cursor->cursor.visible = preset != 0;
		cursor->cursor.blink = false;
	}
}

static struct foreign_cursor *
cursor_value(WrenVM *vm)
{
	return (struct foreign_cursor *)foreign_kind(vm, 0, FOREIGN_CURSOR);
}

#define CURSOR_NUMBER(name, field) \
	static void fn_cursor_##name(WrenVM *vm) \
	{ \
		struct foreign_cursor *cursor = cursor_value(vm); \
		if (cursor != NULL) \
			wrenSetSlotDouble(vm, 0, cursor->cursor.field); \
	} \
	static void fn_cursor_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_cursor *cursor = cursor_value(vm); \
		int64_t value; \
		if (cursor != NULL && slot_integer(vm, 1, 0, UINT32_MAX, &value)) \
			cursor->cursor.field = (uint32_t)value; \
	}

#define CURSOR_BOOL(name, field) \
	static void fn_cursor_##name(WrenVM *vm) \
	{ \
		struct foreign_cursor *cursor = cursor_value(vm); \
		if (cursor != NULL) \
			wrenSetSlotBool(vm, 0, cursor->cursor.field); \
	} \
	static void fn_cursor_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_cursor *cursor = cursor_value(vm); \
		if (cursor != NULL && wrenGetSlotType(vm, 1) == WREN_TYPE_BOOL) \
			cursor->cursor.field = wrenGetSlotBool(vm, 1); \
	}

CURSOR_NUMBER(start, start)
CURSOR_NUMBER(end, end)
CURSOR_NUMBER(range, range)
CURSOR_BOOL(blink, blink)
CURSOR_BOOL(visible, visible)

#undef CURSOR_NUMBER
#undef CURSOR_BOOL

static void
fn_cursor_apply(WrenVM *vm)
{
	struct foreign_cursor *cursor = cursor_value(vm);

	if (cursor != NULL)
		(void)st_console_set_custom_cursor(
		    st_wren_context_from_vm(vm)->console, &cursor->cursor);
}

static void
fn_host_exit_requested(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_host_from_vm(vm)->exit_requested);
}

static void
fn_host_theme_generation(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, (double)st_theme_generation(
	    st_wren_host_from_vm(vm)->themes));
}

static void
push_optional_number(WrenVM *vm, int slot, int value)
{
	if (value < 0)
		wrenSetSlotNull(vm, slot);
	else
		wrenSetSlotDouble(vm, slot, value);
}

static void
push_style(WrenVM *vm, int list_slot, const struct st_theme_style *style)
{
	wrenSetSlotNewList(vm, 2);
	wrenSetSlotString(vm, 3, style->role);
	wrenInsertInList(vm, 2, -1, 3);
	push_optional_number(vm, 3, style->font);
	wrenInsertInList(vm, 2, -1, 3);
	push_optional_number(vm, 3, style->legacy_attr);
	wrenInsertInList(vm, 2, -1, 3);
	push_optional_number(vm, 3, style->foreground);
	wrenInsertInList(vm, 2, -1, 3);
	push_optional_number(vm, 3, style->background);
	wrenInsertInList(vm, 2, -1, 3);
	wrenInsertInList(vm, list_slot, -1, 2);
}

static void
push_theme_object(WrenVM *vm, const struct st_theme *theme)
{
	if (theme == NULL) {
		struct st_theme_manager *manager =
		    st_wren_host_from_vm(vm)->themes;

		theme = st_theme_active(manager);
		if (theme == NULL)
			theme = st_theme_default(manager);
		if (theme == NULL) {
			st_wren_abort(vm, "theme data is unavailable");
			return;
		}
	}
	wrenEnsureSlots(vm, 5);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotNewList(vm, 1);
	for (size_t i = 0; i < theme->style_count; ++i) {
		const struct st_theme_style *style = &theme->styles[i];

		push_style(vm, 1, style);
	}
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotNewList(vm, 1);
	for (size_t i = 0; i < theme->glyph_count; ++i) {
		char encoded[4];
		ptrdiff_t length = st_utf8_encode_one(encoded, sizeof(encoded),
		    theme->glyphs[i].codepoint);
		char ascii = (char)theme->glyphs[i].ascii;

		wrenSetSlotNewList(vm, 2);
		wrenSetSlotString(vm, 3, theme->glyphs[i].name);
		wrenInsertInList(vm, 2, -1, 3);
		wrenSetSlotBytes(vm, 3, encoded, (size_t)length);
		wrenInsertInList(vm, 2, -1, 3);
		wrenSetSlotBytes(vm, 3, &ascii, 1);
		wrenInsertInList(vm, 2, -1, 3);
		wrenInsertInList(vm, 1, -1, 2);
	}
	wrenInsertInList(vm, 0, -1, 1);
}

void
st_wren_push_theme_data(WrenVM *vm, const struct st_theme *theme)
{
	push_theme_object(vm, theme);
}

static void
fn_host_theme_data(WrenVM *vm)
{
	push_theme_object(vm, st_theme_active(st_wren_host_from_vm(vm)->themes));
}

static void
fn_host_default_theme_data(WrenVM *vm)
{
	push_theme_object(vm, st_theme_default(st_wren_host_from_vm(vm)->themes));
}

static void
fn_host_null(WrenVM *vm)
{
	wrenSetSlotNull(vm, 0);
}

static void
fn_host_safe_mode(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_host_from_vm(vm)->config.safe_mode);
}

static void
fn_host_text_terminal(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0,
	    (capabilities(vm) & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0);
}

static void
fn_host_local_control_keys(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0,
	    (capabilities(vm) & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0);
}

static void
fn_host_alt_name(WrenVM *vm)
{
#ifdef __APPLE__
	wrenSetSlotString(vm, 0, "Command");
#else
	wrenSetSlotString(vm, 0, "Alt");
#endif
}

static void
fn_host_alt_short(WrenVM *vm)
{
#ifdef __APPLE__
	wrenSetSlotString(vm, 0, "Cmd");
#else
	wrenSetSlotString(vm, 0, "Alt");
#endif
}

static void
fn_host_print(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "Host.print expects a String");
		return;
	}
	puts(wrenGetSlotString(vm, 1));
	fflush(stdout);
}

static void
fn_timer_now(WrenVM *vm)
{
	uint64_t milliseconds;

	if (st_time_monotonic_ms(&milliseconds) != 0)
		wrenSetSlotDouble(vm, 0, 0);
	else
		wrenSetSlotDouble(vm, 0, (double)milliseconds / 1000.0);
}

static void
fn_format_bytes(WrenVM *vm)
{
	double value = wrenGetSlotDouble(vm, 1);
	static const char *const suffixes[] = {"B", "K", "M", "G", "T", "P"};
	size_t suffix = 0;
	char text[64];

	while (fabs(value) >= 1024.0 && suffix + 1 < sizeof(suffixes) /
	    sizeof(suffixes[0])) {
		value /= 1024.0;
		++suffix;
	}
	(void)snprintf(text, sizeof(text), "%.1f %s", value, suffixes[suffix]);
	wrenSetSlotString(vm, 0, text);
}

static void
fn_format_duration(WrenVM *vm)
{
	double value = wrenGetSlotDouble(vm, 1);
	const char *suffix = "s";
	char text[64];

	if (fabs(value) >= 31557600.0) {
		value /= 31557600.0;
		suffix = "y";
	}
	else if (fabs(value) >= 86400.0) {
		value /= 86400.0;
		suffix = "d";
	}
	else if (fabs(value) >= 3600.0) {
		value /= 3600.0;
		suffix = "h";
	}
	else if (fabs(value) >= 60.0) {
		value /= 60.0;
		suffix = "m";
	}
	(void)snprintf(text, sizeof(text), "%.1f %s", value, suffix);
	wrenSetSlotString(vm, 0, text);
}

static void
fn_cterm_status_display(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, 0);
}

static const struct binding bindings[] = {
	{"Input", true, "next()", fn_input_next},
	{"Input", true, "next(_)", fn_input_next_timed},
	{"Input", true, "poll()", fn_input_poll},
	{"Input", true, "nextForWidget_(_)", fn_input_next_widget},
	{"Input", true, "mouseEvents", fn_input_mouse_events},
	{"Input", true, "mouseEvents=(_)", fn_input_mouse_events_set},
	{"Input", true, "enableMouseEvent(_)", fn_input_mouse_event_enable},
	{"Input", true, "disableMouseEvent(_)", fn_input_mouse_event_disable},
	{"Input", true, "mouseVisible=(_)", fn_input_mouse_visible_set},
	{"Input", true, "mousedrag()", fn_input_mouse_drag},
	{"Input", true, "mousedrag(_)", fn_input_mouse_drag},
	{"KeyEvent", false, "code", fn_key_code},
	{"KeyEvent", false, "codepoint", fn_key_codepoint},
	{"KeyEvent", false, "text", fn_key_text},
	{"KeyEvent", false, "toString", fn_key_to_string},
	{"PhysicalKeyEvent", false, "evdev", fn_physical_evdev},
	{"PhysicalKeyEvent", false, "pressed", fn_physical_pressed},
	{"MouseEvent", false, "event", fn_mouse_event},
	{"MouseEvent", false, "bstate", fn_mouse_buttons},
	{"MouseEvent", false, "modifiers", fn_mouse_modifiers},
	{"MouseEvent", false, "startX", fn_mouse_start_x},
	{"MouseEvent", false, "startY", fn_mouse_start_y},
	{"MouseEvent", false, "endX", fn_mouse_end_x},
	{"MouseEvent", false, "endY", fn_mouse_end_y},
	{"MouseEvent", false, "startPixelX", fn_mouse_start_pixel_x},
	{"MouseEvent", false, "startPixelY", fn_mouse_start_pixel_y},
	{"MouseEvent", false, "endPixelX", fn_mouse_end_pixel_x},
	{"MouseEvent", false, "endPixelY", fn_mouse_end_pixel_y},
	{"MouseEvent", false, "toString", fn_mouse_to_string},
	{"Screen", true, "size", fn_screen_size},
	{"Screen", true, "save()", fn_screen_save},
	{"Screen", true, "restore(_)", fn_screen_restore},
	{"Screen", true, "readRect(_,_,_,_)", fn_screen_read_rect},
	{"Screen", true, "putRect(_,_,_)", fn_screen_put_rect},
	{"Screen", true, "putRect_(_,_,_,_,_,_,_)", fn_screen_put_rect_partial},
	{"Screen", true, "attr", fn_screen_attr},
	{"Screen", true, "attr=(_)", fn_screen_attr_set},
	{"Screen", true, "pixelSize", fn_screen_pixel_size},
	{"Screen", true, "cellPixelSize", fn_screen_cell_pixel_size},
	{"Screen", true, "textWidth(_,_)", fn_screen_text_width},
	{"ScreenWindow", true, "position", fn_screen_window_position},
	{"ScreenWindow", true, "position=(_)", fn_screen_window_position_set},
	{"ScreenWindow", true, "bounds", fn_screen_window_bounds},
	{"ScreenWindow", true, "bounds=(_)", fn_screen_window_bounds_set},
	{"ScreenWindow", true, "clear()", fn_screen_window_clear},
	{"ScreenWindow", true, "clearToLineEnd()", fn_screen_window_clear_line},
	{"ScreenWindow", true, "deleteLine()", fn_screen_window_delete_line},
	{"ScreenWindow", true, "insertLine()", fn_screen_window_insert_line},
	{"ScreenWindow", true, "scroll()", fn_screen_window_scroll},
	{"ScreenWindow", true, "putChar(_)", fn_screen_window_put_char},
	{"ScreenWindow", true, "print(_)", fn_screen_window_print},
	{"ScreenSupports", true, "loadableFonts", fn_support_loadable_fonts},
	{"ScreenSupports", true, "altBlinkFont", fn_support_false},
	{"ScreenSupports", true, "altBoldFont", fn_support_false},
	{"ScreenSupports", true, "brightBackground", fn_support_false},
	{"ScreenSupports", true, "paletteChange", fn_support_palette_change},
	{"ScreenSupports", true, "pixels", fn_support_pixels},
	{"ScreenSupports", true, "customCursor", fn_support_custom_cursor},
	{"ScreenSupports", true, "fontSelection", fn_support_loadable_fonts},
	{"ScreenSupports", true, "windowTitle", fn_support_window_title},
	{"ScreenSupports", true, "windowName", fn_support_false},
	{"ScreenSupports", true, "windowIcon", fn_support_false},
	{"ScreenSupports", true, "extendedPalette", fn_support_extended_palette},
	{"ScreenSupports", true, "blockyScaling", fn_support_blocky_scaling},
	{"ScreenSupports", true, "externalScaling", fn_support_external_scaling},
	{"ScreenSupports", true, "closeLock", fn_support_false},
	{"ScreenFonts", true, "[_]", fn_screen_font_get},
	{"ScreenFonts", true, "[_]=(_)", fn_screen_font_set},
	{"Codepage", true, "encodes_(_)", fn_codepage_encodes},
	{"Font", true, "name(_)", fn_font_name},
	{"Font", true, "count", fn_font_count},
	{"Font", true, "available(_)", fn_font_available},
	{"Font", true, "codepage", fn_font_codepage},
	{"Font", true, "codepageOf(_)", fn_font_codepage_of},
	{"Palette", true, "[_]", fn_palette_get},
	{"Palette", true, "[_]=(_)", fn_palette_set},
	{"Surface", false, "count", fn_surface_count},
	{"Surface", false, "[_]", fn_surface_get},
	{"Surface", false, "iterate(_)", fn_surface_iterate},
	{"Surface", false, "iteratorValue(_)", fn_surface_iterator_value},
	{"Surface", false, "width", fn_surface_width},
	{"Surface", false, "height", fn_surface_height},
	{"Surface", false, "cellAt(_,_)", fn_surface_cell_at},
	{"Surface", false, "cellAt(_,_,_)", fn_surface_cell_at_into},
	{"Surface", false, "putRect(_,_,_)", fn_surface_put},
	{"Surface", false, "putRect_(_,_,_,_,_,_,_)", fn_surface_put_partial},
	{"Surface", false, "fill_(_,_,_,_,_)", fn_surface_fill},
	{"Surface", false, "toString", fn_surface_to_string},
	{"Cell", false, "ch", fn_cell_ch},
	{"Cell", false, "ch=(_)", fn_cell_ch_set},
	{"Cell", false, "chByte", fn_cell_byte},
	{"Cell", false, "chByte=(_)", fn_cell_byte_set},
	{"Cell", false, "font", fn_cell_font},
	{"Cell", false, "font=(_)", fn_cell_font_set},
	{"Cell", false, "columns", fn_cell_columns},
	{"Cell", false, "legacyAttr", fn_cell_attr},
	{"Cell", false, "legacyAttr=(_)", fn_cell_attr_set},
	{"Cell", false, "bright", fn_cell_bright},
	{"Cell", false, "bright=(_)", fn_cell_bright_set},
	{"Cell", false, "blink", fn_cell_blink},
	{"Cell", false, "blink=(_)", fn_cell_blink_set},
	{"Cell", false, "pixelGraphics", fn_cell_pixel_graphics},
	{"Cell", false, "fgPalette", fn_cell_fg_palette},
	{"Cell", false, "fgPalette=(_)", fn_cell_fg_palette_set},
	{"Cell", false, "bgPalette", fn_cell_bg_palette},
	{"Cell", false, "bgPalette=(_)", fn_cell_bg_palette_set},
	{"Cell", false, "fgRgb", fn_cell_fg_rgb},
	{"Cell", false, "fgRgb=(_)", fn_cell_fg_rgb_set},
	{"Cell", false, "bgRgb", fn_cell_bg_rgb},
	{"Cell", false, "bgRgb=(_)", fn_cell_bg_rgb_set},
	{"Cell", false, "hyperlinkId", fn_cell_hyperlink},
	{"Cell", false, "hyperlinkId=(_)", fn_cell_hyperlink_set},
	{"Cell", false, "eqContent(_)", fn_cell_equal},
	{"Cell", false, "toString", fn_cell_to_string},
	{"Color", true, "fromRgb(_,_,_)", fn_color_from_rgb},
	{"Color", true, "fromAttr(_)", fn_color_from_attr},
	{"Color", true, "toLegacyAttr(_,_)", fn_color_to_attr},
	{"Clipboard", true, "text", fn_clipboard_get},
	{"Clipboard", true, "text=(_)", fn_clipboard_set},
	{"CustomCursor", true, "presetLegacy_(_)", fn_cursor_preset},
	{"CustomCursor", false, "startLine", fn_cursor_start},
	{"CustomCursor", false, "startLine=(_)", fn_cursor_start_set},
	{"CustomCursor", false, "endLine", fn_cursor_end},
	{"CustomCursor", false, "endLine=(_)", fn_cursor_end_set},
	{"CustomCursor", false, "range", fn_cursor_range},
	{"CustomCursor", false, "range=(_)", fn_cursor_range_set},
	{"CustomCursor", false, "blink", fn_cursor_blink},
	{"CustomCursor", false, "blink=(_)", fn_cursor_blink_set},
	{"CustomCursor", false, "visible", fn_cursor_visible},
	{"CustomCursor", false, "visible=(_)", fn_cursor_visible_set},
	{"CustomCursor", false, "apply()", fn_cursor_apply},
	{"Host", true, "exitRequested_", fn_host_exit_requested},
	{"Host", true, "themeGeneration", fn_host_theme_generation},
	{"Host", true, "themeData", fn_host_theme_data},
	{"Host", true, "defaultThemeData", fn_host_default_theme_data},
	{"Host", true, "cacheDirectory", fn_host_null},
	{"Host", true, "downloadDir", fn_host_null},
	{"Host", true, "uploadPath", fn_host_null},
	{"Host", true, "safeMode", fn_host_safe_mode},
	{"Host", true, "textTerminal", fn_host_text_terminal},
	{"Host", true, "localControlKeys", fn_host_local_control_keys},
	{"Host", true, "altKeyName", fn_host_alt_name},
	{"Host", true, "altKeyShort", fn_host_alt_short},
	{"Host", true, "print(_)", fn_host_print},
	{"Timer", true, "now", fn_timer_now},
	{"Format", true, "bytes(_)", fn_format_bytes},
	{"Format", true, "duration(_)", fn_format_duration},
	{"CTerm", true, "statusDisplay", fn_cterm_status_display},
};

WrenForeignMethodFn
st_wren_core_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
		if (bindings[i].is_static == is_static
		    && strcmp(bindings[i].class_name, class_name) == 0
		    && strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	}
	return NULL;
}

WrenForeignClassMethods
st_wren_core_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {allocate_generic, finalize_foreign};

	if (strcmp(class_name, "Cell") == 0)
		methods.allocate = allocate_cell;
	else if (strcmp(class_name, "Surface") == 0)
		methods.allocate = allocate_surface;
	else if (strcmp(class_name, "KeyEvent") == 0)
		methods.allocate = allocate_key_event;
	else if (strcmp(class_name, "PhysicalKeyEvent") == 0)
		methods.allocate = allocate_physical_key_event;
	else if (strcmp(class_name, "MouseEvent") == 0)
		methods.allocate = allocate_mouse_event;
	else if (strcmp(class_name, "CustomCursor") == 0)
		methods.allocate = allocate_cursor;
	return methods;
}
