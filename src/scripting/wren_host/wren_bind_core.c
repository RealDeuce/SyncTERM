/* Wren bindings for the console, input, and menu-safe host facilities. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"
#include "st_console_test.h"

#include "io_error.h"
#include "st_app_paths.h"
#include "st_builtin_fonts.h"
#include "st_console.h"
#include "st_connection.h"
#include "st_codepage.h"
#include "st_emoji_catalog.h"
#include "st_emoji_locale.h"
#include "st_language.h"
#include "string_compat.h"
#include "st_cterm.h"
#include "st_ooii.h"
#include "st_settings.h"
#include "st_session_history.h"
#include "st_theme.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "st_unicode_compare.h"
#include "st_unicode_width.h"
#include "st_utf8.h"
#include "syncterm_config.h"
#include "value_convert.h"
#include "wren_vm.h"
#include "wren_value.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SYNCTERM_PLATFORM_POSIX
#include <sys/utsname.h>
#endif

enum foreign_kind {
	FOREIGN_CELL,
	FOREIGN_SURFACE,
	FOREIGN_SNAPSHOT,
	FOREIGN_KEY_EVENT,
	FOREIGN_PHYSICAL_KEY_EVENT,
	FOREIGN_MOUSE_EVENT,
	FOREIGN_CLAIM_HANDLE,
	FOREIGN_CURSOR,
	FOREIGN_VIDEO_FLAGS,
	FOREIGN_FONT_ERROR,
	FOREIGN_SCREEN_ERROR,
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
	struct st_pixel_buffer **pixels;
	uint32_t pixel_cell_width, pixel_cell_height;
	struct st_wren_host *host;
	struct surface_storage *previous;
	struct surface_storage *next;
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
	uint16_t font_slots[5];
};

struct foreign_key_event {
	struct foreign_header header;
	struct st_console_event native;
};

struct foreign_physical_key_event {
	struct foreign_header header;
	uint16_t evdev;
	uint32_t modifiers;
	bool pressed;
	bool repeat;
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
	int32_t wheel_x;
	int32_t wheel_y;
};

struct st_wren_input_claim {
	WrenHandle *fiber;
	WrenHandle *function;
	uint64_t id;
	struct st_wren_input_claim *next;
};

struct st_wren_wake {
	struct st_wren_operation *operation;
	WrenHandle *fiber;
	WrenHandle *value;
	struct st_wren_wake *next;
	bool cancelled;
};

struct foreign_claim_handle {
	struct foreign_header header;
	struct st_wren_context *context;
	struct st_wren_input_claim *entry;
	uint64_t id;
};

#define ST_WREN_MOUSE_EVENT_MAX 49
#define ST_WREN_MOUSE_MASK_MAX ((UINT64_C(1) << 50) - 1)
#define ST_WREN_MOUSE_WHEEL_UP 46
#define ST_WREN_MOUSE_WHEEL_DOWN 47
#define ST_WREN_MOUSE_WHEEL_LEFT 48
#define ST_WREN_MOUSE_WHEEL_RIGHT 49

struct foreign_cursor {
	struct foreign_header header;
	struct st_console_custom_cursor cursor;
	struct st_console_pixel_cursor pixel_cursor;
};

struct foreign_video_flags {
	struct foreign_header header;
	uint32_t flags;
};

struct foreign_font_error {
	struct foreign_header header;
	enum st_wren_font_error code;
	int native_code;
	const char *message;
};

struct foreign_screen_error {
	struct foreign_header header;
	int code;
	char message[192];
};

bool
st_wren_screen_status(WrenVM *vm, int status, const char *operation)
{
	if (status == 0)
		return true;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "ScreenError", 1);
	struct foreign_screen_error *error = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*error));
	error->header.kind = FOREIGN_SCREEN_ERROR;
	error->code = status;
	const char *reason = status == IO_ERROR_OUT_OF_MEMORY ? "Out of memory"
	    : status == IO_ERROR_NOT_SUPPORTED ? "Not supported"
	    : status == IO_ERROR_OUT_OF_RANGE ? "Out of range"
	    : status == IO_ERROR_INVALID_ARGUMENT ? "Invalid argument"
	    : status == IO_ERROR_IO ? "I/O error" : "Native operation failed";
	(void)snprintf(error->message, sizeof(error->message), "%s: %s (%d)",
	    operation, reason, status);
	return false;
}

void
st_wren_screen_result(WrenVM *vm, int status, const char *operation)
{
	if (st_wren_screen_status(vm, status, operation))
		wrenSetSlotNull(vm, 0);
}

struct binding {
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static void surface_drop_empty_pixels(struct surface_storage *storage, uint32_t row);
static enum st_codepage font_codepage(struct st_wren_context *context, uint16_t selector);

static struct st_wren_input_record
native_key_record(const struct st_console_event *event)
{
	return (struct st_wren_input_record){
		.type = ST_WREN_INPUT_KEY,
		.data.key.native = *event,
	};
}

bool
st_wren_input_key(const struct st_console_event *event,
	uint32_t *key, uint32_t *modifiers)
{
	*modifiers = 0;
	switch (event->type) {
	case ST_CONSOLE_EVENT_TEXT:
		*key = event->data.text.codepoint;
		*modifiers = event->data.text.modifiers;
		return true;
	case ST_CONSOLE_EVENT_KEY_CODE: {
		uint16_t code = event->data.key.code;

		*modifiers = event->data.key.modifiers;
		/* Console control codes retain their key identity. C0 letters
		 * have a logical Latin identity, separate from committed text. */
		if ((*modifiers & ST_CONSOLE_MODIFIER_CONTROL) != 0
		    && code >= 1 && code <= 26 && code != 8 && code != 9
		    && code != 13) {
			*key = 'a' + code - 1;
			return true;
		}
		if (code >= 32 && code < 127) {
			if ((*modifiers & (ST_CONSOLE_MODIFIER_CONTROL
			    | ST_CONSOLE_MODIFIER_ALT | ST_CONSOLE_MODIFIER_GUI)) != 0
			    && code >= 'A' && code <= 'Z')
				code += 'a' - 'A';
			*key = code;
		}
		else
			*key = ST_WREN_NAMED_KEY_BASE + code;
		return true;
	}
	case ST_CONSOLE_EVENT_CLOSE:
		*key = ST_WREN_QUIT_KEY;
		return true;
	default:
		return false;
	}
}

static bool
slot_integer(WrenVM *vm, int slot, int64_t minimum, int64_t maximum,
	int64_t *result)
{
	int64_t value;

	if (!st_wren_number_i64(vm, slot, &value)
	    || value < minimum || value > maximum) {
		st_wren_abort(vm, "expected an exact integer in range");
		return false;
	}
	*result = value;
	return true;
}

static bool
slot_boolean(WrenVM *vm, int slot, bool *result)
{
	if (wrenGetSlotType(vm, slot) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "expected a Boolean");
		return false;
	}
	*result = wrenGetSlotBool(vm, slot);
	return true;
}

static struct foreign_header *
foreign_kind(WrenVM *vm, int slot, enum foreign_kind kind)
{
	static const char *const names[] = {
		"Cell", "Surface", "ScreenSnapshot", "KeyEvent", "PhysicalKeyEvent",
		"MouseEvent", "ClaimHandle", "CustomCursor", "VideoFlags", "FontError",
		"ScreenError",
	};
	struct foreign_header *header = st_wren_foreign_require(vm, slot,
	    "syncterm", names[kind]);

	if (header != NULL && header->kind != kind) {
		st_wren_abort(vm, "foreign value has an invalid native payload");
		return NULL;
	}
	return header;
}

static int
pending_reserve(struct st_wren_context *context, size_t additional)
{
	struct st_wren_input_record *records;
	size_t required;
	size_t capacity;

	if (additional > SIZE_MAX - context->pending_input_count)
		return IO_ERROR_OUT_OF_MEMORY;
	required = context->pending_input_count + additional;
	if (required <= context->pending_input_capacity)
		return 0;
	capacity = context->pending_input_capacity == 0
	    ? 16 : context->pending_input_capacity;
	while (capacity < required) {
		if (capacity > SIZE_MAX / 2)
			return IO_ERROR_OUT_OF_MEMORY;
		capacity *= 2;
	}
	if (capacity > SIZE_MAX / sizeof(*records))
		return IO_ERROR_OUT_OF_MEMORY;
	records = malloc(capacity * sizeof(*records));
	if (records == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < context->pending_input_count; ++i)
		records[i] = context->pending_input[(context->pending_input_head + i)
		    % context->pending_input_capacity];
	free(context->pending_input);
	context->pending_input = records;
	context->pending_input_head = 0;
	context->pending_input_capacity = capacity;
	return 0;
}

static int
pending_push_back(struct st_wren_context *context,
	const struct st_wren_input_record *record)
{
	size_t tail;
	int status = pending_reserve(context, 1);

	if (status != 0)
		return status;
	tail = (context->pending_input_head + context->pending_input_count)
	    % context->pending_input_capacity;
	context->pending_input[tail] = *record;
	++context->pending_input_count;
	return 0;
}

static int
pending_push_front(struct st_wren_context *context,
	const struct st_wren_input_record *record)
{
	int status = pending_reserve(context, 1);

	if (status != 0)
		return status;
	context->pending_input_head = context->pending_input_head == 0
	    ? context->pending_input_capacity - 1
	    : context->pending_input_head - 1;
	context->pending_input[context->pending_input_head] = *record;
	++context->pending_input_count;
	return 0;
}

static bool
pending_take(struct st_wren_context *context,
	struct st_wren_input_record *record)
{
	if (context->pending_input_count == 0)
		return false;
	*record = context->pending_input[context->pending_input_head];
	context->pending_input_head = (context->pending_input_head + 1)
	    % context->pending_input_capacity;
	--context->pending_input_count;
	return true;
}

static void
storage_release(struct surface_storage *storage)
{
	if (storage == NULL)
		return;
	if (--storage->references == 0) {
		if (storage->previous != NULL)
			storage->previous->next = storage->next;
		else
			storage->host->surface_storage = storage->next;
		if (storage->next != NULL)
			storage->next->previous = storage->previous;
		free(storage->cells);
		st_history_free_pixels(storage->pixels, storage->height);
		free(storage);
	}
}

static void
storage_attach(struct surface_storage *storage, struct st_wren_host *host)
{
	storage->host = host;
	storage->next = host->surface_storage;
	if (storage->next != NULL)
		storage->next->previous = storage;
	host->surface_storage = storage;
}

int
st_wren_mark_surface_hyperlinks(struct st_wren_host *host,
	st_hyperlink_mark_fn mark, void *mark_context)
{
	int status = 0;

	for (struct surface_storage *storage = host->surface_storage;
	    storage != NULL && status == 0; storage = storage->next) {
		for (size_t i = 0; i < storage->count && status == 0; ++i)
			status = mark(mark_context, storage->cells[i].hyperlink);
	}
	return status;
}

static struct surface_storage *
storage_create(uint32_t width, uint32_t height,
	const struct st_console_style *style, st_console_t *console,
	struct st_wren_host *host)
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
		.foreground = style->foreground,
		.background = style->background,
		.flags = style->flags & ST_CONSOLE_CELL_PUBLIC_FLAGS,
		.font = style->font,
		.glyph_font = style->font,
		.hyperlink = style->hyperlink,
	};
	{
		uint8_t columns;

		(void)st_console_font_lookup(console, blank.font, ' ',
		    &blank.glyph_font, &blank.glyph, &columns);
	}
	for (size_t i = 0; i < storage->count; ++i)
		storage->cells[i] = blank;
	storage_attach(storage, host);
	return storage;
}

void
st_wren_push_surface_take(WrenVM *vm, struct st_console_cell *cells,
	uint32_t width, uint32_t height)
{
	struct surface_storage *storage;
	struct foreign_surface *surface;

	if (vm == NULL || cells == NULL || width == 0 || height == 0
	    || width > SIZE_MAX / height) {
		free(cells);
		if (vm != NULL)
			wrenSetSlotNull(vm, 0);
		return;
	}
	storage = calloc(1, sizeof(*storage));
	if (storage == NULL) {
		free(cells);
		st_wren_abort(vm, "out of memory");
		return;
	}
	storage->references = 1;
	storage->count = (size_t)width * height;
	storage->width = width;
	storage->height = height;
	storage->cells = cells;
	storage_attach(storage, st_wren_host_from_vm(vm));
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "Surface", 1);
	surface = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*surface));
	surface->header.kind = FOREIGN_SURFACE;
	surface->storage = storage;
}

static struct surface_storage *
status_surface_storage(WrenVM *vm, int slot, uint32_t width, uint32_t height)
{
	struct foreign_surface *surface;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "Surface"))
		return NULL;
	surface = wrenGetSlotForeign(vm, slot);
	if (surface == NULL || surface->header.kind != FOREIGN_SURFACE
	    || surface->storage == NULL
	    || surface->storage->width != width
	    || surface->storage->height != height)
		return NULL;
	return surface->storage;
}

bool
st_wren_surface_reset(WrenVM *vm, int slot,
	const struct st_console_cell *blank, uint32_t width, uint32_t height)
{
	struct surface_storage *storage;

	if (blank == NULL || (storage = status_surface_storage(vm, slot,
	    width, height)) == NULL)
		return false;
	for (size_t i = 0; i < storage->count; ++i)
		storage->cells[i] = *blank;
	st_history_free_pixels(storage->pixels, storage->height);
	storage->pixels = NULL;
	return true;
}

const struct st_console_cell *
st_wren_surface_cells(WrenVM *vm, int slot, uint32_t width, uint32_t height)
{
	struct surface_storage *storage = status_surface_storage(vm, slot,
	    width, height);

	return storage == NULL ? NULL : storage->cells;
}

bool
st_wren_surface_view(WrenVM *vm, int slot,
	struct st_console_cell **cells, uint32_t *width, uint32_t *height)
{
	struct foreign_surface *surface;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "Surface"))
		return false;
	surface = wrenGetSlotForeign(vm, slot);
	if (surface == NULL || surface->header.kind != FOREIGN_SURFACE
	    || surface->storage == NULL)
		return false;
	*cells = surface->storage->cells;
	*width = surface->storage->width;
	*height = surface->storage->height;
	return true;
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

static struct st_console_cell *
cell_value_if_present(WrenVM *vm, int slot)
{
	struct foreign_header *header;
	struct foreign_cell *cell;

	if (!st_wren_foreign_is(vm, slot, "syncterm", "Cell"))
		return NULL;
	header = wrenGetSlotForeign(vm, slot);
	if (header == NULL || header->kind != FOREIGN_CELL)
		return NULL;
	cell = (struct foreign_cell *)header;
	if (cell->storage == NULL || cell->index >= cell->storage->count)
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
	uint8_t columns;

	blank.flags &= ~ST_CONSOLE_CELL_CONTINUATION;
	(void)st_console_font_lookup(st_wren_context_from_vm(vm)->console,
	    blank.font, ' ', &blank.glyph_font, &blank.glyph, &columns);
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

static struct st_console_cell
clipped_blank(WrenVM *vm, const struct st_console_cell *source)
{
	struct st_console_cell blank = *source;
	uint8_t columns;

	blank.flags &= ~(ST_CONSOLE_CELL_CONTINUATION
	    | ST_CONSOLE_CELL_PIXEL_GRAPHICS);
	(void)st_console_font_lookup(st_wren_context_from_vm(vm)->console,
	    blank.font, ' ', &blank.glyph_font, &blank.glyph, &columns);
	return blank;
}

static void
set_cell_cluster(WrenVM *vm, struct foreign_cell *foreign, const char *text,
	size_t text_size, uint8_t columns, bool diagnostic)
{
	struct st_console_cell value = foreign->storage->cells[foreign->index];
	st_console_t *console = st_wren_context_from_vm(vm)->console;
	bool fits_wide = foreign->storage->width != 0
	    && foreign->index % foreign->storage->width + 1
	    < foreign->storage->width;

	value.flags &= ~(ST_CONSOLE_CELL_CONTINUATION | ST_CONSOLE_CELL_PIXEL_GRAPHICS);
	if (st_console_font_lookup_cluster(console, value.font, text, text_size,
	    diagnostic ? 1 : columns, &value.glyph_font, &value.glyph) != 0) {
		return;
	}
	if (st_console_font_glyph_columns(console, value.glyph_font, value.glyph,
	    &columns) != 0)
		return;
	if (diagnostic && columns == 2 && !fits_wide) {
		if (st_console_font_lookup_fallback(console, value.font, 1,
		    &value.glyph_font, &value.glyph) != 0)
			return;
		columns = 1;
	}
	clear_cell_pair(vm, foreign);
	/* Replacing the next cell must also repair a wide pair starting there. */
	if (columns == 2 && fits_wide) {
		struct foreign_cell next = *foreign;
		++next.index;
		clear_cell_pair(vm, &next);
	}
	foreign->storage->cells[foreign->index] = value;
	if (columns == 2 && fits_wide) {
		value.flags |= ST_CONSOLE_CELL_CONTINUATION;
		foreign->storage->cells[foreign->index + 1] = value;
	}
	surface_drop_empty_pixels(foreign->storage, (uint32_t)(foreign->index / foreign->storage->width));
}

static uint8_t
resolved_cell_columns(WrenVM *vm, const struct st_console_cell *cell)
{
	uint8_t columns = 1;

	(void)st_console_font_glyph_columns(st_wren_context_from_vm(vm)->console,
	    cell->glyph_font, cell->glyph, &columns);
	return columns;
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
	cell->storage = storage_create(1, 1, &style,
	    st_wren_context_from_vm(vm)->console, st_wren_host_from_vm(vm));
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
	    &style, st_wren_context_from_vm(vm)->console,
	    st_wren_host_from_vm(vm));
	if (surface->storage == NULL)
		st_wren_abort(vm, "Surface.new: dimensions are too large");
}

static bool
key_arguments(WrenVM *vm, struct st_console_event *native)
{
	int64_t key;
	int64_t modifiers = 0;

	if (!slot_integer(vm, 1, 0, ST_WREN_QUIT_KEY, &key)
	    || (wrenGetSlotCount(vm) == 3
	    && !slot_integer(vm, 2, 0, UINT32_MAX, &modifiers)))
		return false;
	if (key == ST_WREN_QUIT_KEY && modifiers != 0) {
		st_wren_abort(vm, "the quit marker cannot carry modifiers");
		return false;
	}
	if (key == ST_WREN_QUIT_KEY)
		*native = (struct st_console_event){.type = ST_CONSOLE_EVENT_CLOSE};
	else if (key >= ST_WREN_NAMED_KEY_BASE)
		*native = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_KEY_CODE,
			.data.key = {.code = (uint16_t)(key - ST_WREN_NAMED_KEY_BASE),
			    .modifiers = (uint32_t)modifiers},
		};
	else if (key >= 0xd800 && key <= 0xdfff) {
		st_wren_abort(vm, "KeyEvent.new expects a Unicode scalar or named key");
		return false;
	}
	else
		*native = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_TEXT,
			.data.text = {.codepoint = (uint32_t)key,
			    .modifiers = (uint32_t)modifiers},
		};
	return true;
}

static void
allocate_key_event(WrenVM *vm)
{
	struct foreign_key_event *event = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*event));

	memset(event, 0, sizeof(*event));
	event->header.kind = FOREIGN_KEY_EVENT;
	(void)key_arguments(vm, &event->native);
}

static void
fn_key_from_text(WrenVM *vm)
{
	const char *bytes;
	int length;
	uint32_t codepoint;
	size_t consumed;
	int64_t modifiers;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "KeyEvent.text expects exactly one UTF-8 scalar");
		return;
	}
	bytes = wrenGetSlotBytes(vm, 1, &length);
	if (length == 0 || st_utf8_decode_one(&codepoint, &consumed, bytes, (size_t)length)
	    != 0 || consumed != (size_t)length) {
		st_wren_abort(vm, "KeyEvent.text expects exactly one UTF-8 scalar");
		return;
	}
	if (!slot_integer(vm, 2, 0, UINT32_MAX, &modifiers))
		return;
	struct foreign_key_event *event = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*event));
	*event = (struct foreign_key_event){
		.header.kind = FOREIGN_KEY_EVENT,
		.native = {.type = ST_CONSOLE_EVENT_TEXT,
		    .data.text = {.codepoint = codepoint,
			.modifiers = (uint32_t)modifiers}},
	};
}

