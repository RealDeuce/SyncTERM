/* X3 macro acquisition and the outer-loop right-button consumer. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs_input.h"
#include "cterm_igs.h"
#include "cterm_igs_music.h"
#include "cterm.h"
#include "cterm_render.h"
#include "cterm_vt52.h"
#include "st_graphics_parser.h"
#include "st_console_test.h"
#include "io_error.h"
#include "st_cterm.h"
#include "st_utf8.h"
#include "st_unicode_text.h"
#include <stdlib.h>
#include <string.h>

bool cterm_igs_buffer_key(struct cterm_igs *igs, const struct cterm_igs_key *key)
{
	struct cterm_igs_input *m = &igs->input;
	if ((!igs->music->loading && !igs->music->playing && !m->loading && !m->zone_loading && !m->held) || m->key_count == CTERM_IGS_INPUT_KEYS) return false;
	m->keys[(m->key_first + m->key_count) % CTERM_IGS_INPUT_KEYS] = *key;
	++m->key_count;
	return true;
}
int cterm_igs_take_key(struct cterm_igs *igs)
{
	struct cterm_igs_input *m = &igs->input;
	if (m->key_count == 0) return -1;
	int value = m->keys[m->key_first].local;
	m->key_first = (m->key_first + 1) % CTERM_IGS_INPUT_KEYS;
	--m->key_count;
	return value;
}
int cterm_igs_forward_keys(struct cterm_igs *igs)
{
	struct cterm_igs_input *m = &igs->input;
	/* Native local keys not reached by another poll remain normal input.
	 * Replay their original entry point, preserving encoding/modifiers and
	 * suppressing a second console click for the same physical event. */
	while (m->key_count != 0) {
		struct cterm_igs_key key = m->keys[m->key_first];
		(void)cterm_igs_take_key(igs); m->replaying = true;
		switch (key.kind) {
		case CTERM_IGS_KEY_NORMAL: (void)st_cterm_handle_key(igs->terminal, (uint16_t)key.value, key.modifiers); break;
		case CTERM_IGS_KEY_CODEPAGE: (void)st_cterm_handle_codepage_key(igs->terminal, (uint8_t)key.value); break;
		case CTERM_IGS_KEY_TEXT:
			if (!st_cterm_handle_text_key(igs->terminal, key.value)
			    && igs->terminal->wire_encoding == ST_CTERM_ENCODING_UTF8) {
				/* The normal session owner sends unhandled Unicode text.
				 * Replayed events have already left that call frame. */
				char utf8[4], *normalized = NULL; size_t size = 0;
				ptrdiff_t length = st_utf8_encode_one(utf8, sizeof(utf8), key.value);
				int status = length < 0 ? (int)length
				    : st_unicode_text_normalize(&normalized, &size, utf8, (size_t)length, NULL);
				if (status == 0 && igs->terminal->keystroke_cb != NULL)
					igs->terminal->keystroke_cb(normalized, size, igs->terminal->keystroke_cbdata);
				free(normalized);
				if (status != 0) { m->replaying = false; return status; }
			}
			break;
		case CTERM_IGS_KEY_DOORWAY: (void)st_cterm_handle_doorway_key(igs->terminal, (uint8_t)key.value); break;
		}
		m->replaying = false;
		if (igs->reset_pending) return IO_ERROR_INTERRUPTED;
	}
	return 0;
}

static uint64_t deadline(uint64_t now)
{
	return now > UINT64_MAX-5000 ? UINT64_MAX : now+5000;
}
int cterm_igs_input_reset(struct cterm_igs *igs)
{
	struct cterm_igs_zone *zones = igs->input.zones;
	if (zones == NULL) zones = calloc(CTERM_IGS_ZONES, sizeof(*zones));
	if (zones == NULL) return IO_ERROR_OUT_OF_MEMORY;
	memset(zones, 0, CTERM_IGS_ZONES * sizeof(*zones));
	memset(&igs->input, 0, sizeof(igs->input));
	igs->input.zones = zones;
	return 0;
}
int cterm_igs_zone_length(int value)
{
	return value < 1 ? 1 : value > CTERM_IGS_MACRO_BYTES ? CTERM_IGS_MACRO_BYTES : value;
}
int cterm_igs_zone_control(struct cterm_igs *igs, int zone)
{
	if (zone == 9999) {
		for (size_t i = 0; i < CTERM_IGS_ZONES; ++i)
			for (size_t j = 0; j < 4; ++j) igs->input.zones[i].rectangle[j] = -1;
	} else if (zone == 9998) igs->input.zone_loopback = 1;
	else if (zone == 9997) igs->input.zone_loopback = 0;
	else return IO_ERROR_PROTOCOL;
	return 0;
}
static int auxiliary_begin(struct cterm_igs *igs, uint64_t *due)
{
	int status = igs->loop_mode == 1 ? cterm_igs_flow(igs, true) : 0;
	/* The first auxiliary read starts after any flow output/readiness. */
	uint64_t now;
	if (status == 0) status = igs->now_ms(&now);
	if (status == 0) *due = deadline(now);
	return status;
}
int cterm_igs_zone_load(struct cterm_igs *igs, int length)
{
	struct cterm_igs_input *input = &igs->input;
	if (input->zone < 0 || input->zone >= CTERM_IGS_ZONES) return IO_ERROR_OUT_OF_RANGE;
	input->zones[input->zone].length = (uint8_t)cterm_igs_zone_length(length);
	input->zone_loading = true; input->zone_received = 0;
	return auxiliary_begin(igs, &input->zone_deadline);
}
int cterm_igs_macro_length(int value)
{
	unsigned byte = (unsigned)value & 255u;
	int length = byte < 128 ? (int)byte : (int)byte-256;
	return length < 1 ? 1 : length > CTERM_IGS_MACRO_BYTES ? CTERM_IGS_MACRO_BYTES : length;
}
int cterm_igs_macro_load(struct cterm_igs *igs, int length)
{
	struct cterm_igs_input *input = &igs->input;
	input->length = (uint8_t)cterm_igs_macro_length(length);
	input->loading = true; input->received = 0;
	return auxiliary_begin(igs, &input->deadline);
}
/* X3 and X4 use the same fixed auxiliary service: retain n bytes and
 * discard one unconditional read, with a separate deadline for each slot. */
