/* Connected-session Wren status surface and render callback. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "io_error.h"

#include <stdlib.h>
#include <string.h>

static void
status_handle_get(WrenVM *vm, WrenHandle *handle)
{
	if (handle == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotHandle(vm, 0, handle);
}

static void
status_handle_set(WrenVM *vm, WrenHandle **destination)
{
	WrenHandle *next = NULL;

	if (wrenGetSlotType(vm, 1) != WREN_TYPE_NULL) {
		next = wrenGetSlotHandle(vm, 1);
		if (next == NULL) {
			st_wren_abort(vm, "Status: out of memory");
			return;
		}
	}
	if (*destination != NULL)
		wrenReleaseHandle(vm, *destination);
	*destination = next;
	if (next == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotHandle(vm, 0, next);
}

static void
fn_status_callable(WrenVM *vm)
{
	status_handle_get(vm, st_wren_context_from_vm(vm)->status_callable);
}

static void
fn_status_callable_set(WrenVM *vm)
{
	status_handle_set(vm, &st_wren_context_from_vm(vm)->status_callable);
}

static void
fn_status_details(WrenVM *vm)
{
	status_handle_get(vm, st_wren_context_from_vm(vm)->status_details);
}

static void
fn_status_details_set(WrenVM *vm)
{
	status_handle_set(vm, &st_wren_context_from_vm(vm)->status_details);
}

static void
fn_status_enabled(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_wren_context_from_vm(vm)->status_enabled);
}

static void
fn_status_hover_uri(WrenVM *vm)
{
	char *uri = st_wren_hover_uri(st_wren_context_from_vm(vm));
	if (uri == NULL) wrenSetSlotNull(vm, 0);
	else wrenSetSlotString(vm, 0, uri);
	free(uri);
}

WrenForeignMethodFn
st_wren_status_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	if (!is_static || strcmp(class_name, "Status") != 0)
		return NULL;
	if (strcmp(signature, "callable") == 0)
		return fn_status_callable;
	if (strcmp(signature, "callable=(_)") == 0)
		return fn_status_callable_set;
	if (strcmp(signature, "details") == 0)
		return fn_status_details;
	if (strcmp(signature, "details=(_)") == 0)
		return fn_status_details_set;
	if (strcmp(signature, "hoverUri") == 0)
		return fn_status_hover_uri;
	if (strcmp(signature, "enabled") == 0)
		return fn_status_enabled;
	return NULL;
}

static int
blank_status_cell(struct st_wren_context *context,
	struct st_console_cell *blank)
{
	uint16_t font;
	uint8_t columns;

	if (!st_wren_font_identifier(context, 0, &font))
		return IO_ERROR_NOT_FOUND;
	*blank = (struct st_console_cell){
		.foreground = ST_CONSOLE_COLOR_RGB_VALUE(255, 255, 84),
		.background = ST_CONSOLE_COLOR_RGB_VALUE(0, 0, 168),
		.font = font,
		.glyph_font = font,
	};
	return st_console_font_lookup(context->console, font, ' ',
	    &blank->glyph_font, &blank->glyph, &columns);
}

int
st_wren_status_render(WrenVM *vm, bool enabled)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	struct st_console_cell blank;
	struct st_console_rect row;
	const struct st_console_cell *cells;
	uint32_t width, height;
	int status;

	context->status_enabled = enabled;
	if (!enabled)
		return 0;
	status = st_console_size(context->console, &width, &height);
	if (status != 0 || width == 0 || height == 0)
		return status == 0 ? IO_ERROR_OUT_OF_RANGE : status;
	status = blank_status_cell(context, &blank);
	if (status != 0)
		return status;
	if (context->status_surface == NULL
	    || context->status_surface_width != width) {
		struct st_console_cell *owned = calloc(width, sizeof(*owned));

		if (owned == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		st_wren_push_surface_take(vm, owned, width, 1);
		if (wrenGetSlotType(vm, 0) != WREN_TYPE_FOREIGN)
			return IO_ERROR_OUT_OF_MEMORY;
		WrenHandle *surface = wrenGetSlotHandle(vm, 0);

		if (surface == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (context->status_surface != NULL)
			wrenReleaseHandle(vm, context->status_surface);
		context->status_surface = surface;
		context->status_surface_width = width;
	}
	wrenEnsureSlots(vm, 2);
	wrenSetSlotHandle(vm, 1, context->status_surface);
	if (!st_wren_surface_reset(vm, 1, &blank, width, 1))
		return IO_ERROR_INVALID_FORMAT;
	if (context->status_callable != NULL) {
		if (context->status_call == NULL) {
			context->status_call = wrenMakeCallHandle(vm, "call(_)");
			if (context->status_call == NULL)
				return IO_ERROR_OUT_OF_MEMORY;
		}
		wrenSetSlotHandle(vm, 0, context->status_callable);
		if (wrenCall(vm, context->status_call) != WREN_RESULT_SUCCESS) {
			wrenSetSlotHandle(vm, 1, context->status_surface);
			(void)st_wren_surface_reset(vm, 1, &blank, width, 1);
		}
	}
	wrenSetSlotHandle(vm, 1, context->status_surface);
	cells = st_wren_surface_cells(vm, 1, width, 1);
	if (cells == NULL)
		return IO_ERROR_INVALID_FORMAT;
	row = (struct st_console_rect){0, height - 1, width, 1};
	return st_console_write_cells(context->console, &row, cells, width);
}

void
st_wren_status_dispose(WrenVM *vm)
{
	struct st_wren_context *context = st_wren_context_from_vm(vm);
	WrenHandle **handles[] = {
		&context->status_callable,
		&context->status_details,
		&context->status_surface,
		&context->status_call,
	};

	for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
		if (*handles[i] != NULL) {
			wrenReleaseHandle(vm, *handles[i]);
			*handles[i] = NULL;
		}
	}
}