static void
fn_key_from_byte(WrenVM *vm)
{
	int64_t value;

	if (!slot_integer(vm, 1, 0, UINT8_MAX, &value))
		return;
	struct foreign_key_event *event = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*event));
	*event = (struct foreign_key_event){
		.header.kind = FOREIGN_KEY_EVENT,
		.native = {.type = ST_CONSOLE_EVENT_CODEPAGE_BYTE,
		    .data.codepage_byte = (uint8_t)value},
	};
}

static void
fn_key_from_doorway(WrenVM *vm)
{
	int64_t scan;
	bool enhanced;

	if (!slot_integer(vm, 1, 0, UINT8_MAX, &scan)
	    || !slot_boolean(vm, 2, &enhanced))
		return;
	struct foreign_key_event *event = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*event));
	*event = (struct foreign_key_event){
		.header.kind = FOREIGN_KEY_EVENT,
		.native = {.type = ST_CONSOLE_EVENT_DOORWAY_KEY,
		    .data.doorway_key = {.scan_code = (uint8_t)scan,
			.enhanced = enhanced}},
	};
}

static void
allocate_snapshot(WrenVM *vm)
{
	struct foreign_snapshot *snapshot = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*snapshot));

	*snapshot = (struct foreign_snapshot){.header.kind = FOREIGN_SNAPSHOT};
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
	    || !slot_boolean(vm, 2, &event->pressed))
		return;
	event->evdev = (uint16_t)evdev;
	if (wrenGetSlotCount(vm) == 5) {
		int64_t modifiers;

		if (!slot_integer(vm, 3, 0, UINT32_MAX, &modifiers)
		    || !slot_boolean(vm, 4, &event->repeat))
			return;
		event->modifiers = (uint32_t)modifiers;
	}
	else if (wrenGetSlotCount(vm) != 3)
		st_wren_abort(vm,
		    "PhysicalKeyEvent.new expects 2 or 4 arguments");
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
		bool unsigned_field = i == 0 || (count == 4 && i == 3)
		    || (count >= 6 && i == 5) || (count == 7 && i == 6);
		int64_t minimum = unsigned_field ? 0 : INT32_MIN;
		int64_t maximum = i == 0 ? ST_WREN_MOUSE_EVENT_MAX
		    : unsigned_field ? UINT32_MAX : INT32_MAX;

		if (!slot_integer(vm, i + 1, minimum, maximum, &values[i]))
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
	if (count != 7 && event->event >= 1 && event->event <= 45)
		event->buttons = UINT32_C(1)
		    << (unsigned int)((event->event - 1) / 9);
}

static void
allocate_claim_handle(WrenVM *vm)
{
	struct foreign_claim_handle *handle = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*handle));

	memset(handle, 0, sizeof(*handle));
	handle->header.kind = FOREIGN_CLAIM_HANDLE;
}

static void
allocate_cursor(WrenVM *vm)
{
	struct st_console_custom_cursor value;
	if (!st_wren_screen_status(vm, st_console_get_custom_cursor(st_wren_context_from_vm(vm)->console,
	    &value), "CustomCursor.current")) return;
	struct st_console_pixel_cursor pixels;
	if (!st_wren_screen_status(vm, st_console_get_pixel_cursor(st_wren_context_from_vm(vm)->console,
		&pixels), "CustomCursor.current pixels")) return;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "CustomCursor", 1);
	struct foreign_cursor *cursor = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*cursor));
	cursor->header.kind = FOREIGN_CURSOR;
	cursor->cursor = value;
	cursor->pixel_cursor = pixels;
}

static void
allocate_video_flags(WrenVM *vm)
{
	uint32_t value;
	if (!st_wren_screen_status(vm, st_console_get_video_flags(st_wren_context_from_vm(vm)->console,
	    &value), "VideoFlags.current")) return;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "VideoFlags", 1);
	struct foreign_video_flags *flags = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*flags));
	flags->header.kind = FOREIGN_VIDEO_FLAGS;
	flags->flags = value;
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
make_input_event(WrenVM *vm, int slot,
	const struct st_wren_input_record *record)
{
	wrenEnsureSlots(vm, slot + 2);
	if (record->type == ST_WREN_INPUT_KEY) {
		struct foreign_key_event *event;

		wrenGetVariable(vm, "syncterm", "KeyEvent", slot + 1);
		event = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*event));
		event->header.kind = FOREIGN_KEY_EVENT;
		event->native = record->data.key.native;
	}
	else if (record->type == ST_WREN_INPUT_PHYSICAL_KEY) {
		struct foreign_physical_key_event *event;

		wrenGetVariable(vm, "syncterm", "PhysicalKeyEvent", slot + 1);
		event = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*event));
		event->header.kind = FOREIGN_PHYSICAL_KEY_EVENT;
		event->evdev = record->data.physical_key.evdev;
		event->modifiers = record->data.physical_key.modifiers;
		event->pressed = record->data.physical_key.pressed;
		event->repeat = record->data.physical_key.repeat;
	}
	else {
		struct foreign_mouse_event *event;

		wrenGetVariable(vm, "syncterm", "MouseEvent", slot + 1);
		event = wrenSetSlotNewForeign(vm, slot, slot + 1, sizeof(*event));
		memset(event, 0, sizeof(*event));
		event->header.kind = FOREIGN_MOUSE_EVENT;
		event->event = record->data.mouse.event;
		event->buttons = record->data.mouse.buttons;
		event->modifiers = record->data.mouse.modifiers;
		event->start_x = record->data.mouse.start_x;
		event->start_y = record->data.mouse.start_y;
		event->end_x = record->data.mouse.end_x;
		event->end_y = record->data.mouse.end_y;
		event->start_pixel_x = record->data.mouse.start_pixel_x;
		event->start_pixel_y = record->data.mouse.start_pixel_y;
		event->end_pixel_x = record->data.mouse.end_pixel_x;
		event->end_pixel_y = record->data.mouse.end_pixel_y;
		event->wheel_x = record->data.mouse.wheel_x;
		event->wheel_y = record->data.mouse.wheel_y;
	}
}

void
st_wren_input_push_event(WrenVM *vm, int slot,
	const struct st_wren_input_record *record)
{
	make_input_event(vm, slot, record);
}

static struct st_wren_input_record
mouse_record(const struct st_console_event *source, int code,
	int32_t start_x, int32_t start_y, int32_t start_pixel_x,
	int32_t start_pixel_y, uint32_t modifiers)
{
	struct st_wren_input_record record = {
		.type = ST_WREN_INPUT_MOUSE,
		.data.mouse = {
			.event = code,
			.buttons = source->data.mouse.buttons,
			.modifiers = modifiers,
			.start_x = start_x + 1,
			.start_y = start_y + 1,
			.end_x = source->data.mouse.x + 1,
			.end_y = source->data.mouse.y + 1,
			.start_pixel_x = start_pixel_x,
			.start_pixel_y = start_pixel_y,
			.end_pixel_x = source->data.mouse.pixel_x,
			.end_pixel_y = source->data.mouse.pixel_y,
		},
	};

	return record;
}

static void
mouse_add_if_enabled(struct st_wren_context *context,
	struct st_wren_input_record *records, size_t *count,
	struct st_wren_input_record record)
{
	if ((context->mouse_events & (UINT64_C(1) << record.data.mouse.event))
	    != 0)
		records[(*count)++] = record;
}

static void
collect_mouse_events(struct st_wren_context *context,
	const struct st_console_event *source,
	struct st_wren_input_record records[11], size_t *result_count)
{
	static const uint32_t masks[5] = {
		ST_CONSOLE_MOUSE_BUTTON_LEFT, ST_CONSOLE_MOUSE_BUTTON_MIDDLE,
		ST_CONSOLE_MOUSE_BUTTON_RIGHT, ST_CONSOLE_MOUSE_BUTTON_X1,
		ST_CONSOLE_MOUSE_BUTTON_X2,
	};
	size_t count = 0;

	if (source->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL) {
		struct st_wren_input_record record;
		int32_t wheel_y = st_settings.invert_wheel
		    ? -source->data.mouse.wheel_y : source->data.mouse.wheel_y;

		if (wheel_y != 0) {
			record = mouse_record(source,
			    wheel_y > 0
			    ? ST_WREN_MOUSE_WHEEL_UP : ST_WREN_MOUSE_WHEEL_DOWN,
			    source->data.mouse.x, source->data.mouse.y,
			    source->data.mouse.pixel_x, source->data.mouse.pixel_y,
			    source->data.mouse.modifiers);
			record.data.mouse.wheel_y = wheel_y;
			mouse_add_if_enabled(context, records, &count, record);
		}
		if (source->data.mouse.wheel_x != 0) {
			record = mouse_record(source,
			    source->data.mouse.wheel_x < 0
			    ? ST_WREN_MOUSE_WHEEL_LEFT : ST_WREN_MOUSE_WHEEL_RIGHT,
			    source->data.mouse.x, source->data.mouse.y,
			    source->data.mouse.pixel_x, source->data.mouse.pixel_y,
			    source->data.mouse.modifiers);
			record.data.mouse.wheel_x = source->data.mouse.wheel_x;
			mouse_add_if_enabled(context, records, &count, record);
		}
	}
	else if (source->data.mouse.action == ST_CONSOLE_MOUSE_MOVE) {
		struct st_wren_input_record record = mouse_record(source,
		    ST_WREN_MOUSE_MOVE, source->data.mouse.x, source->data.mouse.y,
		    source->data.mouse.pixel_x, source->data.mouse.pixel_y,
		    source->data.mouse.modifiers);

		mouse_add_if_enabled(context, records, &count, record);
		for (size_t i = 0; i < 5; ++i) {
			struct st_wren_mouse_gesture *gesture =
			    &context->mouse_gestures[i];

			if (!gesture->pressed
			    || (source->data.mouse.buttons & masks[i]) == 0)
				continue;
			if (!gesture->dragging
			    && source->data.mouse.x == gesture->start_x
			    && source->data.mouse.y == gesture->start_y)
				continue;
			gesture->end_x = source->data.mouse.x;
			gesture->end_y = source->data.mouse.y;
			record = mouse_record(source, (int)(i * 9 +
			    (gesture->dragging ? 8 : 7)), gesture->start_x,
			    gesture->start_y, gesture->start_pixel_x,
			    gesture->start_pixel_y, gesture->modifiers);
			gesture->dragging = true;
			mouse_add_if_enabled(context, records, &count, record);
		}
	}
	else {
		uint32_t changed = context->mouse_buttons
		    ^ source->data.mouse.buttons;

		for (size_t i = 0; i < 5; ++i) {
			struct st_wren_mouse_gesture *gesture =
			    &context->mouse_gestures[i];
			struct st_wren_input_record record;
			int base = (int)(i * 9 + 1);

			if ((changed & masks[i]) == 0)
				continue;
			if ((source->data.mouse.buttons & masks[i]) != 0) {
				*gesture = (struct st_wren_mouse_gesture){
					.start_x = source->data.mouse.x,
					.start_y = source->data.mouse.y,
					.start_pixel_x = source->data.mouse.pixel_x,
					.start_pixel_y = source->data.mouse.pixel_y,
					.end_x = source->data.mouse.x,
					.end_y = source->data.mouse.y,
					.modifiers = source->data.mouse.modifiers,
					.pressed = true,
				};
				record = mouse_record(source, base,
				    source->data.mouse.x, source->data.mouse.y,
				    source->data.mouse.pixel_x,
				    source->data.mouse.pixel_y,
				    source->data.mouse.modifiers);
				mouse_add_if_enabled(context, records, &count, record);
			}
			else {
				record = mouse_record(source, base + 1,
				    source->data.mouse.x, source->data.mouse.y,
				    source->data.mouse.pixel_x,
				    source->data.mouse.pixel_y,
				    source->data.mouse.modifiers);
				mouse_add_if_enabled(context, records, &count, record);
				if (gesture->pressed) {
					record = mouse_record(source,
					    base + (gesture->dragging ? 8 : 2),
					    gesture->start_x, gesture->start_y,
					    gesture->start_pixel_x,
					    gesture->start_pixel_y,
					    gesture->modifiers);
					mouse_add_if_enabled(context, records, &count,
					    record);
				}
				memset(gesture, 0, sizeof(*gesture));
			}
		}
		context->mouse_buttons = source->data.mouse.buttons;
	}
	*result_count = count;
}

static int
queue_mouse_events(struct st_wren_context *context,
	const struct st_console_event *source)
{
	struct st_wren_input_record records[11];
	size_t count;

	collect_mouse_events(context, source, records, &count);
	if (pending_reserve(context, count) != 0)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < count; ++i)
		(void)pending_push_back(context, &records[i]);
	return 0;
}

static int
queue_console_event(struct st_wren_context *context,
	const struct st_console_event *event)
{
	struct st_wren_input_record records[2] = {0};
	size_t count = 0;

	switch (event->type) {
	case ST_CONSOLE_EVENT_KEY_CODE:
	case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
	case ST_CONSOLE_EVENT_TEXT:
	case ST_CONSOLE_EVENT_DOORWAY_KEY:
		records[count++] = native_key_record(event);
		break;
	case ST_CONSOLE_EVENT_PHYSICAL_KEY: {
		records[count++] = (struct st_wren_input_record){
			.type = ST_WREN_INPUT_PHYSICAL_KEY,
			.data.physical_key = {
				.evdev = event->data.physical_key.evdev,
				.modifiers = event->data.physical_key.modifiers,
				.pressed = event->data.physical_key.pressed,
				.repeat = event->data.physical_key.repeat,
			},
		};
		break;
	}
	case ST_CONSOLE_EVENT_MOUSE:
		return queue_mouse_events(context, event);
	default:
		return 0;
	}
	if (pending_reserve(context, count) != 0)
		return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < count; ++i)
		(void)pending_push_back(context, &records[i]);
	return 0;
}

static bool
next_input_event(WrenVM *vm, bool wait, int64_t timeout_ms, bool at_exit,
    bool wake_on_resize)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_input_record record;
	struct st_console_event event;
	struct timespec deadline;
	const struct timespec *deadline_pointer = NULL;
	int status;

	if (wait && timeout_ms >= 0) {
		status = st_time_utc_deadline_after_ms(&deadline, timeout_ms);
		if (status != 0) {
			st_wren_abort(vm, "input deadline failed");
			return false;
		}
		deadline_pointer = &deadline;
	}
	for (;;) {
#if ST_CONSOLE_TEST
		st_console_test_service(host->config.display);
#endif
		struct st_wren_context *poll_context = context->cterm != NULL ? context : host->poll_contexts;
		st_cterm_t *terminal = poll_context != NULL ? poll_context->cterm : NULL;
		/* Synchronous UI waits keep the owner from reaching the outer
		 * terminal pump. Refill audio here without parsing remote input. */
		bool service = terminal != NULL && st_cterm_input_paused(terminal);
		if (service) {
			int poll_status = poll_context->connection != NULL
			    && !st_connection_is_connected(poll_context->connection)
			    ? st_cterm_finish(terminal) : st_cterm_poll(terminal);
			if (poll_status != 0) {
				st_wren_abort(vm, "terminal polling failed during input wait");
				return false;
			}
		}
		service = terminal != NULL && st_cterm_input_paused(terminal);
		if (host->exit_requested && !at_exit) {
			record = (struct st_wren_input_record){
				.type = ST_WREN_INPUT_KEY,
				.data.key.native.type = ST_CONSOLE_EVENT_CLOSE,
			};
			make_input_event(vm, 0, &record);
			return true;
		}
		if (pending_take(context, &record)) {
			make_input_event(vm, 0, &record);
			return true;
		}
		struct timespec service_deadline;
		const struct timespec *event_deadline = deadline_pointer;
		bool service_timeout = false;
		if (wait && service) {
			status = st_time_utc_deadline_after_ms(&service_deadline, 20);
			if (status != 0) {
				st_wren_abort(vm, "input service deadline failed");
				return false;
			}
			if (event_deadline == NULL || service_deadline.tv_sec < event_deadline->tv_sec
			    || (service_deadline.tv_sec == event_deadline->tv_sec
			    && service_deadline.tv_nsec < event_deadline->tv_nsec)) {
				event_deadline = &service_deadline;
				service_timeout = true;
			}
		}
		if (!wait)
			status = st_console_event_try(context->console, &event);
		else if (event_deadline == NULL)
			status = st_console_event_wait(context->console, &event);
		else
			status = st_console_event_timedwait(context->console, &event,
			    event_deadline);
		if (status == IO_ERROR_WOULD_BLOCK || status == IO_ERROR_TIMED_OUT) {
			if (service_timeout)
				continue;
			wrenSetSlotNull(vm, 0);
			return false;
		}
		if (status != 0) {
			st_wren_abort(vm, "input wait failed");
			return false;
		}
		if (event.type == ST_CONSOLE_EVENT_CLOSE) {
			host->exit_requested = true;
			continue;
		}
		if (event.type == ST_CONSOLE_EVENT_RESIZE) {
			st_wren_host_external_screen_resize(host, context->console,
			    event.data.resize.columns, event.data.resize.rows);
			if (wake_on_resize) {
				wrenSetSlotNull(vm, 0);
				return false;
			}
			continue;
		}
		status = queue_console_event(context, &event);
		if (status != 0) {
			st_wren_abort(vm, "out of memory queuing input");
			return false;
		}
	}
}

static void
fn_input_next(WrenVM *vm)
{
	(void)next_input_event(vm, true, -1, false, false);
}

static void
fn_input_next_timed(WrenVM *vm)
{
	int64_t milliseconds;

	if (!slot_integer(vm, 1, 0, INT_MAX, &milliseconds))
		return;
	(void)next_input_event(vm, true, milliseconds, false, false);
}

static void
fn_input_poll(WrenVM *vm)
{
	(void)next_input_event(vm, false, 0, false, false);
}

static void
fn_input_next_widget(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Input.nextForWidget_ expects a Bool");
		return;
	}
	(void)next_input_event(vm, true, -1, wrenGetSlotBool(vm, 1), true);
}

static void
fn_input_next_widget_timed(WrenVM *vm)
{
	int64_t milliseconds;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Input.nextForWidget_ expects a Bool");
		return;
	}
	if (!slot_integer(vm, 2, 0, INT_MAX, &milliseconds))
		return;
	(void)next_input_event(vm, true, milliseconds, wrenGetSlotBool(vm, 1), true);
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

	if (slot_integer(vm, 1, 0, (int64_t)ST_WREN_MOUSE_MASK_MAX, &value))
		st_wren_context_from_vm(vm)->mouse_events = (uint64_t)value;
}

static void
fn_input_mouse_event_enable(WrenVM *vm)
{
	int64_t value;

	if (slot_integer(vm, 1, 0, ST_WREN_MOUSE_EVENT_MAX, &value))
		st_wren_context_from_vm(vm)->mouse_events |= UINT64_C(1) << value;
}

static void
fn_input_mouse_event_disable(WrenVM *vm)
{
	int64_t value;

	if (slot_integer(vm, 1, 0, ST_WREN_MOUSE_EVENT_MAX, &value))
		st_wren_context_from_vm(vm)->mouse_events &= ~(UINT64_C(1) << value);
}

static void
fn_input_unget_key(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 1, FOREIGN_KEY_EVENT);
	struct st_wren_input_record record;

	if (event == NULL)
		return;
	record = (struct st_wren_input_record){
		.type = ST_WREN_INPUT_KEY,
		.data.key.native = event->native,
	};
	if (pending_push_front(st_wren_context_from_vm(vm), &record) != 0)
		st_wren_abort(vm, "out of memory returning input");
}

static void
fn_input_unget_physical(WrenVM *vm)
{
	struct foreign_physical_key_event *event =
	    (struct foreign_physical_key_event *)foreign_kind(vm, 1,
	    FOREIGN_PHYSICAL_KEY_EVENT);
	struct st_wren_input_record record;

	if (event == NULL)
		return;
	record = (struct st_wren_input_record){
		.type = ST_WREN_INPUT_PHYSICAL_KEY,
		.data.physical_key = {
			.evdev = event->evdev,
			.modifiers = event->modifiers,
			.pressed = event->pressed,
			.repeat = event->repeat,
		},
	};
	if (pending_push_front(st_wren_context_from_vm(vm), &record) != 0)
		st_wren_abort(vm, "out of memory returning input");
}