static int auxiliary_bytes(struct cterm_igs *igs, uint8_t *destination, size_t length,
    size_t *received, uint64_t *due, const uint8_t *bytes, size_t size, size_t *accepted)
{
	*accepted = 0;
	if (*received > length || size > length+1-*received) return IO_ERROR_PROTOCOL;
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status != 0) return status;
	bool expired = now >= *due;
	if (expired && size != 0) {
		const uint8_t bell = 7;
		status = cterm_vt52_output(igs->terminal, &bell, 1);
		if (status != 0 || igs->reset_pending) return status != 0 ? status : IO_ERROR_INTERRUPTED;
	}
	while (*accepted < size) {
		if (*received < length) destination[*received] = expired && *accepted == 0 ? ' ' : bytes[*accepted];
		++*received; ++*accepted;
	}
	*due = deadline(now);
	return 0;
}
int cterm_igs_macro_bytes(struct cterm_igs *igs, const uint8_t *bytes, size_t size, size_t *accepted)
{
	struct cterm_igs_input *input = &igs->input;
	if (!input->loading || input->length > sizeof(input->bytes)) { *accepted = 0; return IO_ERROR_PROTOCOL; }
	return auxiliary_bytes(igs, input->bytes, input->length, &input->received, &input->deadline, bytes, size, accepted);
}
int cterm_igs_zone_bytes(struct cterm_igs *igs, const uint8_t *bytes, size_t size, size_t *accepted)
{
	struct cterm_igs_input *input = &igs->input;
	if (!input->zone_loading || input->zone < 0 || input->zone >= CTERM_IGS_ZONES) {
		*accepted = 0; return IO_ERROR_PROTOCOL;
	}
	struct cterm_igs_zone *zone = &input->zones[input->zone];
	if (zone->length > sizeof(zone->bytes)) { *accepted = 0; return IO_ERROR_PROTOCOL; }
	return auxiliary_bytes(igs, zone->bytes, zone->length, &input->zone_received, &input->zone_deadline, bytes, size, accepted);
}
void cterm_igs_input_cancel_reader(struct cterm_igs *igs)
{
	struct cterm_igs_input *input = &igs->input;
	input->configuring = input->loading = false; input->deadline = 0;
	input->zone_configuring = input->zone_loading = false; input->zone_deadline = 0;
}
void cterm_igs_input_cancel(struct cterm_igs *igs)
{
	struct cterm_igs_input *input = &igs->input;
	cterm_igs_input_cancel_reader(igs);
	input->held = input->suspended = input->sending = input->suppressed = false;
	input->buttons = 0;
}
int cterm_igs_input_activate(struct cterm_igs *igs)
{
	struct cterm_igs_input *input = &igs->input;
	if (input->buttons != 2 || input->enabled != 1 || input->suppressed || cterm_igs_paused(igs)) return 0;
	bool feeding = igs->feeding;
	igs->feeding = true; input->sending = input->held = true;
	int status = 0;
	for (size_t i = 0; i < (input->length < 128 ? input->length : 0); ++i) {
		if (i >= sizeof(input->bytes)) { status = IO_ERROR_OUT_OF_RANGE; break; }
		char byte = (char)input->bytes[i];
		if (igs->terminal->keystroke_cb != NULL)
			igs->terminal->keystroke_cb(&byte, 1, igs->terminal->keystroke_cbdata);
		if (igs->reset_pending) { status = IO_ERROR_INTERRUPTED; break; }
	}
	if (status == 0 && input->cr != 0 && igs->terminal->keystroke_cb != NULL) {
		igs->terminal->keystroke_cb("\r", 1, igs->terminal->keystroke_cbdata);
		if (igs->reset_pending) status = IO_ERROR_INTERRUPTED;
	}
	/* Transport owns readiness. Do not invent a UART poll or delay. */
	input->sending = false; input->held = input->buttons != 0;
	igs->feeding = feeding;
	if (!feeding && igs->reset_pending) {
		cterm_igs_reset(igs);
		if (status == IO_ERROR_INTERRUPTED) status = 0;
	}
	return status;
}
static unsigned tos_buttons(uint32_t buttons)
{
	return ((buttons & ST_CONSOLE_MOUSE_BUTTON_LEFT) != 0 ? 1u : 0u)
	    | ((buttons & ST_CONSOLE_MOUSE_BUTTON_RIGHT) != 0 ? 2u : 0u);
}
static void sample(struct cterm_igs *igs, uint32_t buttons)
{
	bool releasing = igs->input.held || igs->input.suspended;
	igs->input.buttons = tos_buttons(buttons);
	if (igs->input.buttons == 0) {
		igs->input.held = igs->input.suppressed = false;
		/* Release may arrive inside a send callback. Defer parser/key work
		 * until that callback and its surrounding parser have returned. */
		if (releasing && !igs->feeding) cterm_render_record_error(igs->terminal, cterm_igs_poll(igs));
	}
}
bool cterm_igs_mouse(struct cterm_igs *igs, const struct st_console_event *event)
{
	if (event->data.mouse.action == ST_CONSOLE_MOUSE_WHEEL) return false;
	bool was_held = igs->input.held;
	bool inside = event->data.mouse.pixel_x >= 0 && event->data.mouse.pixel_y >= 0
	    ? (uint32_t)event->data.mouse.pixel_x < igs->width
	      && (uint32_t)event->data.mouse.pixel_y < (uint32_t)igs->terminal->height * (igs->resolution == 2 ? 16u : 8u)
	    : event->data.mouse.x >= 0 && event->data.mouse.y >= 0
	      && event->data.mouse.x < igs->terminal->width && event->data.mouse.y < igs->terminal->height;
	if (!inside && !was_held) { cterm_igs_mouse_sync(igs, event->data.mouse.buttons); return false; }
	sample(igs, event->data.mouse.buttons);
	bool claimed = was_held || (igs->input.enabled == 1 && igs->input.buttons == 2 && !igs->input.suppressed);
	if (!igs->feeding && !st_graphics_parser_in_command(igs->parser))
		cterm_render_record_error(igs->terminal, cterm_igs_input_activate(igs));
	return claimed;
}
void cterm_igs_mouse_sync(struct cterm_igs *igs, uint32_t buttons)
{
	/* A press claimed by local UI/disabled forwarding must not fire later
	 * merely because a command completed while that button remained held. */
	if (!igs->input.held && tos_buttons(buttons) != 0) igs->input.suppressed = true;
	sample(igs, buttons);
}
#if ST_CONSOLE_TEST
void cterm_igs_input_inspect(const struct cterm_igs *igs, struct st_console_test_application *app)
{
	const struct cterm_igs_input *input = &igs->input;
	uint32_t hash = UINT32_C(2166136261);
	for (size_t i = 0; i < sizeof(input->bytes); ++i) hash = (hash ^ input->bytes[i]) * UINT32_C(16777619);
#define FIELD(name) st_console_test_number(app, "cterm.igs_macro_" #name, input->name)
	FIELD(enabled); FIELD(length); FIELD(cr); FIELD(loading); FIELD(received); FIELD(held); FIELD(buttons);
#undef FIELD
	st_console_test_number(app, "cterm.igs_macro_buffer", hash);
	uint32_t rectangles = UINT32_C(2166136261), payloads = rectangles;
	for (size_t i = 0; i < CTERM_IGS_ZONES; ++i) {
		const struct cterm_igs_zone *zone = &input->zones[i];
		for (size_t j = 0; j < 4; ++j) {
			unsigned word = (uint16_t)zone->rectangle[j];
			rectangles = (rectangles ^ (word >> 8)) * UINT32_C(16777619);
			rectangles = (rectangles ^ (word & 255)) * UINT32_C(16777619);
		}
		payloads = (payloads ^ zone->length) * UINT32_C(16777619);
		for (size_t j = 0; j < sizeof(zone->bytes); ++j) payloads = (payloads ^ zone->bytes[j]) * UINT32_C(16777619);
	}
	st_console_test_number(app, "cterm.igs_zone_rectangles", rectangles);
	st_console_test_number(app, "cterm.igs_zone_payloads", payloads);
	st_console_test_number(app, "cterm.igs_zone_loopback", input->zone_loopback);
	st_console_test_number(app, "cterm.igs_zone_loading", input->zone_loading);
	st_console_test_number(app, "cterm.igs_zone_received", (int64_t)input->zone_received);
}
#endif
