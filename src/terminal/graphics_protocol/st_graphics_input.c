/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_key.h"
#include "st_graphics_input.h"

static bool
graphics_key(st_cterm_t *terminal, enum cterm_key_kind kind, uint32_t value, uint32_t modifiers, int local)
{
	if (terminal == NULL || terminal->device.key == NULL)
		return false;
	if (kind == CTERM_KEY_NORMAL)
		local = cterm_mapped_key((uint16_t)value, modifiers);
	const struct cterm_key key = {.kind = kind, .value = value, .modifiers = modifiers, .local = local};
	return terminal->device.key(terminal->device.context, &key);
}

bool
st_graphics_handle_key(st_cterm_t *terminal, uint16_t code, uint32_t modifiers)
{
	return graphics_key(terminal, CTERM_KEY_NORMAL, code, modifiers, code)
	       || st_cterm_handle_key(terminal, code, modifiers);
}

bool
st_graphics_handle_text_key(st_cterm_t *terminal, uint32_t scalar)
{
	return graphics_key(terminal, CTERM_KEY_TEXT, scalar, 0, (int)scalar)
	       || st_cterm_handle_text_key(terminal, scalar);
}

bool
st_graphics_handle_codepage_key(st_cterm_t *terminal, uint8_t byte)
{
	return graphics_key(terminal, CTERM_KEY_CODEPAGE, byte, 0, byte)
	       || st_cterm_handle_codepage_key(terminal, byte);
}

bool
st_graphics_handle_doorway_key(st_cterm_t *terminal, uint8_t scan)
{
	return graphics_key(terminal, CTERM_KEY_DOORWAY, scan, 0, (int)scan << 8)
	       || st_cterm_handle_doorway_key(terminal, scan);
}

bool
st_graphics_handle_mouse(st_cterm_t *terminal, const struct st_console_event *event)
{
	if (terminal == NULL || event == NULL || event->type != ST_CONSOLE_EVENT_MOUSE)
		return false;
	const struct cterm_device_events *device  = &terminal->device;
	uint32_t                          changed = terminal->api_mouse_buttons ^ event->data.mouse.buttons;
	/* A device can open a modal editor which receives the later release. */
	st_cterm_sync_mouse_buttons(terminal, event->data.mouse.buttons);
	if (device->mouse != NULL) {
		bool owns    = device->owns_mouse
		               || (!device->mouse_passthrough && device->mouse_mode != NULL
		                   && device->mouse_mode(device->context) != 0);
		bool handled = device->mouse(device->context, event, changed);
		if (owns || handled)
			return handled;
	}
	return cterm_report_mouse(terminal, event, changed);
}

void
st_graphics_sync_mouse_buttons(st_cterm_t *terminal, uint32_t buttons)
{
	st_cterm_sync_mouse_buttons(terminal, buttons);
	if (terminal != NULL && terminal->device.sync_mouse != NULL)
		terminal->device.sync_mouse(terminal->device.context, buttons);
}

void
st_graphics_handle_controller(st_cterm_t *terminal, const struct st_controller_event *event, bool consumed)
{
	if (!terminal || !event)
		return;
	if (!consumed && event->record == ST_PAD_VALUE && terminal->device.controller)
		consumed |= terminal->device.controller(terminal->device.context, event);
	st_cterm_pad_event(terminal, event, consumed);
}