static void
fn_input_unget_mouse(WrenVM *vm)
{
	struct foreign_mouse_event *event =
	    (struct foreign_mouse_event *)foreign_kind(vm, 1,
	    FOREIGN_MOUSE_EVENT);
	struct st_wren_input_record record;

	if (event == NULL)
		return;
	record = (struct st_wren_input_record){
		.type = ST_WREN_INPUT_MOUSE,
		.data.mouse = {
			.event = event->event,
			.buttons = event->buttons,
			.modifiers = event->modifiers,
			.start_x = event->start_x,
			.start_y = event->start_y,
			.end_x = event->end_x,
			.end_y = event->end_y,
			.start_pixel_x = event->start_pixel_x,
			.start_pixel_y = event->start_pixel_y,
			.end_pixel_x = event->end_pixel_x,
			.end_pixel_y = event->end_pixel_y,
			.wheel_x = event->wheel_x,
			.wheel_y = event->wheel_y,
		},
	};
	if (pending_push_front(st_wren_context_from_vm(vm), &record) != 0)
		st_wren_abort(vm, "out of memory returning input");
}

static void
fn_input_synthesize_physical(WrenVM *vm)
{
	int64_t evdev;
	bool pressed;
	int status;

	if (!slot_integer(vm, 1, 0, UINT16_MAX, &evdev)
	    || !slot_boolean(vm, 2, &pressed))
		return;
	status = st_console_synthesize_physical_key(
	    st_wren_context_from_vm(vm)->console, (uint16_t)evdev, pressed);
	if (status != 0)
		st_wren_abort(vm, status == IO_ERROR_OUT_OF_MEMORY
		    ? "out of memory synthesizing physical input"
		    : "physical input synthesis failed");
}

static void
fn_mouse_with_pixels_common(WrenVM *vm, bool wheel)
{
	struct foreign_mouse_event *event;
	int64_t values[13] = {0};
	int count = wheel ? 13 : 11;

	for (int i = 0; i < count; ++i) {
		bool unsigned_field = i == 0 || i == 5 || i == 6;
		int64_t minimum = unsigned_field ? 0 : INT32_MIN;
		int64_t maximum = i == 0 ? ST_WREN_MOUSE_EVENT_MAX
		    : unsigned_field ? UINT32_MAX : INT32_MAX;

		if (!slot_integer(vm, i + 1, minimum, maximum, &values[i]))
			return;
	}
	wrenEnsureSlots(vm, count + 2);
	wrenGetVariable(vm, "syncterm", "MouseEvent", count + 1);
	event = wrenSetSlotNewForeign(vm, 0, count + 1, sizeof(*event));
	memset(event, 0, sizeof(*event));
	event->header.kind = FOREIGN_MOUSE_EVENT;
	event->event = (int)values[0];
	event->start_x = (int32_t)values[1];
	event->start_y = (int32_t)values[2];
	event->end_x = (int32_t)values[3];
	event->end_y = (int32_t)values[4];
	event->modifiers = (uint32_t)values[5];
	event->buttons = (uint32_t)values[6];
	event->start_pixel_x = (int32_t)values[7];
	event->start_pixel_y = (int32_t)values[8];
	event->end_pixel_x = (int32_t)values[9];
	event->end_pixel_y = (int32_t)values[10];
	if (wheel) {
		event->wheel_x = (int32_t)values[11];
		event->wheel_y = (int32_t)values[12];
	}
}

static void
fn_mouse_with_pixels(WrenVM *vm)
{
	fn_mouse_with_pixels_common(vm, false);
}

static void
fn_mouse_with_pixels_and_wheel(WrenVM *vm)
{
	fn_mouse_with_pixels_common(vm, true);
}

static bool
claim_remove(struct st_wren_context *context,
	struct st_wren_input_claim *entry, uint64_t id)
{
	struct st_wren_input_claim **cursor;

	if (context == NULL || entry == NULL)
		return false;
	for (cursor = &context->claim_top; *cursor != NULL;
	    cursor = &(*cursor)->next) {
		struct st_wren_input_claim *removed;

		if (*cursor != entry)
			continue;
		if (entry->id != id)
			return false;
		removed = *cursor;
		*cursor = removed->next;
		wrenReleaseHandle(context->vm, removed->function);
		wrenReleaseHandle(context->vm, removed->fiber);
		free(removed);
		return true;
	}
	return false;
}

static void
finalize_claim_handle(void *data)
{
	struct foreign_claim_handle *handle = data;

	if (handle == NULL)
		return;
	(void)claim_remove(handle->context, handle->entry, handle->id);
	handle->entry = NULL;
}

static void
fn_input_push_claim(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_input_claim *entry;
	struct foreign_claim_handle *result;
	WrenHandle *fiber = wrenGetSlotHandle(vm, 1);
	WrenHandle *function = wrenGetSlotHandle(vm, 2);

	if (fiber == NULL || function == NULL) {
		if (fiber != NULL)
			wrenReleaseHandle(vm, fiber);
		if (function != NULL)
			wrenReleaseHandle(vm, function);
		st_wren_abort(vm, "out of memory registering input claim");
		return;
	}
	for (entry = context->claim_top; entry != NULL; entry = entry->next)
		if (wrenValuesSame(entry->fiber->value, fiber->value))
			break;
	if (entry == NULL) {
		entry = calloc(1, sizeof(*entry));
		if (entry == NULL) {
			wrenReleaseHandle(vm, function);
			wrenReleaseHandle(vm, fiber);
			st_wren_abort(vm, "out of memory registering input claim");
			return;
		}
		entry->fiber = fiber;
		entry->function = function;
		entry->next = context->claim_top;
		context->claim_top = entry;
	}
	else {
		wrenReleaseHandle(vm, entry->function);
		wrenReleaseHandle(vm, fiber);
		entry->function = function;
	}
	if (++context->next_claim_id == 0)
		++context->next_claim_id;
	entry->id = context->next_claim_id;
	wrenEnsureSlots(vm, 4);
	wrenGetVariable(vm, "syncterm", "ClaimHandle", 3);
	result = wrenSetSlotNewForeign(vm, 0, 3, sizeof(*result));
	result->header.kind = FOREIGN_CLAIM_HANDLE;
	result->context = context;
	result->entry = entry;
	result->id = entry->id;
}

static void
fn_claim_pop(WrenVM *vm)
{
	struct foreign_claim_handle *handle =
	    (struct foreign_claim_handle *)foreign_kind(vm, 0,
	    FOREIGN_CLAIM_HANDLE);
	bool removed = false;

	if (handle != NULL) {
		removed = claim_remove(handle->context, handle->entry, handle->id);
		handle->entry = NULL;
	}
	wrenSetSlotBool(vm, 0, removed);
}

static void
fn_wake_post(WrenVM *vm)
{
	if (st_wren_wake_enqueue(vm, 1, 2, NULL) != 0) {
		st_wren_abort(vm, "out of memory posting wake");
		return;
	}
	wrenSetSlotNull(vm, 0);
}

int
st_wren_wake_enqueue(WrenVM *vm, int fiber_slot, int value_slot,
	struct st_wren_operation *source)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_wren_wake *wake = calloc(1, sizeof(*wake));

	if (wake == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	wake->operation = source;
	wake->fiber = wrenGetSlotHandle(vm, fiber_slot);
	wake->value = wrenGetSlotHandle(vm, value_slot);
	if (wake->fiber == NULL || wake->value == NULL) {
		if (wake->fiber != NULL)
			wrenReleaseHandle(vm, wake->fiber);
		if (wake->value != NULL)
			wrenReleaseHandle(vm, wake->value);
		free(wake);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	if (context->wake_tail == NULL)
		context->wake_head = wake;
	else
		context->wake_tail->next = wake;
	context->wake_tail = wake;
	return 0;
}

static bool
fiber_is_done(struct st_wren_context *context, WrenHandle *fiber)
{
	wrenEnsureSlots(context->vm, 1);
	wrenSetSlotHandle(context->vm, 0, fiber);
	if (wrenCall(context->vm, context->fiber_is_done) != WREN_RESULT_SUCCESS)
		return true;
	return wrenGetSlotType(context->vm, 0) != WREN_TYPE_BOOL
	    || wrenGetSlotBool(context->vm, 0);
}

int
st_wren_input_dispatch(WrenVM *vm,
	const struct st_wren_input_record *event, bool *consumed)
{
	struct claim_snapshot {
		WrenHandle *fiber;
		WrenHandle *function;
	};
	struct st_wren_context *context;
	struct st_wren_input_claim **cursor;
	struct claim_snapshot *claims = NULL;
	size_t count = 0;
	size_t index = 0;
	int status = 0;

	if (vm == NULL || event == NULL || consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	context = st_wren_context_from_vm(vm);
	*consumed = false;
	for (cursor = &context->claim_top; *cursor != NULL;) {
		struct st_wren_input_claim *claim = *cursor;

		if (!fiber_is_done(context, claim->fiber)) {
			++count;
			cursor = &claim->next;
			continue;
		}
		*cursor = claim->next;
		wrenReleaseHandle(vm, claim->function);
		wrenReleaseHandle(vm, claim->fiber);
		free(claim);
	}
	if (count != 0) {
		claims = calloc(count, sizeof(*claims));
		if (claims == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	for (struct st_wren_input_claim *claim = context->claim_top;
	    claim != NULL; claim = claim->next) {
		wrenEnsureSlots(vm, 2);
		wrenSetSlotHandle(vm, 0, claim->fiber);
		claims[index].fiber = wrenGetSlotHandle(vm, 0);
		wrenSetSlotHandle(vm, 0, claim->function);
		claims[index].function = wrenGetSlotHandle(vm, 0);
		if (claims[index].fiber == NULL
		    || claims[index].function == NULL) {
			if (claims[index].function != NULL)
				wrenReleaseHandle(vm, claims[index].function);
			if (claims[index].fiber != NULL)
				wrenReleaseHandle(vm, claims[index].fiber);
			status = IO_ERROR_OUT_OF_MEMORY;
			break;
		}
		++index;
	}
	for (size_t i = 0; status == 0 && i < index; ++i) {
		wrenEnsureSlots(vm, 4);
		wrenSetSlotHandle(vm, 0, context->hook_class);
		wrenSetSlotHandle(vm, 1, claims[i].function);
		make_input_event(vm, 2, event);
		if (wrenCall(vm, context->hook_dispatch) != WREN_RESULT_SUCCESS) {
			status = IO_ERROR_INVALID_FORMAT;
			break;
		}
		if (wrenGetSlotType(vm, 0) == WREN_TYPE_BOOL
		    && wrenGetSlotBool(vm, 0)) {
			*consumed = true;
			break;
		}
	}
	for (size_t i = 0; i < index; ++i) {
		if (claims[i].function != NULL)
			wrenReleaseHandle(vm, claims[i].function);
		if (claims[i].fiber != NULL)
			wrenReleaseHandle(vm, claims[i].fiber);
	}
	free(claims);
	return status;
}

int
st_wren_input_dispatch_console_event(WrenVM *vm,
	const struct st_console_event *event, bool *consumed)
{
	struct st_wren_context *context;
	struct st_wren_input_record record;
	int status;

	if (vm == NULL || event == NULL || consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = false;
	if (event->type == ST_CONSOLE_EVENT_TEXT
	    && !st_unicode_scalar_valid(event->data.text.codepoint))
		return IO_ERROR_INVALID_ENCODING;
	context = st_wren_context_from_vm(vm);
	switch (event->type) {
	case ST_CONSOLE_EVENT_KEY_CODE:
	case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
	case ST_CONSOLE_EVENT_TEXT:
	case ST_CONSOLE_EVENT_DOORWAY_KEY:
		record = native_key_record(event);
		break;
	case ST_CONSOLE_EVENT_PHYSICAL_KEY: {
		record = (struct st_wren_input_record){
			.type = ST_WREN_INPUT_PHYSICAL_KEY,
			.data.physical_key = {
				.evdev = event->data.physical_key.evdev,
				.modifiers = event->data.physical_key.modifiers,
				.pressed = event->data.physical_key.pressed,
				.repeat = event->data.physical_key.repeat,
			},
		};
		status = st_wren_input_dispatch(vm, &record, consumed);
		if (status != 0 || *consumed)
			return status;
		if (context->connected != NULL) {
			status = st_wren_connected_handle_physical_record(
			    context->connected, &record, consumed);
			if (status != 0 || *consumed)
				return status;
		}
		return 0;
	}
	case ST_CONSOLE_EVENT_MOUSE: {
		struct st_wren_input_record records[11];
		size_t count;

		collect_mouse_events(context, event, records, &count);
		for (size_t i = 0; i < count; ++i) {
			bool claimed = false;

			status = st_wren_input_dispatch(vm, &records[i], &claimed);
			if (status != 0)
				return status;
			if (!claimed && context->connected != NULL) {
				status = st_wren_connected_handle_mouse_record(
				    context->connected, &records[i], &claimed);
				if (status != 0)
					return status;
			}
			if (claimed)
				*consumed = true;
		}
		/* A drag hook may have consumed a later release while running. */
		if (*consumed)
			st_cterm_sync_mouse_buttons(context->cterm,
			    context->mouse_buttons);
		return 0;
	}
	default:
		return 0;
	}
	status = st_wren_input_dispatch(vm, &record, consumed);
	if (status != 0 || *consumed || context->connected == NULL)
		return status;
	return st_wren_connected_handle_key_record(context->connected, &record,
	    consumed);
}

bool
st_wren_input_native(WrenVM *vm, int slot, struct st_console_event *event)
{
	if (st_wren_foreign_is(vm, slot, "syncterm", "KeyEvent")) {
		struct foreign_key_event *key = wrenGetSlotForeign(vm, slot);
		*event = key->native;
		return true;
	}
	if (st_wren_foreign_is(vm, slot, "syncterm", "PhysicalKeyEvent")) {
		struct foreign_physical_key_event *physical = wrenGetSlotForeign(vm, slot);
		*event = (struct st_console_event){
			.type = ST_CONSOLE_EVENT_PHYSICAL_KEY,
			.data.physical_key = {.evdev = physical->evdev,
			    .modifiers = physical->modifiers,
			    .pressed = physical->pressed, .repeat = physical->repeat},
		};
		return true;
	}
	st_wren_abort(vm, "expected a KeyEvent or PhysicalKeyEvent");
	return false;
}

void
st_wren_wake_forget_fiber(WrenVM *vm, const WrenHandle *fiber)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);

	/* A callback may cancel while drain holds pointers into this queue.
	 * Mark entries for disposal instead of invalidating the active cursor. */
	for (struct st_wren_wake *wake = context->wake_head;
	    wake != NULL; wake = wake->next) {
		if (wrenValuesSame(wake->fiber->value, fiber->value))
			wake->cancelled = true;
	}
}

int
st_wren_wake_drain(WrenVM *vm)
{
	struct st_wren_context *context;
	struct st_wren_wake *previous = NULL;
	struct st_wren_wake *boundary;
	struct st_wren_wake **cursor;
	int status = 0;

	if (vm == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	context = st_wren_context_from_vm(vm);
	boundary = context->wake_tail;
	cursor = &context->wake_head;
	while (*cursor != NULL) {
		struct st_wren_wake *wake = *cursor;
		bool last = wake == boundary;
		enum st_wren_wake_action action = ST_WREN_WAKE_DISCARD;

		if (!wake->cancelled && !fiber_is_done(context, wake->fiber)) {
			action = st_wren_operation_wake_action(vm, wake->fiber, wake->operation);
			/* Completion may bypass a parked fiber's messages; another
			 * generic message must never overtake an earlier deferred one. */
			if (action == ST_WREN_WAKE_DELIVER && wake->operation == NULL) {
				for (struct st_wren_wake *earlier = context->wake_head;
				    earlier != wake; earlier = earlier->next) {
					if (earlier->operation == NULL && st_wren_fibers_same_scope(vm,
					    earlier->fiber, wake->fiber)) {
						action = ST_WREN_WAKE_DEFER;
						break;
					}
				}
			}
		}
		if (action == ST_WREN_WAKE_DEFER) {
			previous = wake;
			cursor = &wake->next;
		}
		else {
			*cursor = wake->next;
			if (context->wake_tail == wake)
				context->wake_tail = previous;
			if (action == ST_WREN_WAKE_DELIVER) {
				wrenEnsureSlots(vm, 2);
				wrenSetSlotHandle(vm, 0, st_wren_cleanup_wake_target(vm, wake->fiber));
				wrenSetSlotHandle(vm, 1, wake->value);
				if (wrenCall(vm, context->fiber_call) != WREN_RESULT_SUCCESS)
					status = IO_ERROR_INVALID_FORMAT;
			}
			wrenReleaseHandle(vm, wake->value);
			wrenReleaseHandle(vm, wake->fiber);
			free(wake);
		}
		/* Callbacks may append more work. Drain only the original batch. */
		if (last)
			break;
	}
	return status;
}

int
st_wren_input_context_init(WrenVM *vm)
{
	struct st_wren_context *context;

	if (vm == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	context = st_wren_context_from_vm(vm);
	context->vm = vm;
	wrenEnsureSlots(vm, 1);
	if (!wrenHasModule(vm, "syncterm")
	    || !wrenHasVariable(vm, "syncterm", "Hook"))
		return IO_ERROR_INVALID_FORMAT;
	wrenGetVariable(vm, "syncterm", "Hook", 0);
	context->hook_class = wrenGetSlotHandle(vm, 0);
	context->hook_dispatch = wrenMakeCallHandle(vm, "dispatch_(_,_)");
	context->fiber_is_done = wrenMakeCallHandle(vm, "isDone");
	context->fiber_call = wrenMakeCallHandle(vm, "call(_)");
	if (context->hook_class == NULL || context->hook_dispatch == NULL
	    || context->fiber_is_done == NULL || context->fiber_call == NULL) {
		st_wren_input_context_dispose(vm);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	return 0;
}

void
st_wren_input_context_dispose(WrenVM *vm)
{
	struct st_wren_context *context;

	if (vm == NULL)
		return;
	context = st_wren_context_from_vm(vm);
	st_wren_cleanup_shutdown(vm);
	st_wren_wire_dispose(context);
	st_wren_send_dispose(context);
	st_wren_operation_dispose(vm);
	st_wren_transfer_dispose(context);
	while (context->claim_top != NULL) {
		struct st_wren_input_claim *claim = context->claim_top;

		context->claim_top = claim->next;
		wrenReleaseHandle(vm, claim->function);
		wrenReleaseHandle(vm, claim->fiber);
		free(claim);
	}
	while (context->wake_head != NULL) {
		struct st_wren_wake *wake = context->wake_head;

		context->wake_head = wake->next;
		wrenReleaseHandle(vm, wake->value);
		wrenReleaseHandle(vm, wake->fiber);
		free(wake);
	}
	context->wake_tail = NULL;
	free(context->pending_input);
	context->pending_input = NULL;
	context->pending_input_head = 0;
	context->pending_input_count = 0;
	context->pending_input_capacity = 0;
	if (context->fiber_call != NULL)
		wrenReleaseHandle(vm, context->fiber_call);
	if (context->fiber_is_done != NULL)
		wrenReleaseHandle(vm, context->fiber_is_done);
	if (context->hook_dispatch != NULL)
		wrenReleaseHandle(vm, context->hook_dispatch);
	if (context->hook_class != NULL)
		wrenReleaseHandle(vm, context->hook_class);
	context->fiber_call = NULL;
	context->fiber_is_done = NULL;
	context->hook_dispatch = NULL;
	context->hook_class = NULL;
}

static void
fn_input_mouse_visible_set(WrenVM *vm)
{
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, "Input.setMouseVisible expects a Bool");
		return;
	}
	st_wren_screen_result(vm, st_console_set_pointer_visible(
	    st_wren_context_from_vm(vm)->console, wrenGetSlotBool(vm, 1)), "Input.setMouseVisible");
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
fn_key_key(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);
	uint32_t key;
	uint32_t modifiers;

	if (event == NULL)
		return;
	if (st_wren_input_key(&event->native, &key, &modifiers))
		wrenSetSlotDouble(vm, 0, key);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_key_matches(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);
	int64_t expected_key;
	int64_t expected_modifiers;
	uint32_t key;
	uint32_t modifiers;
	uint32_t locks = ST_CONSOLE_MODIFIER_CAPS_LOCK | ST_CONSOLE_MODIFIER_NUM_LOCK;

	if (event == NULL || !slot_integer(vm, 1, 0, ST_WREN_QUIT_KEY, &expected_key)
	    || !slot_integer(vm, 2, 0, UINT32_MAX, &expected_modifiers))
		return;
	if (expected_key >= 0xd800 && expected_key <= 0xdfff) {
		st_wren_abort(vm, "expected a Unicode scalar or named key");
		return;
	}
	bool matches = st_wren_input_key(&event->native, &key, &modifiers)
	    && key == (uint32_t)expected_key
	    && (modifiers & ~locks) == ((uint32_t)expected_modifiers & ~locks);
	wrenSetSlotBool(vm, 0, matches);
}

static void
fn_key_modifiers(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);
	uint32_t key;
	uint32_t modifiers;

	if (event != NULL) {
		(void)st_wren_input_key(&event->native, &key, &modifiers);
		wrenSetSlotDouble(vm, 0, modifiers);
	}
}

static void
fn_key_codepoint(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);

	if (event == NULL)
		return;
	if (event->native.type == ST_CONSOLE_EVENT_TEXT)
		wrenSetSlotDouble(vm, 0, event->native.data.text.codepoint);
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
	length = event->native.type == ST_CONSOLE_EVENT_TEXT
	    ? st_utf8_encode_one(encoded, sizeof(encoded),
	    event->native.data.text.codepoint) : 0;
	if (length <= 0)
		wrenSetSlotString(vm, 0, "");
	else
		wrenSetSlotBytes(vm, 0, encoded, (size_t)length);
}

static void
fn_key_byte(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);

	if (event == NULL)
		return;
	if (event->native.type == ST_CONSOLE_EVENT_CODEPAGE_BYTE)
		wrenSetSlotDouble(vm, 0, event->native.data.codepage_byte);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_key_scan_code(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);

	if (event == NULL)
		return;
	if (event->native.type == ST_CONSOLE_EVENT_DOORWAY_KEY)
		wrenSetSlotDouble(vm, 0, event->native.data.doorway_key.scan_code);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_key_enhanced(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);

	if (event == NULL)
		return;
	if (event->native.type == ST_CONSOLE_EVENT_DOORWAY_KEY)
		wrenSetSlotBool(vm, 0, event->native.data.doorway_key.enhanced);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_key_to_string(WrenVM *vm)
{
	struct foreign_key_event *event = (struct foreign_key_event *)foreign_kind(
	    vm, 0, FOREIGN_KEY_EVENT);
	uint32_t key = 0;
	uint32_t modifiers;
	char text[64];

	if (event == NULL)
		return;
	(void)st_wren_input_key(&event->native, &key, &modifiers);
	(void)snprintf(text, sizeof(text), "KeyEvent(type=%d, key=%u, modifiers=%u)",
	    (int)event->native.type, key, modifiers);
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
MOUSE_GETTER(wheel_x, wheel_x)
MOUSE_GETTER(wheel_y, wheel_y)

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
fn_physical_modifiers(WrenVM *vm)
{
	struct foreign_physical_key_event *event =
	    (struct foreign_physical_key_event *)foreign_kind(vm, 0,
	    FOREIGN_PHYSICAL_KEY_EVENT);

	if (event != NULL)
		wrenSetSlotDouble(vm, 0, event->modifiers);
}

static void
fn_physical_repeat(WrenVM *vm)
{
	struct foreign_physical_key_event *event =
	    (struct foreign_physical_key_event *)foreign_kind(vm, 0,
	    FOREIGN_PHYSICAL_KEY_EVENT);

	if (event != NULL)
		wrenSetSlotBool(vm, 0, event->repeat);
}

static void
fn_physical_to_string(WrenVM *vm)
{
	struct foreign_physical_key_event *event =
	    (struct foreign_physical_key_event *)foreign_kind(vm, 0,
	    FOREIGN_PHYSICAL_KEY_EVENT);
	char text[96];

	if (event == NULL)
		return;
	(void)snprintf(text, sizeof(text),
	    "PhysicalKeyEvent(%u, %s, modifiers=%u, repeat=%s)",
	    event->evdev, event->pressed ? "pressed" : "released",
	    event->modifiers, event->repeat ? "true" : "false");
	wrenSetSlotString(vm, 0, text);
}

static void
fn_screen_size(WrenVM *vm)
{
	uint32_t width;
	uint32_t height;
	if (!st_wren_screen_status(vm, st_console_size(st_wren_context_from_vm(vm)->console,
	    &width, &height), "Screen.size")) return;
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
	if (!st_wren_screen_status(vm, st_console_snapshot_create(&snapshot,
	    st_wren_context_from_vm(vm)->console), "Screen.save")) return;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "ScreenSnapshot", 1);
	struct foreign_snapshot *foreign = wrenSetSlotNewForeign(vm, 0, 1,
	    sizeof(*foreign));
	foreign->header.kind = FOREIGN_SNAPSHOT;
	foreign->snapshot = snapshot;
	memcpy(foreign->font_slots, st_wren_context_from_vm(vm)->fonts.slots,
	    sizeof(foreign->font_slots));
}

static void
fn_screen_restore(WrenVM *vm)
{
	struct foreign_snapshot *snapshot = (struct foreign_snapshot *)foreign_kind(vm, 1, FOREIGN_SNAPSHOT);
	bool restored = false;
	if (snapshot == NULL || snapshot->snapshot == NULL)
		return;
	int status = st_console_snapshot_restore(st_wren_context_from_vm(vm)->console,
	    snapshot->snapshot, &restored);
	if (restored)
		memcpy(st_wren_context_from_vm(vm)->fonts.slots, snapshot->font_slots,
		    sizeof(snapshot->font_slots));
	st_wren_screen_result(vm, status, "Screen.restore");
}

/* A row raster is owned by its Surface. Cell flags decide which parts still
 * belong to that raster after text/UI edits, so overlays need no pixel mask. */
static void
surface_drop_empty_pixels(struct surface_storage *storage, uint32_t row)
{
	if (storage->pixels == NULL || storage->pixels[row] == NULL)
		return;
	for (uint32_t x = 0; x < storage->width; ++x)
		if ((storage->cells[(size_t)row * storage->width + x].flags &
			ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0)
			return;
	st_pixel_buffer_destroy(storage->pixels[row]);
	storage->pixels[row] = NULL;
}

int
st_wren_surface_take_row_pixels(WrenVM *vm, int slot, uint32_t row,
	uint32_t source_columns, struct st_pixel_buffer *pixels)
{
	struct foreign_surface *surface = (struct foreign_surface *)foreign_kind(vm, slot, FOREIGN_SURFACE);
	int status = 0;
	if (surface == NULL || row >= surface->storage->height)
		status = IO_ERROR_INVALID_ARGUMENT;
	else {
		struct surface_storage *storage = surface->storage;
		if (pixels != NULL) {
			if (source_columns == 0 || pixels->width % source_columns != 0 ||
				pixels->width / source_columns == 0)
				status = IO_ERROR_INVALID_ARGUMENT;
			else {
				if (storage->pixel_cell_width == 0) {
					storage->pixel_cell_width = pixels->width / source_columns;
					storage->pixel_cell_height = pixels->height;
				}
				if ((uint64_t)storage->width * storage->pixel_cell_width != pixels->width ||
					pixels->height != storage->pixel_cell_height || source_columns != storage->width) {
					struct st_pixel_buffer *resized = NULL;
					status = st_history_resize_pixels(&resized, pixels, source_columns,
						storage->width, storage->pixel_cell_width, storage->pixel_cell_height);
					if (status == 0) {
						st_pixel_buffer_destroy(pixels);
						pixels = resized;
					}
				}
				if (status == 0 && storage->pixels == NULL) {
					storage->pixels = calloc(storage->height, sizeof(*storage->pixels));
					if (storage->pixels == NULL)
						status = IO_ERROR_OUT_OF_MEMORY;
				}
			}
		}
		if (status == 0 && storage->pixels != NULL) {
			st_pixel_buffer_destroy(storage->pixels[row]);
			storage->pixels[row] = pixels;
			pixels = NULL;
		}
	}
	st_pixel_buffer_destroy(pixels);
	return status;
}

static int
surface_copy_pixel_row(struct surface_storage *destination, uint32_t dx, uint32_t dy,
	const struct surface_storage *source, uint32_t sx, uint32_t sy, uint32_t columns)
{
	if (source->pixels == NULL || source->pixels[sy] == NULL)
		return 0;
	bool graphics = false;
	for (uint32_t x = 0; x < columns; ++x)
		graphics |= (source->cells[(size_t)sy * source->width + sx + x].flags &
			ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0;
	if (!graphics)
		return 0;
	if (destination->pixel_cell_width == 0) {
		destination->pixel_cell_width = source->pixel_cell_width;
		destination->pixel_cell_height = source->pixel_cell_height;
	}
	if (destination->pixels == NULL) {
		destination->pixels = calloc(destination->height, sizeof(*destination->pixels));
		if (destination->pixels == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
	}
	struct st_pixel_buffer *converted = NULL;
	const struct st_pixel_buffer *pixels = source->pixels[sy];
	int status = 0;
	if (source->pixel_cell_width != destination->pixel_cell_width ||
		source->pixel_cell_height != destination->pixel_cell_height) {
		status = st_history_resize_pixels(&converted, pixels, source->width, source->width,
			destination->pixel_cell_width, destination->pixel_cell_height);
		pixels = converted;
	}
	if (status == 0 && destination->pixels[dy] == NULL) {
		if (destination->width > UINT32_MAX / destination->pixel_cell_width)
			status = IO_ERROR_OUT_OF_RANGE;
		else
			status = st_pixel_buffer_create(&destination->pixels[dy],
				destination->width * destination->pixel_cell_width,
				destination->pixel_cell_height, pixels->alternate != NULL);
	}
	if (status == 0) {
		const struct st_pixel_blit blit = {.source_x = sx * destination->pixel_cell_width,
			.width = columns * destination->pixel_cell_width,
			.height = destination->pixel_cell_height,
			.destination_x = (int32_t)(dx * destination->pixel_cell_width),
			.scale_x = 1, .scale_y = 1};
		status = st_pixel_buffer_blit(destination->pixels[dy], pixels, NULL, &blit);
	}
	st_pixel_buffer_destroy(converted);
	return status;
}

static int
surface_draw_pixels(st_console_t *console, const struct surface_storage *storage,
	const struct st_console_rect *source, const struct st_console_rect *destination)
{
	if (storage->pixels == NULL)
		return 0;
	uint32_t cw, ch;
	int status = st_console_cell_pixel_size(console, &cw, &ch);
	if (status != 0 || cw == 0 || ch == 0)
		return status;
	for (uint32_t y = 0; y < source->height && status == 0; ++y) {
		const struct st_pixel_buffer *pixels = storage->pixels[source->y + y];
		struct st_pixel_buffer *converted = NULL;
		if (pixels == NULL)
			continue;
		if (cw != storage->pixel_cell_width || ch != storage->pixel_cell_height) {
			status = st_history_resize_pixels(&converted, pixels, storage->width,
				storage->width, cw, ch);
			if (status != 0)
				break;
			pixels = converted;
		}
		for (uint32_t x = 0; x < source->width && status == 0;) {
			if ((storage->cells[(size_t)(source->y + y) * storage->width + source->x + x].flags &
				ST_CONSOLE_CELL_PIXEL_GRAPHICS) == 0) {
				++x;
				continue;
			}
			uint32_t first = x++;
			while (x < source->width && (storage->cells[(size_t)(source->y + y) * storage->width +
				source->x + x].flags & ST_CONSOLE_CELL_PIXEL_GRAPHICS) != 0)
				++x;
			const struct st_pixel_blit blit = {.source_x = (source->x + first) * cw,
				.width = (x - first) * cw, .height = ch,
				.destination_x = (int32_t)((destination->x + first) * cw),
				.destination_y = (int32_t)((destination->y + y) * ch), .scale_x = 1, .scale_y = 1};
			status = st_console_blit_pixels(console, pixels, NULL, &blit);
		}
		st_pixel_buffer_destroy(converted);
	}
	return status;
}

static bool
read_rect_arguments(WrenVM *vm, int first, struct st_console_rect *rect)
{
	double values[4];

	for (size_t i = 0; i < 4; ++i) {
		if (wrenGetSlotType(vm, first + (int)i) != WREN_TYPE_NUM) {
			st_wren_abort(vm, "expected an integer");
			return false;
		}
		values[i] = wrenGetSlotDouble(vm, first + (int)i);
		if (!isfinite(values[i]) || trunc(values[i]) != values[i]) {
			st_wren_abort(vm, "expected an integer");
			return false;
		}
		if (values[i] < 1.0 || values[i] > (double)UINT32_MAX) {
			(void)st_wren_screen_status(vm, IO_ERROR_OUT_OF_RANGE, "Screen.readRect region");
			return false;
		}
	}
	if (values[2] < values[0] || values[3] < values[1]) {
		(void)st_wren_screen_status(vm, IO_ERROR_OUT_OF_RANGE, "Screen.readRect region");
		return false;
	}
	rect->x = (uint32_t)values[0] - 1;
	rect->y = (uint32_t)values[1] - 1;
	rect->width = (uint32_t)(values[2] - values[0] + 1.0);
	rect->height = (uint32_t)(values[3] - values[1] + 1.0);
	return true;
}

static void
fn_screen_read_rect(WrenVM *vm)
{
	struct st_console_rect rect;
	struct st_console_style style;
	struct surface_storage *storage;
	struct foreign_surface *surface;
	uint32_t screen_width;
	uint32_t screen_height;
	st_console_t *console = st_wren_context_from_vm(vm)->console;

	if (!read_rect_arguments(vm, 1, &rect))
		return;
	if (!st_wren_screen_status(vm, st_console_size(console, &screen_width, &screen_height), "Screen.readRect size"))
		return;
	if (rect.x >= screen_width || rect.y >= screen_height
	    || rect.width > screen_width - rect.x
	    || rect.height > screen_height - rect.y) {
		(void)st_wren_screen_status(vm, IO_ERROR_OUT_OF_RANGE, "Screen.readRect region");
		return;
	}
	if (!st_wren_screen_status(vm, st_console_get_style(console, &style), "Screen.readRect style"))
		return;
	storage = storage_create(rect.width, rect.height, &style,
	    st_wren_context_from_vm(vm)->console, st_wren_host_from_vm(vm));
	if (storage == NULL) {
		(void)st_wren_screen_status(vm, IO_ERROR_OUT_OF_MEMORY, "Screen.readRect storage");
		return;
	}
	if (!st_wren_screen_status(vm, st_console_read_cells(console, &rect, storage->cells,
	    rect.width), "Screen.readRect")) {
		storage_release(storage);
		return;
	}
	uint32_t cw, ch;
	int status = st_console_cell_pixel_size(console, &cw, &ch);
	for (uint32_t row = 0; status == 0 && row < rect.height; ++row) {
		struct st_pixel_buffer *pixels = NULL;
		const struct st_console_rect region = {rect.x * cw, (rect.y + row) * ch, rect.width * cw, ch};
		status = st_history_capture_pixels(&pixels, console,
			storage->cells + (size_t)row * rect.width, rect.width, &region);
		if (status == 0 && pixels != NULL) {
			if (storage->pixels == NULL) {
				storage->pixels = calloc(rect.height, sizeof(*storage->pixels));
				if (storage->pixels == NULL)
					status = IO_ERROR_OUT_OF_MEMORY;
			}
			if (status == 0) {
				storage->pixel_cell_width = cw;
				storage->pixel_cell_height = ch;
				storage->pixels[row] = pixels;
				pixels = NULL;
			}
		}
		st_pixel_buffer_destroy(pixels);
	}
	if (!st_wren_screen_status(vm, status, "Screen.readRect pixels")) {
		storage_release(storage);
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "Surface", 1);
	surface = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*surface));
	surface->header.kind = FOREIGN_SURFACE;
	surface->storage = storage;
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

	if (surface == NULL || !st_wren_screen_status(vm, st_console_size(st_wren_context_from_vm(vm)->console, &screen_width,
	    &screen_height), "Screen.putRect size"))
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
	    screen_height, &source_rect, &destination)) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	st_console_t *console = st_wren_context_from_vm(vm)->console;
	int status = st_console_begin_update(console);
	if (status == 0) {
		status = st_console_write_complete_cells(console, &destination,
			&surface->storage->cells[(size_t)source_rect.y * surface->storage->width + source_rect.x],
			surface->storage->width);
		if (status == 0)
			status = surface_draw_pixels(console, surface->storage, &source_rect, &destination);
		int finished = st_console_end_update(console);
		if (status == 0)
			status = finished;
	}
	st_wren_screen_result(vm, status, "Screen.putRect");
}

static void fn_screen_put_rect(WrenVM *vm) { screen_put_rect(vm, false); }
static void fn_screen_put_rect_partial(WrenVM *vm) { screen_put_rect(vm, true); }

struct screen_update_guard {
	st_console_t *console;
};

static void
allocate_screen_update(WrenVM *vm)
{
	st_console_t *console = st_wren_context_from_vm(vm)->console;
	if (!st_wren_screen_status(vm, st_console_begin_update(console), "Screen.update begin"))
		return;
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "ScreenUpdate_", 1);
	struct screen_update_guard *guard = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*guard));
	guard->console = console;
}

static void
finalize_screen_update(void *data)
{
	struct screen_update_guard *guard = data;

	if (guard->console != NULL)
		(void)st_console_end_update(guard->console);
}

static void
fn_screen_update_finish(WrenVM *vm)
{
	struct screen_update_guard *guard = wrenGetSlotForeign(vm, 0);
	st_console_t *console = guard->console;

	/* End can report a presentation failure after closing the batch. */
	guard->console = NULL;
	st_wren_screen_result(vm, console == NULL ? 0 : st_console_end_update(console), "Screen.update end");
}

static uint32_t
legacy_colour(uint32_t colour)
{
	static const uint8_t map[16] = {
		0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
	};

	return map[colour & 0x0f];
}

struct legacy_ycocg {
	int32_t y;
	int32_t co;
	int32_t cg;
};

static int32_t
floor_half(int32_t value)
{
	return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

static struct legacy_ycocg
rgb_to_ycocg(uint32_t rgb)
{
	int32_t red = (int32_t)((rgb >> 16) & UINT32_C(0xff));
	int32_t green = (int32_t)((rgb >> 8) & UINT32_C(0xff));
	int32_t blue = (int32_t)(rgb & UINT32_C(0xff));
	int32_t co = red - blue;
	int32_t temporary = blue + floor_half(co);
	int32_t cg = green - temporary;

	return (struct legacy_ycocg){temporary + floor_half(cg), co, cg};
}

static uint32_t
ycocg_distance(uint32_t left, uint32_t right)
{
	struct legacy_ycocg x = rgb_to_ycocg(left);
	struct legacy_ycocg y = rgb_to_ycocg(right);
	uint32_t dy = (uint32_t)(x.y > y.y ? x.y - y.y : y.y - x.y);
	uint32_t dco = (uint32_t)(x.co > y.co ? x.co - y.co : y.co - x.co);
	uint32_t dcg = (uint32_t)(x.cg > y.cg ? x.cg - y.cg : y.cg - x.cg);

	return dy + (dco >> 1) + (dcg >> 1);
}

static bool
console_palette16(WrenVM *vm, uint32_t palette[16], uint32_t *video_flags)
{
	st_console_t *console = st_wren_context_from_vm(vm)->console;

	return st_wren_screen_status(vm, st_console_get_palette(console, 0, palette, 16), "Palette read")
	    && st_wren_screen_status(vm, st_console_get_video_flags(console, video_flags), "VideoFlags read");
}

static bool
native_colour_rgb(WrenVM *vm, uint32_t colour, uint32_t *rgb)
{
	if (ST_CONSOLE_COLOR_IS_RGB(colour)) {
		*rgb = colour & UINT32_C(0xffffff);
		return true;
	}
	if (colour > UINT16_MAX) {
		st_wren_abort(vm, "cell colour is invalid");
		return false;
	}
	return st_wren_screen_status(vm, st_console_get_palette(st_wren_context_from_vm(vm)->console,
	    (uint16_t)colour, rgb, 1), "Palette read");
}

static bool
nearest_legacy_base(WrenVM *vm, uint32_t colour, const uint32_t palette[16],
	bool high, uint8_t *result)
{
	uint32_t target;
	uint32_t best_distance = UINT32_MAX;
	uint8_t best = 0;

	if (!native_colour_rgb(vm, colour, &target))
		return false;
	for (uint8_t i = 0; i < 8; ++i) {
		uint8_t attribute_index = (uint8_t)(i | (high ? 8 : 0));
		uint32_t distance = ycocg_distance(target,
		    palette[legacy_colour(attribute_index)]);

		if (distance < best_distance) {
			best_distance = distance;
			best = i;
		}
	}
	*result = best;
	return true;
}

static bool
cell_legacy_attr(WrenVM *vm, const struct st_console_cell *cell,
	uint8_t *attribute)
{
	uint32_t palette[16];
	uint32_t video_flags;
	bool bright = (cell->flags & ST_CONSOLE_CELL_BRIGHT) != 0;
	bool blink = (cell->flags & ST_CONSOLE_CELL_BLINK) != 0;
	uint8_t foreground;
	uint8_t background;

	if (!console_palette16(vm, palette, &video_flags))
		return false;
	if ((cell->flags & ST_CONSOLE_CELL_LEGACY_FOREGROUND) != 0
	    && !ST_CONSOLE_COLOR_IS_RGB(cell->foreground)
	    && cell->foreground < 8)
		foreground = (uint8_t)legacy_colour(cell->foreground);
	else if (!nearest_legacy_base(vm, cell->foreground, palette,
	    bright && (video_flags & ST_CONSOLE_VIDEO_NO_BRIGHT) == 0, &foreground))
		return false;
	if ((cell->flags & ST_CONSOLE_CELL_LEGACY_BACKGROUND) != 0
	    && !ST_CONSOLE_COLOR_IS_RGB(cell->background)
	    && cell->background < 8)
		background = (uint8_t)legacy_colour(cell->background);
	else if (!nearest_legacy_base(vm, cell->background, palette,
	    blink && (video_flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) != 0, &background))
		return false;
	*attribute = (uint8_t)(foreground | (background << 4)
	    | (bright ? UINT8_C(0x08) : 0)
	    | (blink ? UINT8_C(0x80) : 0));
	return true;
}

static void
set_cell_legacy_attr(struct st_console_cell *cell, uint8_t attribute)
{
	cell->foreground = legacy_colour(attribute & UINT8_C(0x07));
	cell->background = legacy_colour((attribute >> 4) & UINT8_C(0x07));
	cell->flags |= ST_CONSOLE_CELL_LEGACY_FOREGROUND
	    | ST_CONSOLE_CELL_LEGACY_BACKGROUND;
	if ((attribute & UINT8_C(0x08)) != 0)
		cell->flags |= ST_CONSOLE_CELL_BRIGHT;
	else
		cell->flags &= ~ST_CONSOLE_CELL_BRIGHT;
	if ((attribute & UINT8_C(0x80)) != 0)
		cell->flags |= ST_CONSOLE_CELL_BLINK;
	else
		cell->flags &= ~ST_CONSOLE_CELL_BLINK;
}

static bool
resolve_screen_style_font(WrenVM *vm, struct st_console_style *style)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_console_font_selection selection = {
		.normal = 1,
		.bright = 2,
		.blink = 3,
		.bright_blink = 4,
	};
	uint32_t video_flags;
	uint16_t slot;
	uint16_t identifier;

	if (!st_wren_screen_status(vm, st_console_get_video_flags(context->console, &video_flags), "VideoFlags read")) {
		return false;
	}
	selection.bright_selects_font
	    = (video_flags & ST_CONSOLE_VIDEO_ALT_CHARS) != 0;
	selection.blink_selects_font
	    = (video_flags & ST_CONSOLE_VIDEO_BLINK_ALT_CHARS) != 0;
	slot = st_console_font_for_flags(&selection, style->flags);
	if (!st_wren_font_identifier(context, context->fonts.slots[slot],
	    &identifier)) {
		st_wren_abort(vm, "selected font is not available");
		return false;
	}
	style->font = identifier;
	return true;
}

static bool
refresh_screen_style_font(WrenVM *vm)
{
	st_console_t *console = st_wren_context_from_vm(vm)->console;
	struct st_console_style style;

	if (!st_wren_screen_status(vm, st_console_get_style(console, &style), "Screen style read")) {
		return false;
	}
	if (!resolve_screen_style_font(vm, &style))
		return false;
	if (!st_wren_screen_status(vm, st_console_set_style(console, &style), "Screen style update")) {
		return false;
	}
	return true;
}

static void
fn_screen_hyperlink(WrenVM *vm)
{
	struct st_console_style style;

	if (!st_wren_screen_status(vm, st_console_get_style(st_wren_context_from_vm(vm)->console, &style), "Screen.hyperlinkId read")) {
		return;
	}
	wrenSetSlotDouble(vm, 0, style.hyperlink);
}

static void
fn_screen_hyperlink_set(WrenVM *vm)
{
	struct st_console_style style;
	int64_t value;

	if (!slot_integer(vm, 1, 0, UINT16_MAX, &value))
		return;
	if (!st_wren_screen_status(vm, st_console_get_style(st_wren_context_from_vm(vm)->console, &style), "Screen.hyperlinkId read")) {
		return;
	}
	style.hyperlink = (uint16_t)value;
	st_wren_screen_result(vm, st_console_set_style(st_wren_context_from_vm(vm)->console, &style), "Screen.hyperlinkId update");
}

static void
fn_screen_attr(WrenVM *vm)
{
	struct st_console_style style;
	struct st_console_cell cell;
	uint8_t value;

	if (!st_wren_screen_status(vm, st_console_get_style(st_wren_context_from_vm(vm)->console, &style), "Screen.attr")) {
		return;
	}
	cell = (struct st_console_cell){.foreground = style.foreground,
	    .background = style.background, .flags = style.flags};
	if (cell_legacy_attr(vm, &cell, &value))
		wrenSetSlotDouble(vm, 0, value);
}

static void
fn_screen_attr_set(WrenVM *vm)
{
	struct st_console_style style;
	int64_t value;

	if (!slot_integer(vm, 1, 0, 255, &value)
	    || !st_wren_screen_status(vm, st_console_get_style(st_wren_context_from_vm(vm)->console,
	    &style), "Screen.attr read"))
		return;
	struct st_console_cell cell = {.foreground = style.foreground,
	    .background = style.background, .flags = style.flags};

	set_cell_legacy_attr(&cell, (uint8_t)value);
	style.foreground = cell.foreground;
	style.background = cell.background;
	style.flags = cell.flags;
	if (!resolve_screen_style_font(vm, &style))
		return;
	st_wren_screen_result(vm, st_console_set_style(st_wren_context_from_vm(vm)->console,
	    &style), "Screen.attr");
}

static void
fn_screen_window_position(WrenVM *vm)
{
	struct st_console_rect window;
	uint32_t x;
	uint32_t y;

	if (!st_wren_screen_status(vm, st_console_get_position(st_wren_context_from_vm(vm)->console,
	    &x, &y), "Screen.window.position") || !st_wren_screen_status(vm, st_console_get_window(
	    st_wren_context_from_vm(vm)->console, &window), "Screen.window.bounds")) return;
	if (x < window.x || y < window.y
	    || x - window.x >= window.width || y - window.y >= window.height) {
		st_wren_abort(vm, "Screen.window.position");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, x - window.x + 1);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, y - window.y + 1);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_screen_window_position_set(WrenVM *vm)
{
	struct st_console_rect window;
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
	if (!st_wren_screen_status(vm, st_console_get_window(st_wren_context_from_vm(vm)->console,
	    &window), "Screen.window.bounds")) return;
	if ((uint64_t)x > window.width || (uint64_t)y > window.height) {
		st_wren_abort(vm, "Screen.window.position is outside the window");
		return;
	}
	st_wren_screen_result(vm, st_console_set_position(st_wren_context_from_vm(vm)->console,
	    window.x + (uint32_t)x - 1, window.y + (uint32_t)y - 1), "Screen.window.setPosition");
}

static void
fn_screen_window_bounds(WrenVM *vm)
{
	struct st_console_rect window;

	if (!st_wren_screen_status(vm, st_console_get_window(st_wren_context_from_vm(vm)->console,
	    &window), "Screen.window.bounds")) {
		return;
	}
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
	if (values[2] < values[0] || values[3] < values[1]) {
		st_wren_abort(vm, "Screen.window.bounds update");
		return;
	}
	window = (struct st_console_rect){(uint32_t)values[0] - 1,
	    (uint32_t)values[1] - 1, (uint32_t)(values[2] - values[0] + 1),
	    (uint32_t)(values[3] - values[1] + 1)};
	st_wren_screen_result(vm, st_console_set_window(st_wren_context_from_vm(vm)->console,
	    &window), "Screen.window.bounds update");
}

static void
fn_screen_window_clear(WrenVM *vm)
{
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_clear_window(st_wren_context_from_vm(vm)->console,
		    ' '), "Screen.window.clear");
}

static void
fn_screen_window_clear_line(WrenVM *vm)
{
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_clear_line(st_wren_context_from_vm(vm)->console,
		    ' '), "Screen.window.clearToLineEnd");
}

static void
fn_screen_window_delete_line(WrenVM *vm)
{
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_delete_lines(st_wren_context_from_vm(vm)->console,
		    1, ' '), "Screen.window.deleteLine");
}

static void
fn_screen_window_insert_line(WrenVM *vm)
{
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_insert_lines(st_wren_context_from_vm(vm)->console,
		    1, ' '), "Screen.window.insertLine");
}

static void
fn_screen_window_scroll(WrenVM *vm)
{
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_scroll(st_wren_context_from_vm(vm)->console,
		    1, ' '), "Screen.window.scroll");
}

static void
fn_screen_window_put_char(WrenVM *vm)
{
	int64_t value;

	if (!slot_integer(vm, 1, 0, 0x10ffff, &value))
		return;
	if (value >= 0xd800 && value <= 0xdfff) {
		st_wren_abort(vm, "Screen.window.putChar expects a Unicode scalar");
		return;
	}
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_put_codepoint(st_wren_context_from_vm(vm)->console,
		    (uint32_t)value), "Screen.window.putChar");
}

static void
fn_screen_window_print(WrenVM *vm)
{
	int length;
	const char *text;

	if (!st_wren_text_slot(vm, 1, &text, &length)) {
		st_wren_abort(vm, "Screen.window.print expects a String");
		return;
	}
	if (refresh_screen_style_font(vm))
		st_wren_screen_result(vm, st_console_write_utf8(st_wren_context_from_vm(vm)->console,
		    text, (size_t)length), "Screen.window.print");
}



static void
fn_screen_text_width(WrenVM *vm)
{
	const unsigned char *text;
	st_unicode_text_t *graphemes = NULL;
	struct st_unicode_text_item item;
	int length;
	int64_t selector;
	uint16_t identifier;
	size_t consumed = 0;
	uint64_t width = 0;
	int status;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_STRING
	    || !slot_integer(vm, 2, 0, UINT8_MAX, &selector)) {
		st_wren_abort(vm, "Screen.textWidth expects a String and font");
		return;
	}
	if (!st_wren_font_identifier(st_wren_context_from_vm(vm), (uint16_t)selector, &identifier)) {
		st_wren_abort(vm, "font is not available");
		return;
	}
	{
		const char *normalized;

		if (!st_wren_text_slot(vm, 1, &normalized, &length))
			return;
		text = (const unsigned char *)normalized;
	}
	status = st_unicode_text_create(&graphemes, ST_UNICODE_TEXT_GRAPHEMES);
	if (status == 0)
		status = st_unicode_text_feed(graphemes, text, (size_t)length,
		    &consumed);
	if (status == 0 && consumed != (size_t)length)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0)
		status = st_unicode_text_finish(graphemes);
	if (status != 0) {
		st_unicode_text_destroy(&graphemes);
		(void)st_wren_screen_status(vm, status, "Screen.textWidth");
		return;
	}
	while (st_unicode_text_peek(graphemes, &item) == 0) {
		uint32_t glyph;
		uint16_t glyph_font;
		uint8_t columns = item.columns;

		if (columns == 0) {
			(void)st_unicode_text_consume(graphemes);
			continue;
		}
		if (st_console_font_lookup_cluster(
		    st_wren_context_from_vm(vm)->console, identifier,
		    (const char *)item.bytes, item.size, item.columns, &glyph_font,
		    &glyph) == 0) {
			(void)st_console_font_glyph_columns(
			    st_wren_context_from_vm(vm)->console, glyph_font, glyph,
			    &columns);
		}
		width += columns;
		(void)st_unicode_text_consume(graphemes);
	}
	st_unicode_text_destroy(&graphemes);
	wrenSetSlotDouble(vm, 0, (double)width);
}

#define SUPPORT_GETTER(name, flag) \
	static void fn_support_##name(WrenVM *vm) \
	{ \
		uint32_t value; \
		if (st_wren_screen_status(vm, st_console_capabilities(st_wren_context_from_vm(vm)->console, \
		    &value), "Screen.supports")) \
			wrenSetSlotBool(vm, 0, (value & (flag)) != 0); \
	}

SUPPORT_GETTER(loadable_fonts, ST_CONSOLE_CAP_LOADABLE_FONTS)
SUPPORT_GETTER(palette_change, ST_CONSOLE_CAP_PALETTE_CHANGE)
SUPPORT_GETTER(pixels, ST_CONSOLE_CAP_PIXELS)
SUPPORT_GETTER(custom_cursor, ST_CONSOLE_CAP_CUSTOM_CURSOR)
SUPPORT_GETTER(window_title, ST_CONSOLE_CAP_WINDOW_TITLE)
SUPPORT_GETTER(clipboard, ST_CONSOLE_CAP_CLIPBOARD)
SUPPORT_GETTER(open_url, ST_CONSOLE_CAP_OPEN_URL)
SUPPORT_GETTER(extended_palette, ST_CONSOLE_CAP_EXTENDED_PALETTE)
SUPPORT_GETTER(blocky_scaling, ST_CONSOLE_CAP_BLOCKY_SCALING)
SUPPORT_GETTER(external_scaling, ST_CONSOLE_CAP_EXTERNAL_SCALING)
SUPPORT_GETTER(bright_background, ST_CONSOLE_CAP_BRIGHT_BACKGROUND)

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
	{
		const char *normalized;

		if (!st_wren_text_slot(vm, 1, &normalized, &length))
			return;
		text = (const unsigned char *)normalized;
	}
	if (length <= 0 || st_utf8_decode_one(&codepoint, &consumed, text,
	    (size_t)length) != 0 || consumed != (size_t)length) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	wrenSetSlotBool(vm, 0, st_codepage_from_unicode(&encoded,
	    font_codepage(st_wren_context_from_vm(vm), st_wren_context_from_vm(vm)->fonts.slots[0]),
	    codepoint));
}

static enum st_codepage
font_codepage(struct st_wren_context *context, uint16_t selector)
{
	uint16_t identifier;
	enum st_codepage codepage;

	return st_wren_font_metadata(context, selector, &identifier, &codepage)
	    ? codepage : ST_CODEPAGE_COUNT;
}

static void
fn_codepage_encode_byte(WrenVM *vm)
{
	int64_t codepage;
	int64_t scalar;
	uint8_t byte;

	if (!slot_integer(vm, 1, 0, ST_CODEPAGE_COUNT - 1, &codepage)
	    || !slot_integer(vm, 2, 0, 0x10ffff, &scalar))
		return;
	if (scalar >= 0xd800 && scalar <= 0xdfff) {
		st_wren_abort(vm, "Codepage.encodeByte expects a Unicode scalar");
		return;
	}
	if (st_codepage_from_unicode(&byte, (enum st_codepage)codepage, (uint32_t)scalar))
		wrenSetSlotDouble(vm, 0, byte);
	else
		wrenSetSlotNull(vm, 0);
}

static void
fn_codepage_broken_vertical(WrenVM *vm)
{
	int64_t codepage;

	if (slot_integer(vm, 1, 0, ST_CODEPAGE_COUNT - 1, &codepage))
		wrenSetSlotBool(vm, 0, st_codepage_broken_vertical((enum st_codepage)codepage));
}

static void
codepage_decode_byte(WrenVM *vm, bool explicit_graphics)
{
	int64_t codepage;
	int64_t byte;
	bool graphic_controls = true;
	char text[4];
	ptrdiff_t size;

	if (!slot_integer(vm, 1, 0, ST_CODEPAGE_COUNT - 1, &codepage)
	    || !slot_integer(vm, 2, 0, UINT8_MAX, &byte)
	    || (explicit_graphics && !slot_boolean(vm, 3, &graphic_controls)))
		return;
	size = st_utf8_encode_one(text, sizeof(text), st_codepage_to_unicode(
	    (enum st_codepage)codepage, (uint8_t)byte, graphic_controls));
	wrenSetSlotBytes(vm, 0, text, (size_t)size);
}

static void fn_codepage_decode_byte(WrenVM *vm) { codepage_decode_byte(vm, false); }
static void fn_codepage_decode_byte_graphics(WrenVM *vm) { codepage_decode_byte(vm, true); }

static void
fn_font_builtin_broken_vertical(WrenVM *vm)
{
	int64_t selector;

	if (slot_integer(vm, 1, 0, ST_BUILTIN_FONT_COUNT - 1, &selector))
		wrenSetSlotBool(vm, 0, st_builtin_font_broken_vertical((size_t)selector));
}

static void
fn_font_name(WrenVM *vm)
{
	int64_t selector;
	const char *name = NULL;
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	char runtime_name[40];

	if (!slot_integer(vm, 1, 0, UINT8_MAX, &selector))
		return;
	uint16_t identifier;
	if (context->fonts.runtime_loaded[selector]
	    || (selector >= ST_BUILTIN_FONT_COUNT + (int64_t)st_settings_font_count()
	    && st_wren_font_identifier(context, (uint16_t)selector, &identifier))) {
		(void)snprintf(runtime_name, sizeof(runtime_name),
		    "Loaded font %d", (int)selector);
		wrenSetSlotString(vm, 0, runtime_name);
		return;
	}
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
	struct st_wren_font_state *fonts =
	    &st_wren_context_from_vm(vm)->fonts;
	size_t count = ST_BUILTIN_FONT_COUNT + fonts->loaded_count;

	for (size_t i = count; i <= UINT8_MAX; ++i) {
		uint16_t identifier;
		if (fonts->runtime_loaded[i] || st_wren_font_identifier(st_wren_context_from_vm(vm), (uint16_t)i, &identifier))
			count = i + 1;
	}
	wrenSetSlotDouble(vm, 0, (double)count);
}

static void
allocate_font_error(WrenVM *vm)
{
	struct foreign_font_error *error = wrenSetSlotNewForeign(vm, 0, 0, sizeof(*error));
	*error = (struct foreign_font_error){.header.kind = FOREIGN_FONT_ERROR};
}

void
st_wren_push_font_error(WrenVM *vm, enum st_wren_font_error code,
	int native_code, const char *message)
{
	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "FontError", 1);
	struct foreign_font_error *error = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*error));
	/* Diagnostics are static literals, never borrowed caller strings. */
	*error = (struct foreign_font_error){
		.header.kind = FOREIGN_FONT_ERROR, .code = code,
		.native_code = native_code, .message = message,
	};
}

static struct foreign_font_error *
font_error_value(WrenVM *vm)
{
	return (struct foreign_font_error *)foreign_kind(vm, 0, FOREIGN_FONT_ERROR);
}
static void fn_font_error_code(WrenVM *vm)
{
	struct foreign_font_error *error = font_error_value(vm);
	if (error != NULL) wrenSetSlotDouble(vm, 0, error->code);
}
static void fn_font_error_native_code(WrenVM *vm)
{
	struct foreign_font_error *error = font_error_value(vm);
	if (error != NULL) wrenSetSlotDouble(vm, 0, error->native_code);
}
static void fn_font_error_message(WrenVM *vm)
{
	struct foreign_font_error *error = font_error_value(vm);
	if (error != NULL) wrenSetSlotString(vm, 0, error->message == NULL ? "" : error->message);
}
static void fn_font_error_to_string(WrenVM *vm)
{
	struct foreign_font_error *error = font_error_value(vm);
	char text[192];
	if (error == NULL) return;
	(void)snprintf(text, sizeof(text), "FontError(%d): %s", (int)error->code,
	    error->message == NULL ? "" : error->message);
	wrenSetSlotString(vm, 0, text);
}

static void
font_load(WrenVM *vm, bool explicit_codepage)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	const char *path;
	uint32_t capabilities = 0;
	uint16_t identifier;
	int64_t value = 0;
	enum st_codepage codepage;
	size_t first;
	int status;

	if (!st_wren_foreign_is(vm, 1, "syncterm", "File")) {
		st_wren_abort(vm, "Font.load expects a readable File");
		return;
	}
	if (explicit_codepage && !slot_integer(vm, 2, 0, ST_CODEPAGE_COUNT - 1, &value))
		return;
	path = st_wren_file_read_path(vm, 1);
	if (path == NULL) {
		st_wren_push_file_error(vm, IO_ERROR_PERMISSION_DENIED, 0, "Font read grant is unavailable");
		return;
	}
	if (context->cterm == NULL) {
		st_wren_push_font_error(vm, ST_WREN_FONT_UNAVAILABLE, 0, "No active terminal");
		return;
	}
	status = st_console_capabilities(context->console, &capabilities);
	if (status != 0 || (capabilities & ST_CONSOLE_CAP_LOADABLE_FONTS) == 0) {
		st_wren_push_font_error(vm, ST_WREN_FONT_UNAVAILABLE, status, "Font loading is unavailable");
		return;
	}
	codepage = explicit_codepage ? (enum st_codepage)value : st_cterm_codepage(context->cterm);
	first = ST_BUILTIN_FONT_COUNT + context->fonts.loaded_count;
	if (first < ST_FONT_CATALOG_FIRST_CUSTOM)
		first = ST_FONT_CATALOG_FIRST_CUSTOM;
	for (size_t selector = first; selector <= UINT8_MAX; ++selector) {
		struct st_font_load_error error;

		status = st_cterm_register_font(context->cterm, (uint8_t)selector,
		    path, codepage, &identifier, &error);
		if (status == IO_ERROR_ALREADY_EXISTS)
			continue;
		if (status != 0) {
			if (error.failure == ST_FONT_LOAD_FILE)
				st_wren_push_file_error(vm, status, error.native_error, "Font file could not be read");
			else {
				enum st_wren_font_error code = ST_WREN_FONT_NATIVE_FAILURE;
				const char *message = "Font registration failed";

				if (error.failure == ST_FONT_LOAD_BITMAP) {
					code = ST_WREN_FONT_INVALID_BITMAP;
					message = "Bitmap must contain 256 glyphs matching the current cell size";
				}
				else if (error.failure == ST_FONT_LOAD_CODEPAGE) {
					code = ST_WREN_FONT_UNSUPPORTED_CODEPAGE;
					message = "No character mapping is available for the requested codepage";
				}
				st_wren_push_font_error(vm, code, status, message);
			}
			return;
		}
		context->fonts.catalog[selector] = identifier;
		context->fonts.catalog_valid[selector] = true;
		context->fonts.runtime_loaded[selector] = true;
		context->fonts.codepages[selector] = codepage;
		context->fonts.selector_by_identifier[identifier] = (uint16_t)selector;
		wrenSetSlotDouble(vm, 0, (double)selector);
		return;
	}
	st_wren_push_font_error(vm, ST_WREN_FONT_NO_SLOTS, 0, "No free font selectors");
}
static void fn_font_load(WrenVM *vm) { font_load(vm, false); }
static void fn_font_load_codepage(WrenVM *vm) { font_load(vm, true); }

static void
fn_font_available(WrenVM *vm)
{
	int64_t selector;
	uint16_t identifier;
	if (slot_integer(vm, 1, 0, UINT8_MAX, &selector))
		wrenSetSlotBool(vm, 0, st_wren_font_identifier(st_wren_context_from_vm(vm),
		    (uint16_t)selector, &identifier));
}

static void
return_font_codepage(WrenVM *vm, uint16_t selector)
{
	enum st_codepage codepage = font_codepage(st_wren_context_from_vm(vm), selector);
	if (codepage == ST_CODEPAGE_COUNT) wrenSetSlotNull(vm, 0);
	else wrenSetSlotDouble(vm, 0, codepage);
}
static void fn_font_codepage(WrenVM *vm)
{
	return_font_codepage(vm, st_wren_context_from_vm(vm)->fonts.slots[0]);
}
static void fn_font_codepage_of(WrenVM *vm)
{
	int64_t slot;
	if (slot_integer(vm, 1, 0, 4, &slot))
		return_font_codepage(vm, st_wren_context_from_vm(vm)->fonts.slots[slot]);
}
static void fn_font_codepage_for(WrenVM *vm)
{
	int64_t selector;
	if (slot_integer(vm, 1, 0, UINT8_MAX, &selector))
		return_font_codepage(vm, (uint16_t)selector);
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
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	int64_t slot;
	int64_t font;

	if (slot_integer(vm, 1, 0, 4, &slot)
	    && slot_integer(vm, 2, 0, UINT16_MAX, &font)) {
		context->fonts.slots[slot] = (uint16_t)font;
		if (slot == 0)
			context->fonts.slots[1] = (uint16_t)font;
	}
}

static void
fn_screen_error_code(WrenVM *vm)
{
	struct foreign_screen_error *error = (struct foreign_screen_error *)foreign_kind(vm, 0, FOREIGN_SCREEN_ERROR);
	if (error != NULL) wrenSetSlotDouble(vm, 0, error->code);
}

static void
fn_screen_error_message(WrenVM *vm)
{
	struct foreign_screen_error *error = (struct foreign_screen_error *)foreign_kind(vm, 0, FOREIGN_SCREEN_ERROR);
	if (error != NULL) wrenSetSlotString(vm, 0, error->message);
}

static void
fn_screen_error_to_string(WrenVM *vm)
{
	struct foreign_screen_error *error = (struct foreign_screen_error *)foreign_kind(vm, 0, FOREIGN_SCREEN_ERROR);
	char text[224];
	if (error == NULL) return;
	(void)snprintf(text, sizeof(text), "ScreenError(%d): %s", error->code, error->message);
	wrenSetSlotString(vm, 0, text);
}

static void
fn_palette_get(WrenVM *vm)
{
	int64_t index;
	uint32_t colour;
	if (!slot_integer(vm, 1, 0, 255, &index))
		return;
	if (st_wren_screen_status(vm, st_console_get_palette(st_wren_context_from_vm(vm)->console,
	    (uint16_t)index, &colour, 1), "Palette read"))
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
	st_wren_screen_result(vm, st_console_set_palette(st_wren_context_from_vm(vm)->console,
	    (uint16_t)index, &value, 1), "Palette.set");
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
	bool reverse = destination->storage == source->storage
	    && destination_rect.y > source_rect.y
	    && destination_rect.y < source_rect.y + source_rect.height;
	for (uint32_t iteration = 0; iteration < source_rect.height;
	    ++iteration) {
		uint32_t row = reverse ? source_rect.height - iteration - 1
		    : iteration;
		size_t source_index = (size_t)(source_rect.y + row)
		    * source->storage->width + source_rect.x;
		size_t destination_index = (size_t)(destination_rect.y + row)
		    * destination->storage->width + destination_rect.x;
		size_t source_last = source_index + source_rect.width - 1;
		size_t destination_last = destination_index
		    + destination_rect.width - 1;
		struct st_console_cell source_first_cell =
		    source->storage->cells[source_index];
		struct st_console_cell source_last_cell =
		    source->storage->cells[source_last];
		bool clipped_left = (source_first_cell.flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0;
		bool clipped_right = (source_last_cell.flags
		    & ST_CONSOLE_CELL_CONTINUATION) == 0
		    && resolved_cell_columns(vm, &source_last_cell) == 2;
		bool repair_left = destination_rect.x != 0
		    && (destination->storage->cells[destination_index].flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0;
		bool repair_right = destination_rect.x + destination_rect.width
		    < destination->storage->width
		    && (destination->storage->cells[destination_last].flags
		    & ST_CONSOLE_CELL_CONTINUATION) == 0
		    && resolved_cell_columns(vm,
		    &destination->storage->cells[destination_last]) == 2
		    && (destination->storage->cells[destination_last + 1].flags
		    & ST_CONSOLE_CELL_CONTINUATION) != 0;

		int status = surface_copy_pixel_row(destination->storage, destination_rect.x,
			destination_rect.y + row, source->storage, source_rect.x, source_rect.y + row,
			source_rect.width);
		if (status != 0) {
			st_wren_abort(vm, "Surface.putRect could not copy pixel storage");
			return;
		}
		memmove(&destination->storage->cells[
		    destination_index],
		    &source->storage->cells[source_index],
		    (size_t)source_rect.width * sizeof(struct st_console_cell));
		if (repair_left)
			destination->storage->cells[destination_index - 1] =
			    clipped_blank(vm, &destination->storage->cells[
			    destination_index - 1]);
		if (repair_right)
			destination->storage->cells[destination_last + 1] =
			    clipped_blank(vm, &destination->storage->cells[
			    destination_last + 1]);
		if (clipped_left)
			destination->storage->cells[destination_index] =
			    clipped_blank(vm, &source_first_cell);
		if (clipped_right)
			destination->storage->cells[destination_last] =
			    clipped_blank(vm, &source_last_cell);
		surface_drop_empty_pixels(destination->storage, destination_rect.y + row);
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
	if (values[2] <= 0 || values[3] <= 0)
		return;
	right = values[0] + values[2];
	bottom = values[1] + values[3];
	left = values[0] < 0 ? 0 : values[0];
	top = values[1] < 0 ? 0 : values[1];
	if (right <= 0 || bottom <= 0
	    || left >= surface->storage->width
	    || top >= surface->storage->height)
		return;
	if (right > surface->storage->width)
		right = surface->storage->width;
	if (bottom > surface->storage->height)
		bottom = surface->storage->height;
	struct st_console_cell fill = *cell;
	fill.flags &= ~ST_CONSOLE_CELL_PIXEL_GRAPHICS;
	for (int64_t y = top; y < bottom; ++y) {
		for (int64_t x = left; x < right; ++x)
			surface->storage->cells[(size_t)y * surface->storage->width + (size_t)x] = fill;
		surface_drop_empty_pixels(surface->storage, (uint32_t)y);
	}
}

static void
fn_surface_url_at(WrenVM *vm)
{
	struct foreign_surface *surface = surface_value(vm, 0);
	int64_t x, y;
	if (surface == NULL || !slot_integer(vm, 1, INT32_MIN, INT32_MAX, &x)
	    || !slot_integer(vm, 2, INT32_MIN, INT32_MAX, &y)) return;
	char *uri = x < 0 || y < 0 ? NULL : st_wren_detect_url(
	    st_wren_context_from_vm(vm)->console, surface->storage->cells,
	    surface->storage->width, surface->storage->height, (uint32_t)x, (uint32_t)y);
	if (uri == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotString(vm, 0, uri);
	free(uri);
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
	char local[32];
	char *encoded = local;
	size_t length;
	int status;

	if (cell == NULL)
		return;
	status = st_console_font_glyph_text(st_wren_context_from_vm(vm)->console,
	    cell->glyph_font, cell->glyph, local, sizeof(local), &length);
	if (status == IO_ERROR_OUT_OF_RANGE) {
		encoded = malloc(length);
		if (encoded != NULL)
			status = st_console_font_glyph_text(
			    st_wren_context_from_vm(vm)->console, cell->glyph_font,
			    cell->glyph, encoded, length, &length);
		else
			status = IO_ERROR_OUT_OF_MEMORY;
	}
	if (status != 0) {
		encoded = local;
		length = (size_t)st_utf8_encode_one(local, sizeof(local),
		    UINT32_C(0xfffd));
	}
	wrenSetSlotBytes(vm, 0, encoded, length);
	if (encoded != local)
		free(encoded);
}

static void
fn_cell_ch_set(WrenVM *vm)
{
	struct foreign_cell *foreign = foreign_cell_value(vm, 0);
	const char *text;
	st_unicode_text_t *unicode = NULL;
	struct st_unicode_text_item item;
	char *normalized = NULL;
	size_t normalized_size = 0;
	uint8_t columns = 1;
	size_t consumed = 0;
	int length;
	int status;

	if (foreign == NULL || wrenGetSlotType(vm, 1) != WREN_TYPE_STRING) {
		if (foreign != NULL)
			st_wren_abort(vm, "Cell.ch expects a String");
		return;
	}
	text = wrenGetSlotBytes(vm, 1, &length);
	status = length <= 0 ? IO_ERROR_INVALID_ENCODING
	    : st_unicode_text_create(&unicode, ST_UNICODE_TEXT_GRAPHEMES);
	if (status == 0)
		status = st_unicode_text_feed(unicode, text, (size_t)length,
		    &consumed);
	if (status == 0 && consumed != (size_t)length)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0)
		status = st_unicode_text_finish(unicode);
	if (status != 0 || st_unicode_text_peek(unicode, &item) != 0
	    || item.kind != ST_UNICODE_TEXT_ITEM_TEXT) {
		st_unicode_text_destroy(&unicode);
		st_wren_abort(vm, "Cell.ch expects one valid Unicode grapheme");
		return;
	}
	normalized = malloc(item.size);
	if (normalized == NULL) {
		st_unicode_text_destroy(&unicode);
		st_wren_abort(vm, "out of memory");
		return;
	}
	memcpy(normalized, item.bytes, item.size);
	normalized_size = item.size;
	columns = item.columns;
	(void)st_unicode_text_consume(unicode);
	if (st_unicode_text_peek(unicode, &item) == 0) {
		free(normalized);
		st_unicode_text_destroy(&unicode);
		st_wren_abort(vm, "Cell.ch expects one valid Unicode grapheme");
		return;
	}
	st_unicode_text_destroy(&unicode);
	set_cell_cluster(vm, foreign, normalized, normalized_size, columns,
	    columns == 0);
	free(normalized);
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
	struct st_console_cell *cell;
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	int64_t value;
	uint16_t identifier;
	char local[32];
	char *text = local;
	size_t size;
	uint8_t columns;
	int status;

	if (foreign != NULL && slot_integer(vm, 1, 0, UINT8_MAX, &value)) {
		if (!st_wren_font_identifier(context,
		    (uint16_t)value, &identifier)) {
			st_wren_abort(vm, "font is not available");
			return;
		}
		cell = &foreign->storage->cells[foreign->index];
		if (cell->font == identifier)
			return;
		status = st_console_font_glyph_text(context->console,
		    cell->glyph_font, cell->glyph, local, sizeof(local), &size);
		if (status == IO_ERROR_OUT_OF_RANGE) {
			text = malloc(size);
			if (text == NULL) {
				st_wren_abort(vm, "out of memory");
				return;
			}
			status = st_console_font_glyph_text(context->console,
			    cell->glyph_font, cell->glyph, text, size, &size);
		}
		if (status != 0) {
			if (text != local)
				free(text);
			st_wren_abort(vm, "cell glyph has no Unicode text");
			return;
		}
		columns = resolved_cell_columns(vm, cell);
		cell->font = identifier;
		set_cell_cluster(vm, foreign, text, size, columns, false);
		if (text != local)
			free(text);
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

static void
fn_cell_attr(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	uint8_t value;

	if (cell == NULL)
		return;
	if (cell_legacy_attr(vm, cell, &value))
		wrenSetSlotDouble(vm, 0, value);
	else
		st_wren_abort(vm, "console colour state is unavailable");
}

static void
fn_cell_attr_set(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	int64_t value;

	if (cell == NULL || !slot_integer(vm, 1, 0, 255, &value))
		return;
	set_cell_legacy_attr(cell, (uint8_t)value);
}

/* Only independent drawing styles are writable. Serial attributes and
 * glyph/raster/colour-origin state belong to their existing owners. */
#define CELL_STYLE_FLAGS (ST_CONSOLE_CELL_BRIGHT | ST_CONSOLE_CELL_BLINK \
    | ST_CONSOLE_CELL_UNDERLINE | ST_CONSOLE_CELL_BOLD \
    | ST_CONSOLE_CELL_REVERSE | ST_CONSOLE_CELL_CONCEAL)

static void
cell_flag_get(WrenVM *vm, uint32_t flag)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotBool(vm, 0, (cell->flags & flag) != 0);
}

static void
cell_flag_set(WrenVM *vm, uint32_t flag)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	bool value;

	if (cell == NULL || !slot_boolean(vm, 1, &value))
		return;
	if (value)
		cell->flags |= flag;
	else
		cell->flags &= ~flag;
}

#define CELL_STYLE_PROPERTY(name, flag) \
static void fn_cell_##name(WrenVM *vm) { cell_flag_get(vm, flag); } \
static void fn_cell_##name##_set(WrenVM *vm) { cell_flag_set(vm, flag); }
CELL_STYLE_PROPERTY(bright, ST_CONSOLE_CELL_BRIGHT)
CELL_STYLE_PROPERTY(blink, ST_CONSOLE_CELL_BLINK)
CELL_STYLE_PROPERTY(underline, ST_CONSOLE_CELL_UNDERLINE)
CELL_STYLE_PROPERTY(bold, ST_CONSOLE_CELL_BOLD)
CELL_STYLE_PROPERTY(reverse, ST_CONSOLE_CELL_REVERSE)
CELL_STYLE_PROPERTY(conceal, ST_CONSOLE_CELL_CONCEAL)
#undef CELL_STYLE_PROPERTY

#define CELL_METADATA_PROPERTY(name, flag) \
static void fn_cell_##name(WrenVM *vm) { cell_flag_get(vm, flag); }
CELL_METADATA_PROPERTY(teletext, ST_CONSOLE_CELL_SAA5050)
CELL_METADATA_PROPERTY(double_height, ST_CONSOLE_CELL_DOUBLE_HEIGHT)
CELL_METADATA_PROPERTY(reveal, ST_CONSOLE_CELL_REVEAL)
CELL_METADATA_PROPERTY(separated, ST_CONSOLE_CELL_SEPARATED)
#undef CELL_METADATA_PROPERTY

static void
fn_cell_style_flags(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell != NULL)
		wrenSetSlotDouble(vm, 0, cell->flags & CELL_STYLE_FLAGS);
}

static void
fn_cell_style_flags_set(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);
	int64_t value;

	if (cell == NULL || !slot_integer(vm, 1, 0, UINT32_MAX, &value))
		return;
	if (((uint32_t)value & ~CELL_STYLE_FLAGS) != 0) {
		st_wren_abort(vm, "Cell.styleFlags contains unsupported bits");
		return;
	}
	cell->flags = (cell->flags & ~CELL_STYLE_FLAGS) | (uint32_t)value;
}

static void
fn_cell_teletext_control(WrenVM *vm)
{
	struct st_console_cell *cell = cell_value(vm, 0);

	if (cell == NULL)
		return;
	uint32_t control = (cell->flags & ST_CONSOLE_CELL_SAA5050_CONTROL_MASK)
	    >> ST_CONSOLE_CELL_SAA5050_CONTROL_SHIFT;
	if (control == 0)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotDouble(vm, 0, control);
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
	cell->flags &= foreground ? ~ST_CONSOLE_CELL_LEGACY_FOREGROUND
	    : ~ST_CONSOLE_CELL_LEGACY_BACKGROUND;
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
	cell->flags &= foreground ? ~ST_CONSOLE_CELL_LEGACY_FOREGROUND
	    : ~ST_CONSOLE_CELL_LEGACY_BACKGROUND;
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
	struct st_console_cell *right = cell_value_if_present(vm, 1);

	if (left == NULL)
		return;
	if (right == NULL
	    || ((left->flags | right->flags) & ST_CONSOLE_CELL_PIXEL_GRAPHICS)
	    != 0) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	wrenSetSlotBool(vm, 0, left->glyph == right->glyph
		    && left->glyph_font == right->glyph_font
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
	uint8_t attribute;

	if (cell == NULL)
		return;
	if (!cell_legacy_attr(vm, cell, &attribute)) {
		st_wren_abort(vm, "console colour state is unavailable");
		return;
	}
	(void)snprintf(text, sizeof(text),
	    "Cell(glyph=%u glyphFont=%u attr=0x%02X font=%u)",
	    cell->glyph, cell->glyph_font, attribute, cell->font);
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
	uint32_t palette[16];
	uint32_t video_flags;
	int64_t value;
	uint8_t foreground;
	uint8_t background;

	if (!slot_integer(vm, 1, 0, 255, &value)
	    || !console_palette16(vm, palette, &video_flags))
		return;
	foreground = (uint8_t)value & UINT8_C(0x0f);
	if ((video_flags & ST_CONSOLE_VIDEO_NO_BRIGHT) != 0)
		foreground &= UINT8_C(0x07);
	background = (uint8_t)(value >> 4);
	if ((video_flags & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) == 0)
		background &= UINT8_C(0x07);
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, palette[legacy_colour(foreground)]);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, palette[legacy_colour(background)]);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_color_to_attr(WrenVM *vm)
{
	uint32_t palette[16];
	uint32_t video_flags;
	int64_t foreground;
	int64_t background;
	uint32_t best_foreground_distance = UINT32_MAX;
	uint32_t best_background_distance = UINT32_MAX;
	uint8_t best_foreground = 0;
	uint8_t best_background = 0;
	uint8_t foreground_count;
	uint8_t background_count;

	if (!slot_integer(vm, 1, 0, 0xffffff, &foreground)
	    || !slot_integer(vm, 2, 0, 0xffffff, &background)
	    || !console_palette16(vm, palette, &video_flags))
		return;
	foreground_count = (video_flags & ST_CONSOLE_VIDEO_NO_BRIGHT) != 0
	    ? 8 : 16;
	background_count = (video_flags
	    & ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND) != 0 ? 16 : 8;
	for (uint8_t i = 0; i < foreground_count; ++i) {
		uint32_t distance = ycocg_distance((uint32_t)foreground,
		    palette[legacy_colour(i)]);

		if (distance < best_foreground_distance) {
			best_foreground_distance = distance;
			best_foreground = i;
		}
	}
	for (uint8_t i = 0; i < background_count; ++i) {
		uint32_t distance = ycocg_distance((uint32_t)background,
		    palette[legacy_colour(i)]);

		if (distance < best_background_distance) {
			best_background_distance = distance;
			best_background = i;
		}
	}
	wrenSetSlotDouble(vm, 0, best_foreground | (best_background << 4));
}

static void
fn_clipboard_get(WrenVM *vm)
{
	char *text = NULL;

	if (!st_wren_screen_status(vm, st_console_get_clipboard_text(
	    st_wren_context_from_vm(vm)->console, &text), "Clipboard.text")) {
		return;
	}
	wrenSetSlotString(vm, 0, text);
	st_console_free_text(text);
}

static void
fn_clipboard_set(WrenVM *vm)
{
	const char *text;

	if (!st_wren_text_slot(vm, 1, &text, NULL))
		return;
	st_wren_screen_result(vm, st_console_set_clipboard_text(
	    st_wren_context_from_vm(vm)->console, text), "Clipboard.setText");
}

static void
fn_cursor_preset(WrenVM *vm)
{
	int64_t preset;

	if (!slot_integer(vm, 1, 0, 2, &preset))
		return;
	struct st_console_custom_cursor value;
	if (!st_wren_screen_status(vm, st_console_get_default_cursor(st_wren_context_from_vm(vm)->console, &value), "CustomCursor preset"))
		return;
	wrenEnsureSlots(vm, 3);
	wrenGetVariable(vm, "syncterm", "CustomCursor", 2);
	struct foreign_cursor *cursor = wrenSetSlotNewForeign(vm, 0, 2,
	    sizeof(*cursor));
	cursor->header.kind = FOREIGN_CURSOR;
	cursor->cursor = value;
	cursor->pixel_cursor = (struct st_console_pixel_cursor){0};
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
		if (cursor != NULL && slot_integer(vm, 1, 0, UINT32_MAX, &value)) { \
			cursor->cursor.field = (uint32_t)value; \
			cursor->pixel_cursor.enabled = false; \
		} \
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
		bool value; \
		if (cursor != NULL && slot_boolean(vm, 1, &value)) { \
			cursor->cursor.field = value; \
			cursor->pixel_cursor.enabled = false; \
		} \
	}

CURSOR_NUMBER(start, start)
CURSOR_NUMBER(end, end)
CURSOR_NUMBER(range, range)
CURSOR_BOOL(blink, blink)
CURSOR_BOOL(visible, visible)

#undef CURSOR_NUMBER
#undef CURSOR_BOOL

#define CURSOR_STATIC_NUMBER(name, field) \
	static void fn_cursor_##name##_static(WrenVM *vm) \
	{ \
		struct st_console_custom_cursor cursor; \
		if (!st_wren_screen_status(vm, st_console_get_custom_cursor( \
		    st_wren_context_from_vm(vm)->console, &cursor), "CustomCursor read")) { \
			return; \
		} \
		wrenSetSlotDouble(vm, 0, cursor.field); \
	} \
	static void fn_cursor_##name##_set_static(WrenVM *vm) \
	{ \
		struct st_console_custom_cursor cursor; \
		int64_t value; \
		if (!slot_integer(vm, 1, 0, UINT32_MAX, &value)) \
			return; \
		if (!st_wren_screen_status(vm, st_console_get_custom_cursor( \
		    st_wren_context_from_vm(vm)->console, &cursor), "CustomCursor read")) { \
			return; \
		} \
		cursor.field = (uint32_t)value; \
		st_wren_screen_result(vm, st_console_set_custom_cursor( \
		    st_wren_context_from_vm(vm)->console, &cursor), "CustomCursor update"); \
	}

#define CURSOR_STATIC_BOOL(name, field) \
	static void fn_cursor_##name##_static(WrenVM *vm) \
	{ \
		struct st_console_custom_cursor cursor; \
		if (!st_wren_screen_status(vm, st_console_get_custom_cursor( \
		    st_wren_context_from_vm(vm)->console, &cursor), "CustomCursor read")) { \
			return; \
		} \
		wrenSetSlotBool(vm, 0, cursor.field); \
	} \
	static void fn_cursor_##name##_set_static(WrenVM *vm) \
	{ \
		struct st_console_custom_cursor cursor; \
		bool value; \
		if (!slot_boolean(vm, 1, &value)) \
			return; \
		if (!st_wren_screen_status(vm, st_console_get_custom_cursor( \
		    st_wren_context_from_vm(vm)->console, &cursor), "CustomCursor read")) { \
			return; \
		} \
		cursor.field = value; \
		st_wren_screen_result(vm, st_console_set_custom_cursor( \
		    st_wren_context_from_vm(vm)->console, &cursor), "CustomCursor update"); \
	}

CURSOR_STATIC_NUMBER(start, start)
CURSOR_STATIC_NUMBER(end, end)
CURSOR_STATIC_NUMBER(range, range)
CURSOR_STATIC_BOOL(blink, blink)
CURSOR_STATIC_BOOL(visible, visible)

#undef CURSOR_STATIC_NUMBER
#undef CURSOR_STATIC_BOOL

static void
fn_cursor_apply(WrenVM *vm)
{
	struct foreign_cursor *cursor = cursor_value(vm);
	if (cursor != NULL) {
		st_console_t *console = st_wren_context_from_vm(vm)->console;
		int status = st_console_begin_update(console);
		if (status == 0) {
			status = st_console_set_custom_cursor(console, &cursor->cursor);
			if (status == 0 && cursor->pixel_cursor.enabled)
				status = st_console_set_pixel_cursor(console, &cursor->pixel_cursor);
			int ended = st_console_end_update(console);
			if (status == 0) status = ended;
		}
		st_wren_screen_result(vm, status, "CustomCursor.apply");
	}
}

static struct foreign_video_flags *
video_flags_value(WrenVM *vm)
{
	return (struct foreign_video_flags *)foreign_kind(vm, 0,
	    FOREIGN_VIDEO_FLAGS);
}

#define VIDEO_FLAG(name, bit) \
	static void fn_video_##name##_static(WrenVM *vm) \
	{ \
		uint32_t flags; \
		if (!st_wren_screen_status(vm, st_console_get_video_flags(st_wren_context_from_vm(vm)->console, \
		    &flags), "VideoFlags read")) { \
			return; \
		} \
		wrenSetSlotBool(vm, 0, (flags & (bit)) != 0); \
	} \
	static void fn_video_##name##_set_static(WrenVM *vm) \
	{ \
		uint32_t flags; \
		bool value; \
		if (!slot_boolean(vm, 1, &value)) \
			return; \
		if (!st_wren_screen_status(vm, st_console_get_video_flags(st_wren_context_from_vm(vm)->console, \
		    &flags), "VideoFlags read")) { \
			return; \
		} \
		if (value) \
			flags |= (bit); \
		else \
			flags &= ~(bit); \
		st_wren_screen_result(vm, st_console_set_video_flags(st_wren_context_from_vm(vm)->console, \
		    flags), "VideoFlags update"); \
	} \
	static void fn_video_##name(WrenVM *vm) \
	{ \
		struct foreign_video_flags *flags = video_flags_value(vm); \
		if (flags != NULL) \
			wrenSetSlotBool(vm, 0, (flags->flags & (bit)) != 0); \
	} \
	static void fn_video_##name##_set(WrenVM *vm) \
	{ \
		struct foreign_video_flags *flags = video_flags_value(vm); \
		bool value; \
		if (flags == NULL || !slot_boolean(vm, 1, &value)) \
			return; \
		if (value) \
			flags->flags |= (bit); \
		else \
			flags->flags &= ~(bit); \
	}

VIDEO_FLAG(alt_chars, ST_CONSOLE_VIDEO_ALT_CHARS)
VIDEO_FLAG(no_bright, ST_CONSOLE_VIDEO_NO_BRIGHT)
VIDEO_FLAG(bright_background, ST_CONSOLE_VIDEO_BRIGHT_BACKGROUND)
VIDEO_FLAG(blink_alt_chars, ST_CONSOLE_VIDEO_BLINK_ALT_CHARS)
VIDEO_FLAG(no_blink, ST_CONSOLE_VIDEO_NO_BLINK)
VIDEO_FLAG(line_graphics_expand, ST_CONSOLE_VIDEO_LINE_GRAPHICS_EXPAND)

#undef VIDEO_FLAG

static void
fn_video_expand_static(WrenVM *vm)
{
	uint32_t flags;

	if (!st_wren_screen_status(vm, st_console_get_video_flags(st_wren_context_from_vm(vm)->console,
	    &flags), "VideoFlags read")) {
		return;
	}
	wrenSetSlotBool(vm, 0, (flags & ST_CONSOLE_VIDEO_EXPAND) != 0);
}

static void
fn_video_expand(WrenVM *vm)
{
	struct foreign_video_flags *flags = video_flags_value(vm);

	if (flags != NULL)
		wrenSetSlotBool(vm, 0,
		    (flags->flags & ST_CONSOLE_VIDEO_EXPAND) != 0);
}

static void
fn_video_apply(WrenVM *vm)
{
	struct foreign_video_flags *flags = video_flags_value(vm);
	if (flags != NULL)
		st_wren_screen_result(vm, st_console_set_video_flags(st_wren_context_from_vm(vm)->console,
		    flags->flags), "VideoFlags.apply");
}

static void
fn_host_exit_requested(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_host_from_vm(vm)->exit_requested);
}

static void
fn_host_theme_generation(WrenVM *vm)
{
	st_wren_return_u64(vm, st_theme_generation(
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
	/* Application themes always use the system CP437 font. */
	push_optional_number(vm, 3, style->font < 0 ? style->font : 0);
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
	uint32_t value;
	if (st_wren_screen_status(vm, st_console_capabilities(st_wren_context_from_vm(vm)->console, &value), "Host console capabilities"))
		wrenSetSlotBool(vm, 0, (value & ST_CONSOLE_CAP_TEXT_TERMINAL) != 0);
}

static void
fn_host_local_control_keys(WrenVM *vm)
{
	uint32_t value;
	if (st_wren_screen_status(vm, st_console_capabilities(st_wren_context_from_vm(vm)->console, &value), "Host console capabilities"))
		wrenSetSlotBool(vm, 0, (value & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) != 0);
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
fn_host_launch_script(WrenVM *vm)
{
	const char *script = st_wren_host_from_vm(vm)->config.launch_script;

	if (script == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotString(vm, 0, script);
}

static void
fn_host_print(WrenVM *vm)
{
	const char *text;

	if (!st_wren_text_slot(vm, 1, &text, NULL)) {
		st_wren_abort(vm, "Host.print expects a String");
		return;
	}
	puts(text);
	fflush(stdout);
}

static void
fn_host_log_level(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, st_wren_context_from_vm(vm)->transfer_log_level);
}

static void
fn_host_log_level_set(WrenVM *vm)
{
	double value;
	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM
	    || !isfinite(value = wrenGetSlotDouble(vm, 1))) {
		st_wren_abort(vm, "Host.logLevel expects a finite Number");
		return;
	}
	st_wren_context_from_vm(vm)->transfer_log_level = value < 0 ? 0
	    : value > 7 ? 7 : (int)value;
}

static void
fn_host_log_level_names(WrenVM *vm)
{
	static const char *const names[] = {
		"Emergency", "Alert", "Critical", "Error", "Warning", "Notice", "Info", "Debug"
	};
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		wrenSetSlotString(vm, 1, names[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
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
	double value;
	uint64_t bytes;
	char text[64];

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM) {
		st_wren_abort(vm, "Format.bytes expects a Number");
		return;
	}
	value = wrenGetSlotDouble(vm, 1);
	if (!(value >= 0.0))
		value = 0.0;
	if (value >= 0x1p64)
		bytes = UINT64_MAX;
	else
		bytes = (uint64_t)value;
	if (!value_format_byte_estimate(text, sizeof(text), bytes)) {
		st_wren_abort(vm, "Format.bytes value is out of range");
		return;
	}
	wrenSetSlotString(vm, 0, text);
}

static void
fn_format_duration(WrenVM *vm)
{
	double value;
	char text[64];

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM) {
		st_wren_abort(vm, "Format.duration expects a Number");
		return;
	}
	value = wrenGetSlotDouble(vm, 1);
	if (!value_format_duration_estimate(text, sizeof(text), value)) {
		st_wren_abort(vm, "Format.duration value is out of range");
		return;
	}
	wrenSetSlotString(vm, 0, text);
}

static void
fn_platform_name(WrenVM *vm)
{
#if SYNCTERM_PLATFORM_WINDOWS
	wrenSetSlotString(vm, 0, "Windows");
#elif SYNCTERM_PLATFORM_POSIX
	struct utsname information;

	if (uname(&information) == 0)
		wrenSetSlotString(vm, 0, information.sysname);
	else
		wrenSetSlotString(vm, 0, "Unknown");
#else
	wrenSetSlotString(vm, 0, "Unknown");
#endif
}

static void
fn_platform_languages(WrenVM *vm)
{
	struct st_language_preferences languages;

	if (st_language_discover(&languages) != 0) {
		st_wren_abort(vm, "Could not discover UI language preferences");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < languages.count; i++) {
		wrenSetSlotString(vm, 1, languages.tags[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
	st_language_free(&languages);
}

static bool
text_pair(WrenVM *vm, const char **left, int *left_size,
	const char **right, int *right_size)
{
	return st_wren_text_slot(vm, 1, left, left_size)
	    && st_wren_text_slot(vm, 2, right, right_size);
}

static void
fn_text_graphemes(WrenVM *vm)
{
	const char *text;
	int size;
	st_unicode_text_t *graphemes = NULL;
	struct st_unicode_text_item item;
	size_t consumed = 0;
	int status;

	if (!st_wren_text_slot(vm, 1, &text, &size))
		return;
	status = st_unicode_text_create(&graphemes, ST_UNICODE_TEXT_GRAPHEMES);
	if (status == 0)
		status = st_unicode_text_feed(graphemes, text, (size_t)size, &consumed);
	if (status == 0 && consumed != (size_t)size)
		status = IO_ERROR_INVALID_ENCODING;
	if (status == 0)
		status = st_unicode_text_finish(graphemes);
	if (status != 0) {
		st_unicode_text_destroy(&graphemes);
		st_wren_abort(vm, "could not segment text");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	while (st_unicode_text_peek(graphemes, &item) == 0) {
		wrenSetSlotBytes(vm, 1, (const char *)item.bytes, item.size);
		wrenInsertInList(vm, 0, -1, 1);
		(void)st_unicode_text_consume(graphemes);
	}
	st_unicode_text_destroy(&graphemes);
}

static void
fn_text_normalized_byte_count(WrenVM *vm)
{
	const char *text;
	int size;

	if (st_wren_text_slot(vm, 1, &text, &size))
		wrenSetSlotDouble(vm, 0, size);
}

static void
fn_text_caseless_equal(WrenVM *vm)
{
	const char *left;
	const char *right;
	int left_size;
	int right_size;
	bool equal;

	if (!text_pair(vm, &left, &left_size, &right, &right_size))
		return;
	if (st_unicode_caseless_equal(&equal, left, (size_t)left_size, right,
	    (size_t)right_size) != 0) {
		st_wren_abort(vm, "Unicode comparison failed");
		return;
	}
	wrenSetSlotBool(vm, 0, equal);
}

static void
fn_text_caseless_key(WrenVM *vm)
{
	const char *text;
	int text_size;
	char *key;
	size_t key_size;

	if (!st_wren_text_slot(vm, 1, &text, &text_size))
		return;
	if (st_unicode_caseless_key(&key, &key_size, text, (size_t)text_size)
	    != 0) {
		st_wren_abort(vm, "Unicode comparison failed");
		return;
	}
	wrenSetSlotBytes(vm, 0, key, key_size);
	free(key);
}

static void
fn_text_caseless_starts_with(WrenVM *vm)
{
	const char *text;
	const char *prefix;
	int text_size;
	int prefix_size;
	bool matched;

	if (!text_pair(vm, &text, &text_size, &prefix, &prefix_size))
		return;
	if (st_unicode_caseless_starts_with(&matched, text, (size_t)text_size,
	    prefix, (size_t)prefix_size) != 0) {
		st_wren_abort(vm, "Unicode comparison failed");
		return;
	}
	wrenSetSlotBool(vm, 0, matched);
}

static void
fn_text_caseless_contains(WrenVM *vm)
{
	const char *text;
	const char *needle;
	int text_size;
	int needle_size;
	bool matched;

	if (!text_pair(vm, &text, &text_size, &needle, &needle_size))
		return;
	if (st_unicode_caseless_contains(&matched, text, (size_t)text_size,
	    needle, (size_t)needle_size) != 0) {
		st_wren_abort(vm, "Unicode comparison failed");
		return;
	}
	wrenSetSlotBool(vm, 0, matched);
}

static void
fn_text_compare(WrenVM *vm)
{
	const char *left;
	const char *right;
	int left_size;
	int right_size;
	bool case_sensitive;
	int compared;

	if (!text_pair(vm, &left, &left_size, &right, &right_size)
	    || !slot_boolean(vm, 3, &case_sensitive))
		return;
	if (st_unicode_collate(&compared, left, (size_t)left_size, right,
	    (size_t)right_size, case_sensitive
	    ? ST_UNICODE_COLLATION_CASE_SENSITIVE
	    : ST_UNICODE_COLLATION_CASE_INSENSITIVE) != 0) {
		st_wren_abort(vm, "Unicode collation failed");
		return;
	}
	wrenSetSlotDouble(vm, 0, compared);
}

static void
fn_cterm_status_display(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, 0);
}

static void
fn_host_have_ooii(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, true);
}

static void
fn_host_max_ooii_mode(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, ST_OOII_MAX_MODE);
}

static bool
emoji_index(WrenVM *vm, size_t *index)
{
	int64_t value;
	if (!slot_integer(vm, 1, 0, (int64_t)st_emoji_entry_count - 1, &value))
		return false;
	*index = (size_t)value;
	return true;
}

static void
emoji_add_id(WrenVM *vm, size_t id)
{
	wrenSetSlotDouble(vm, 1, (double)id);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_emoji_entries(WrenVM *vm)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_emoji_entry_count; i++)
		if (st_emoji_entries[i].base == i)
			emoji_add_id(vm, i);
}

static bool
emoji_localization(WrenVM *vm)
{
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	struct st_language_preferences preferences;
	int status;

	if (host->emoji_locale != NULL)
		return true;
	status = st_language_discover(&preferences);
	if (status == 0)
		status = st_emoji_locale_create(&host->emoji_locale,
		    (const char *const *)preferences.tags, preferences.count);
	st_language_free(&preferences);
	if (status != 0) {
		st_wren_abort(vm, "Could not load emoji language data");
		return false;
	}
	return true;
}

static void
fn_emoji_search(WrenVM *vm)
{
	const char *query;
	int size;
	struct st_wren_host *host = st_wren_host_from_vm(vm);
	size_t *ids;
	size_t count;
	if (!emoji_localization(vm))
		return;
	if (!st_wren_text_slot(vm, 1, &query, &size))
		return;
	if (st_emoji_search(host->emoji_locale, query, (size_t)size, &ids, &count) != 0) {
		st_wren_abort(vm, "emoji search failed");
		return;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < count; i++)
		emoji_add_id(vm, ids[i]);
	free(ids);
}

static void
fn_emoji_text(WrenVM *vm)
{
	size_t id;
	if (emoji_index(vm, &id))
		wrenSetSlotString(vm, 0, st_emoji_entries[id].text);
}

static void
fn_emoji_name(WrenVM *vm)
{
	size_t id;
	if (emoji_localization(vm) && emoji_index(vm, &id))
		wrenSetSlotString(vm, 0, st_emoji_localized_name(
		    st_wren_host_from_vm(vm)->emoji_locale, id));
}

static void
fn_emoji_keywords(WrenVM *vm)
{
	size_t id;
	if (!emoji_localization(vm) || !emoji_index(vm, &id))
		return;
	const st_emoji_locale_t *locale = st_wren_host_from_vm(vm)->emoji_locale;
	const char *fields[] = {
		st_emoji_localized_keywords(locale, id),
		st_emoji_english_keywords(locale, id)
	};
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		if (fields[i] != NULL) {
			wrenSetSlotString(vm, 1, fields[i]);
			wrenInsertInList(vm, 0, -1, 1);
		}
	}
}

static void
fn_emoji_variants(WrenVM *vm)
{
	size_t id;
	if (!emoji_index(vm, &id))
		return;
	size_t base = st_emoji_entries[id].base;
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_emoji_entry_count; i++)
		if (st_emoji_entries[i].base == base)
			emoji_add_id(vm, i);
}

static void
fn_emoji_recent(WrenVM *vm)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	for (size_t i = 0; i < st_settings_recent_emoji_count(); i++) {
		size_t id = st_emoji_find(st_settings_recent_emoji_at(i));
		if (id < st_emoji_entry_count)
			emoji_add_id(vm, id);
	}
}

static void
fn_emoji_record(WrenVM *vm)
{
	size_t id;
	if (!emoji_index(vm, &id))
		return;
	int status = st_settings_remember_emoji(st_emoji_entries[id].text,
	    !st_wren_host_from_vm(vm)->config.safe_mode);
	if (status == 0)
		wrenSetSlotNull(vm, 0);
	else
		st_wren_push_file_error(vm, status, 0,
		    "Recent emoji were updated for this session, but could not be saved.");
}

static void
fn_emoji_can_send(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	wrenSetSlotBool(vm, 0, context->cterm != NULL
	    && st_cterm_encoding(context->cterm) == ST_CTERM_ENCODING_UTF8);
}

static const struct binding bindings[] = {
	{"ScreenUpdate_", true, "new()", allocate_screen_update},
	{"ScreenError", false, "code", fn_screen_error_code},
	{"ScreenError", false, "message", fn_screen_error_message},
	{"ScreenError", false, "toString", fn_screen_error_to_string},
	{"CustomCursor", true, "new()", allocate_cursor},
	{"VideoFlags", true, "new()", allocate_video_flags},
	{"Text", true, "graphemes(_)", fn_text_graphemes},
	{"Emoji", true, "entries_", fn_emoji_entries},
	{"Emoji", true, "search_(_)", fn_emoji_search},
	{"Emoji", true, "text_(_)", fn_emoji_text},
	{"Emoji", true, "name_(_)", fn_emoji_name},
	{"Emoji", true, "keywords_(_)", fn_emoji_keywords},
	{"Emoji", true, "variants_(_)", fn_emoji_variants},
	{"Emoji", true, "recent_", fn_emoji_recent},
	{"Emoji", true, "recordUse_(_)", fn_emoji_record},
	{"Emoji", true, "canSend", fn_emoji_can_send},
	{"Host", true, "haveOOII", fn_host_have_ooii},
	{"Host", true, "maxOOIIMode", fn_host_max_ooii_mode},
	{"Platform", true, "name", fn_platform_name},
	{"Platform", true, "languages", fn_platform_languages},
	{"Text", true, "normalizedByteCount(_)", fn_text_normalized_byte_count},
	{"Text", true, "caselessKey(_)", fn_text_caseless_key},
	{"Text", true, "caselessEqual(_,_)", fn_text_caseless_equal},
	{"Text", true, "caselessStartsWith(_,_)", fn_text_caseless_starts_with},
	{"Text", true, "caselessContains(_,_)", fn_text_caseless_contains},
	{"Text", true, "compare(_,_,_)", fn_text_compare},
	{"Input", true, "next()", fn_input_next},
	{"Input", true, "next(_)", fn_input_next_timed},
	{"Input", true, "poll()", fn_input_poll},
	{"Input", true, "nextForWidget_(_)", fn_input_next_widget},
	{"Input", true, "nextForWidget_(_,_)", fn_input_next_widget_timed},
	{"Input", true, "mouseEvents", fn_input_mouse_events},
	{"Input", true, "mouseEvents=(_)", fn_input_mouse_events_set},
	{"Input", true, "enableMouseEvent(_)", fn_input_mouse_event_enable},
	{"Input", true, "disableMouseEvent(_)", fn_input_mouse_event_disable},
	{"Input", true, "pushClaim_(_,_)", fn_input_push_claim},
	{"Input", true, "ungetKey_(_)", fn_input_unget_key},
	{"Input", true, "ungetPhysicalKey_(_)", fn_input_unget_physical},
	{"Input", true, "ungetMouse_(_)", fn_input_unget_mouse},
	{"Input", true, "synthesizePhysicalKey(_,_)", fn_input_synthesize_physical},
	{"Input", true, "setMouseVisible(_)", fn_input_mouse_visible_set},
	{"Input", true, "mousedrag()", fn_input_mouse_drag},
	{"Input", true, "mousedrag(_)", fn_input_mouse_drag},
	{"KeyEvent", false, "key", fn_key_key},
	{"KeyEvent", false, "modifiers", fn_key_modifiers},
	{"KeyEvent", false, "matches(_,_)", fn_key_matches},
	{"KeyEvent", false, "byte", fn_key_byte},
	{"KeyEvent", false, "scanCode", fn_key_scan_code},
	{"KeyEvent", false, "enhanced", fn_key_enhanced},
	{"KeyEvent", true, "text(_,_)", fn_key_from_text},
	{"KeyEvent", true, "byte(_)", fn_key_from_byte},
	{"KeyEvent", true, "doorway(_,_)", fn_key_from_doorway},
	{"KeyEvent", false, "codepoint", fn_key_codepoint},
	{"KeyEvent", false, "text", fn_key_text},
	{"KeyEvent", false, "toString", fn_key_to_string},
	{"PhysicalKeyEvent", false, "evdev", fn_physical_evdev},
	{"PhysicalKeyEvent", false, "pressed", fn_physical_pressed},
	{"PhysicalKeyEvent", false, "modifiers", fn_physical_modifiers},
	{"PhysicalKeyEvent", false, "repeat", fn_physical_repeat},
	{"PhysicalKeyEvent", false, "toString", fn_physical_to_string},
	{"MouseEvent", true, "withPixels(_,_,_,_,_,_,_,_,_,_,_)",
	    fn_mouse_with_pixels},
	{"MouseEvent", true,
	    "withPixelsAndWheel(_,_,_,_,_,_,_,_,_,_,_,_,_)",
	    fn_mouse_with_pixels_and_wheel},
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
	{"MouseEvent", false, "wheelX", fn_mouse_wheel_x},
	{"MouseEvent", false, "wheelY", fn_mouse_wheel_y},
	{"MouseEvent", false, "toString", fn_mouse_to_string},
	{"ClaimHandle", false, "pop()", fn_claim_pop},
	{"Wake", true, "post_(_,_)", fn_wake_post},
	{"Screen", true, "size", fn_screen_size},
	{"Screen", true, "save()", fn_screen_save},
	{"Screen", true, "restore(_)", fn_screen_restore},
	{"Screen", true, "readRect(_,_,_,_)", fn_screen_read_rect},
	{"Screen", true, "putRect(_,_,_)", fn_screen_put_rect},
	{"Screen", true, "putRect_(_,_,_,_,_,_,_)", fn_screen_put_rect_partial},
	{"ScreenUpdate_", false, "finish_()", fn_screen_update_finish},
	{"Screen", true, "hyperlinkId", fn_screen_hyperlink},
	{"Screen", true, "setHyperlinkId(_)", fn_screen_hyperlink_set},
	{"Screen", true, "attr", fn_screen_attr},
	{"Screen", true, "setAttr(_)", fn_screen_attr_set},
	{"Screen", true, "textWidth(_,_)", fn_screen_text_width},
	{"ScreenWindow", true, "position", fn_screen_window_position},
	{"ScreenWindow", true, "setPosition(_)", fn_screen_window_position_set},
	{"ScreenWindow", true, "bounds", fn_screen_window_bounds},
	{"ScreenWindow", true, "setBounds(_)", fn_screen_window_bounds_set},
	{"ScreenWindow", true, "clear()", fn_screen_window_clear},
	{"ScreenWindow", true, "clearToLineEnd()", fn_screen_window_clear_line},
	{"ScreenWindow", true, "deleteLine()", fn_screen_window_delete_line},
	{"ScreenWindow", true, "insertLine()", fn_screen_window_insert_line},
	{"ScreenWindow", true, "scroll()", fn_screen_window_scroll},
	{"ScreenWindow", true, "putChar(_)", fn_screen_window_put_char},
	{"ScreenWindow", true, "print(_)", fn_screen_window_print},
	{"ScreenSupports", true, "loadableFonts", fn_support_loadable_fonts},
	{"ScreenSupports", true, "altBlinkFont", fn_support_loadable_fonts},
	{"ScreenSupports", true, "altBoldFont", fn_support_loadable_fonts},
	{"ScreenSupports", true, "brightBackground", fn_support_bright_background},
	{"ScreenSupports", true, "paletteChange", fn_support_palette_change},
	{"ScreenSupports", true, "pixels", fn_support_pixels},
	{"ScreenSupports", true, "customCursor", fn_support_custom_cursor},
	{"ScreenSupports", true, "fontSelection", fn_support_loadable_fonts},
	{"ScreenSupports", true, "windowTitle", fn_support_window_title},
	{"ScreenSupports", true, "clipboard", fn_support_clipboard},
	{"ScreenSupports", true, "openUrl", fn_support_open_url},
	{"ScreenSupports", true, "windowName", fn_support_false},
	{"ScreenSupports", true, "windowIcon", fn_support_false},
	{"ScreenSupports", true, "extendedPalette", fn_support_extended_palette},
	{"ScreenSupports", true, "blockyScaling", fn_support_blocky_scaling},
	{"ScreenSupports", true, "externalScaling", fn_support_external_scaling},
	{"ScreenSupports", true, "closeLock", fn_support_false},
	{"ScreenFonts", true, "[_]", fn_screen_font_get},
	{"ScreenFonts", true, "[_]=(_)", fn_screen_font_set},
	{"Codepage", true, "encodes_(_)", fn_codepage_encodes},
	{"Codepage", true, "encodeByte(_,_)", fn_codepage_encode_byte},
	{"Codepage", true, "brokenVertical(_)", fn_codepage_broken_vertical},
	{"Codepage", true, "decodeByte(_,_)", fn_codepage_decode_byte},
	{"Codepage", true, "decodeByte(_,_,_)", fn_codepage_decode_byte_graphics},
	{"Font", true, "name(_)", fn_font_name},
	{"Font", true, "count", fn_font_count},
	{"Font", true, "available(_)", fn_font_available},
	{"Font", true, "builtinBrokenVertical(_)", fn_font_builtin_broken_vertical},
	{"Font", true, "codepage", fn_font_codepage},
	{"Font", true, "codepageOf(_)", fn_font_codepage_of},
	{"Font", true, "load(_)", fn_font_load},
	{"Font", true, "load(_,_)", fn_font_load_codepage},
	{"Font", true, "codepageFor(_)", fn_font_codepage_for},
	{"FontError", false, "code", fn_font_error_code},
	{"FontError", false, "nativeCode", fn_font_error_native_code},
	{"FontError", false, "message", fn_font_error_message},
	{"FontError", false, "toString", fn_font_error_to_string},
	{"Palette", true, "[_]", fn_palette_get},
	{"Palette", true, "set(_,_)", fn_palette_set},
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
	{"Surface", false, "urlAt(_,_)", fn_surface_url_at},
	{"Cell", false, "ch", fn_cell_ch},
	{"Cell", false, "ch=(_)", fn_cell_ch_set},
	{"Cell", false, "font", fn_cell_font},
	{"Cell", false, "font=(_)", fn_cell_font_set},
	{"Cell", false, "columns", fn_cell_columns},
	{"Cell", false, "legacyAttr", fn_cell_attr},
	{"Cell", false, "legacyAttr=(_)", fn_cell_attr_set},
	{"Cell", false, "bright", fn_cell_bright},
	{"Cell", false, "bright=(_)", fn_cell_bright_set},
	{"Cell", false, "blink", fn_cell_blink},
	{"Cell", false, "blink=(_)", fn_cell_blink_set},
	{"Cell", false, "styleFlags", fn_cell_style_flags},
	{"Cell", false, "styleFlags=(_)", fn_cell_style_flags_set},
	{"Cell", false, "underline", fn_cell_underline},
	{"Cell", false, "underline=(_)", fn_cell_underline_set},
	{"Cell", false, "bold", fn_cell_bold},
	{"Cell", false, "bold=(_)", fn_cell_bold_set},
	{"Cell", false, "reverse", fn_cell_reverse},
	{"Cell", false, "reverse=(_)", fn_cell_reverse_set},
	{"Cell", false, "conceal", fn_cell_conceal},
	{"Cell", false, "conceal=(_)", fn_cell_conceal_set},
	{"Cell", false, "teletext", fn_cell_teletext},
	{"Cell", false, "doubleHeight", fn_cell_double_height},
	{"Cell", false, "reveal", fn_cell_reveal},
	{"Cell", false, "separated", fn_cell_separated},
	{"Cell", false, "teletextControl", fn_cell_teletext_control},
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
	{"Clipboard", true, "setText(_)", fn_clipboard_set},
	{"CustomCursor", true, "presetLegacy_(_)", fn_cursor_preset},
	{"CustomCursor", true, "startLine", fn_cursor_start_static},
	{"CustomCursor", true, "setStartLine(_)", fn_cursor_start_set_static},
	{"CustomCursor", true, "endLine", fn_cursor_end_static},
	{"CustomCursor", true, "setEndLine(_)", fn_cursor_end_set_static},
	{"CustomCursor", true, "range", fn_cursor_range_static},
	{"CustomCursor", true, "setRange(_)", fn_cursor_range_set_static},
	{"CustomCursor", true, "blink", fn_cursor_blink_static},
	{"CustomCursor", true, "setBlink(_)", fn_cursor_blink_set_static},
	{"CustomCursor", true, "visible", fn_cursor_visible_static},
	{"CustomCursor", true, "setVisible(_)", fn_cursor_visible_set_static},
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
	{"VideoFlags", true, "altChars", fn_video_alt_chars_static},
	{"VideoFlags", true, "setAltChars(_)", fn_video_alt_chars_set_static},
	{"VideoFlags", true, "noBright", fn_video_no_bright_static},
	{"VideoFlags", true, "setNoBright(_)", fn_video_no_bright_set_static},
	{"VideoFlags", true, "bgBright", fn_video_bright_background_static},
	{"VideoFlags", true, "setBgBright(_)", fn_video_bright_background_set_static},
	{"VideoFlags", true, "blinkAltChars", fn_video_blink_alt_chars_static},
	{"VideoFlags", true, "setBlinkAltChars(_)", fn_video_blink_alt_chars_set_static},
	{"VideoFlags", true, "noBlink", fn_video_no_blink_static},
	{"VideoFlags", true, "setNoBlink(_)", fn_video_no_blink_set_static},
	{"VideoFlags", true, "expand", fn_video_expand_static},
	{"VideoFlags", true, "lineGraphicsExpand", fn_video_line_graphics_expand_static},
	{"VideoFlags", true, "setLineGraphicsExpand(_)", fn_video_line_graphics_expand_set_static},
	{"VideoFlags", false, "altChars", fn_video_alt_chars},
	{"VideoFlags", false, "altChars=(_)", fn_video_alt_chars_set},
	{"VideoFlags", false, "noBright", fn_video_no_bright},
	{"VideoFlags", false, "noBright=(_)", fn_video_no_bright_set},
	{"VideoFlags", false, "bgBright", fn_video_bright_background},
	{"VideoFlags", false, "bgBright=(_)", fn_video_bright_background_set},
	{"VideoFlags", false, "blinkAltChars", fn_video_blink_alt_chars},
	{"VideoFlags", false, "blinkAltChars=(_)", fn_video_blink_alt_chars_set},
	{"VideoFlags", false, "noBlink", fn_video_no_blink},
	{"VideoFlags", false, "noBlink=(_)", fn_video_no_blink_set},
	{"VideoFlags", false, "expand", fn_video_expand},
	{"VideoFlags", false, "lineGraphicsExpand", fn_video_line_graphics_expand},
	{"VideoFlags", false, "lineGraphicsExpand=(_)", fn_video_line_graphics_expand_set},
	{"VideoFlags", false, "apply()", fn_video_apply},
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
	{"Host", true, "launchScript", fn_host_launch_script},
	{"Host", true, "logLevel", fn_host_log_level},
	{"Host", true, "logLevel=(_)", fn_host_log_level_set},
	{"Host", true, "logLevelNames", fn_host_log_level_names},
	{"Timer", true, "now", fn_timer_now},
	{"WON", true, "deserialize(_)", st_wren_won_deserialize},
	{"WONError", false, "code", st_wren_won_error_code},
	{"WONError", false, "offset", st_wren_won_error_offset},
	{"WONError", false, "message", st_wren_won_error_message},
	{"WONError", false, "toString", st_wren_won_error_toString},
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
	WrenForeignClassMethods methods = {NULL, finalize_foreign};

	if (strcmp(class_name, "Cell") == 0)
		methods.allocate = allocate_cell;
	else if (strcmp(class_name, "Surface") == 0)
		methods.allocate = allocate_surface;
	else if (strcmp(class_name, "ScreenSnapshot") == 0)
		methods.allocate = allocate_snapshot;
	else if (strcmp(class_name, "ScreenUpdate_") == 0) {
		methods.allocate = NULL;
		methods.finalize = finalize_screen_update;
	}
	else if (strcmp(class_name, "KeyEvent") == 0)
		methods.allocate = allocate_key_event;
	else if (strcmp(class_name, "PhysicalKeyEvent") == 0)
		methods.allocate = allocate_physical_key_event;
	else if (strcmp(class_name, "MouseEvent") == 0)
		methods.allocate = allocate_mouse_event;
	else if (strcmp(class_name, "ClaimHandle") == 0) {
		methods.allocate = allocate_claim_handle;
		methods.finalize = finalize_claim_handle;
	}
	else if (strcmp(class_name, "CustomCursor") == 0)
		methods.allocate = NULL;
	else if (strcmp(class_name, "VideoFlags") == 0)
		methods.allocate = NULL;
	else if (strcmp(class_name, "FontError") == 0)
		methods.allocate = allocate_font_error;
	else if (strcmp(class_name, "ScreenError") == 0)
		methods.allocate = NULL;
	else if (strcmp(class_name, "WONError") == 0) {
		methods.allocate = st_wren_won_error_allocate;
		methods.finalize = st_wren_won_error_finalize;
	}
	else
		methods = (WrenForeignClassMethods){NULL, NULL};
	return methods;
}
