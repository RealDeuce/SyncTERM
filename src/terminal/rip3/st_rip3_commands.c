/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_input.h"
#include "st_rip3_mouse.h"
#include "st_rip3_number.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_refresh.h"
#include "st_rip3_styles.h"
#include "st_rip3_text_window.h"

static int
st_rip3_command_field(uint64_t *result, const uint8_t *bytes, size_t length, unsigned radix)
{
	if (length == 0 || length > 5)
		return IO_ERROR_INVALID_FORMAT;
	/* RIPtel's MegaNum wire collector admits only digits and uppercase letters. */
	if (radix == 36) {
		for (size_t i = 0; i < length; i++)
			if (bytes[i] >= 'a' && bytes[i] <= 'z')
				return IO_ERROR_INVALID_FORMAT;
	}
	return st_rip3_number_decode(result, bytes, length, radix);
}

int
st_rip3_command_next_field(struct st_rip3_fields *fields, uint64_t *result, size_t width, unsigned radix,
    bool final)
{
	if (fields->remaining == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (fields->bytes[0] == '.') {
		if (result != NULL)
			*result = 0;
		width = 1;
	}
	else {
		if (fields->remaining < width) {
			if (!final)
				return IO_ERROR_INVALID_FORMAT;
			width = fields->remaining;
		}
		if (result != NULL) {
			int status = st_rip3_command_field(result, fields->bytes, width, radix);
			if (status != 0)
				return status;
		}
		else {
			/* An ignored field can be too wide for a single integer. */
			for (size_t i = 0; i < width; i++) {
				uint64_t digit;
				int      status = st_rip3_command_field(&digit, fields->bytes + i, 1, radix);
				if (status != 0)
					return status;
			}
		}
	}
	fields->bytes += width;
	fields->remaining -= width;
	return 0;
}

int
st_rip3_command_environment(const struct st_rip3_command_host *host, unsigned *number,
    struct st_rip3_environment *value)
{
	if (host->environments == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	return st_rip3_environments_active(host->environments, number, value);
}

int
st_rip3_command_frame(const struct st_rip3_command_host *host, unsigned environment, struct st_rip3_frame *result)
{
	struct st_rip3_frame     frame;
	struct st_rip3_port_info screen;
	int                      status = st_rip3_environments_frame(host->environments, environment, &frame);

	if (status == 0)
		status = st_rip3_ports_info(host->ports, 0, &screen);
	if (status != 0)
		return status;
	if (screen.bounds.x0 != 0 || screen.bounds.y0 != 0 || (uint32_t)screen.bounds.x1 + 1 != frame.device_width
	    || (uint32_t)screen.bounds.y1 + 1 != frame.device_height)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = frame;
	return 0;
}

static int
desktop_palette(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	unsigned                   number, colors[16];
	uint64_t                   value;
	size_t                     start = 0, entries = 16;
	struct st_rip3_fields      fields = {command + 1, length - 1};
	int                        status;

	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	if (command[0] == 'a') {
		status = st_rip3_command_next_field(&fields, &value, 2, environment.base_math, false);
		if (status != 0)
			return status;
		if (value > 255)
			return IO_ERROR_OUT_OF_RANGE;
		start   = (size_t)value;
		entries = 1;
	}
	for (size_t i = 0; i < entries; i++) {
		status = st_rip3_command_next_field(&fields, &value, 2, environment.base_math, i + 1 == entries);
		if (status != 0)
			return status;
		colors[i] = (unsigned)value;
	}
	/* Collect every wire field before the palette processes decoded values. */
	status     = st_rip3_palettes_desktop(host->palettes, start, colors, entries);
	*operation = status != IO_ERROR_OUT_OF_RANGE;
	return status;
}

static uint32_t
drawing_palette_channel(uint64_t component)
{
	uint32_t stored = ((uint32_t)component & 255) >> 2;
	return stored == 63 ? 255 : stored * 4;
}

static int
drawing_palette(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation)
{
	bool                  block   = command[0] == 'D';
	uint64_t              entries = 1, start, bits;
	struct st_rip3_fields fields  = {command + 1, length - 1};
	uint32_t              colors[256];
	int                   status = 0;

	/* Unlike desktop palette commands, both descriptors force UltraNums
	 * for header fields as well as the packed colors. */
	if (block)
		status = st_rip3_command_next_field(&fields, &entries, 2, 64, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &start, 2, 64, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &bits, 1, 64, false);
	if (status != 0)
		return status;
	if (start > 255 || entries > 256 - start || bits != 8)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < entries; i++) {
		uint64_t color;
		status = st_rip3_command_next_field(&fields, &color, 4, 64, i + 1 == entries);
		if (status != 0)
			return status;
		/* This published representation preserves the stored six bits
		 * under readback, copies, saves and palette reselection. */
		colors[i] = drawing_palette_channel(color >> 16) << 16 | drawing_palette_channel(color >> 8) << 8
		            | drawing_palette_channel(color);
	}
	/* The variable D collector counts the whole tail. Fixed d has
	 * already finished its last field and can ignore surplus. */
	if (block && fields.remaining != 0)
		return IO_ERROR_INVALID_FORMAT;
	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	return st_rip3_palettes_rgb(host->palettes, (size_t)start, 8, colors, (size_t)entries);
}

static int
switch_fields(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, uint64_t *target,
    uint64_t *flags)
{
	struct st_rip3_fields      fields = {command + 2, length - 2};
	struct st_rip3_environment environment;
	unsigned                   current;
	int                        status = st_rip3_command_environment(host, &current, &environment);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, target, 1, environment.base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, flags, 2, environment.base_math, true);
	return status;
}

static int
switch_palette(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation,
    const struct st_rip3_command_sink *sink)
{
	uint64_t target, flags;
	int      status = switch_fields(host, command, length, &target, &flags);
	if (status != 0)
		return status;
	if (target >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	/* Protect then unprotect on each side of selection. In the driver a
	 * failed protection request is diagnostic and leaves the sequence live. */
	if (flags & 4)
		st_rip3_command_result(sink, st_rip3_palettes_protect_current(host->palettes, true), true);
	if (flags & 8)
		st_rip3_command_result(sink, st_rip3_palettes_protect_current(host->palettes, false), true);
	st_rip3_command_result(sink, st_rip3_palettes_switch(host->palettes, (unsigned)target), true);
	if (flags & 1)
		st_rip3_command_result(sink, st_rip3_palettes_protect_current(host->palettes, true), true);
	if (flags & 2)
		st_rip3_command_result(sink, st_rip3_palettes_protect_current(host->palettes, false), true);
	return 0;
}

static int
host_query(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_fields      fields = {command + 2, length - 2};
	struct st_rip3_environment environment;
	unsigned                   number;
	uint64_t                   mode, window;

	int status = st_rip3_command_environment(host, &number, &environment);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 1, environment.base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &window, 1, environment.base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, NULL, 2, environment.base_math, false);
	if (status != 0)
		return status;
	/* The hover owner interprets its stored source as a C-string prefix.
	 * Other query sources retain the command-level embedded-NUL rejection. */
	if (mode != 5 && mode != 6 && memchr(fields.bytes, 0, fields.remaining) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	if (mode > 6)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (mode == 1 || mode == 3) {
		if (host->ports == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		return st_rip3_ports_define_query(host->ports, mode == 1, (unsigned)window, fields.bytes,
		    fields.remaining, host->codepage_converted);
	}
	if (mode == 2 || mode == 4) {
		if (host->text_window == NULL || host->text_window->resident_query == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		return host->text_window->resident_query(host->text_window->context, mode == 2, (unsigned)window,
		    fields.bytes, fields.remaining, host->codepage_converted);
	}
	if (mode == 5 || mode == 6) {
		if (host->mouse == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		return st_rip3_mouse_query_encoded(host->mouse, mode == 5, fields.bytes, fields.remaining,
		    host->codepage_converted);
	}
	/* Private ownership/preflight status is consumed by the void command
	 * boundary. Rejected source syntax has not started an operation. Nested
	 * execution diagnostics remain inside the evaluator's void processing. */
	status = st_rip3_query_start(host->query, fields.bytes, fields.remaining);
	if (status == IO_ERROR_INVALID_ARGUMENT || status == IO_ERROR_INVALID_FORMAT
	    || status == IO_ERROR_OUT_OF_RANGE)
		*operation = false;
	return status;
}

static int
style_protection(struct st_rip3_styles *styles, unsigned number, uint64_t flags, unsigned protect,
    unsigned unprotect, const struct st_rip3_command_sink *sink)
{
	int status = 0;
	if ((flags & protect) != 0) {
		/* The original setter diagnoses attempts to protect zero, but its
		 * caller still selects/applies every remaining flag afterward. */
		if (number == 0) {
			/* Source and destination requests are distinct observations, even
			 * for 0 -> 0. Diagnose here so neither is collapsed into the final
			 * command status, and continue through the remaining ordered steps. */
			if (sink != NULL && sink->diagnostic != NULL)
				sink->diagnostic(sink->context, ST_RIP3_COMMAND_OPERATION_FAILED);
		}
		else
			status = st_rip3_styles_protect(styles, number, true);
	}
	if ((flags & unprotect) != 0 && number != 0) {
		int cleared = st_rip3_styles_protect(styles, number, false);
		if (status == 0)
			status = cleared;
	}
	return status;
}

static int
switch_style(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation,
    const struct st_rip3_command_sink *sink)
{
	uint64_t number, flags;
	unsigned current;
	int      source, destination;
	int      status = switch_fields(host, command, length, &number, &flags);
	if (status != 0)
		return status;
	if (number > 35)
		return 0; /* The shipped handler ignores invalid slots. */
	if (host->styles == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_styles_current(host->styles, &current);
	if (status != 0)
		return status;
	*operation = true;
	source     = style_protection(host->styles, current, flags, 4, 8, sink);
	status     = st_rip3_styles_switch(host->styles, (unsigned)number);
	if (status != 0)
		return status;
	destination = style_protection(host->styles, (unsigned)number, flags, 1, 2, sink);
	return source != 0 ? source : destination;
}

static int
switch_environment(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation,
    const struct st_rip3_command_sink *sink)
{
	uint64_t target, flags;
	int      status = switch_fields(host, command, length, &target, &flags);
	if (status != 0)
		return status;
	if (target >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	unsigned current;
	status = st_rip3_environments_current(host->environments, &current);
	if (status != 0)
		return status;
	*operation = true;
	if (flags & 4)
		st_rip3_command_result(sink, st_rip3_environments_protect(host->environments, current, true),
		    true);
	if ((flags & 8) && current != 0)
		st_rip3_command_result(sink, st_rip3_environments_protect(host->environments, current, false),
		    true);
	status = st_rip3_environments_activate(host->environments, (unsigned)target);
	if (status != 0)
		return status;
	if (flags & 1)
		st_rip3_command_result(sink,
		    st_rip3_environments_protect(host->environments, (unsigned)target, true), true);
	if ((flags & 2) && target != 0)
		st_rip3_command_result(sink,
		    st_rip3_environments_protect(host->environments, (unsigned)target, false), true);
	return 0;
}

static int
switch_text_window(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation)
{
	uint64_t target, flags;
	int      status = switch_fields(host, command, length, &target, &flags);
	if (status != 0)
		return status;
	if (target >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->text_window == NULL || host->text_window->select == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	return host->text_window->select(host->text_window->context, (unsigned)target, (unsigned)flags);
}

static int
normal_text_window(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation,
    const struct st_rip3_command_sink *sink)
{
	struct st_rip3_environment environment;
	unsigned                   number;
	int                        status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	struct st_rip3_fields fields = {command + 1, length - 1};
	bool                  zero   = true;
	uint64_t              values[6];
	for (unsigned i = 0; i < 6; i++) {
		/* Ordinary window bounds use fixed-width text cells. */
		size_t width = i < 4 ? 2 : 1;
		status = st_rip3_command_next_field(&fields, values + i, width, environment.base_math, false);
		if (status != 0)
			return status;
		if (values[i] != 0)
			zero = false;
	}
	if (zero) {
		if (host->text_window != NULL && host->text_window->deactivate != NULL) {
			*operation = true;
			return host->text_window->deactivate(host->text_window->context);
		}
		if (host->text_active == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		*operation         = true;
		*host->text_active = false;
		return 0;
	}
	if (host->text_window == NULL || host->text_window->define == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_frame              frame;
	struct st_rip3_text_window_layout layout;
	status = st_rip3_environments_device(host->environments, &frame);
	if (status == 0)
		status = st_rip3_text_window_normal(&layout, &frame, (uint32_t)values[0], (uint32_t)values[1],
		    (uint32_t)values[2], (uint32_t)values[3], (unsigned)values[4], (unsigned)values[5]);
	if (status != 0)
		return status;
	uint64_t near_x = values[0] < values[2] ? values[0] : values[2],
		 far_x  = values[0] > values[2] ? values[0] : values[2];
	uint64_t near_y = values[1] < values[3] ? values[1] : values[3],
		 far_y  = values[1] > values[3] ? values[1] : values[3];
	if ((near_x != layout.x0 || far_x != layout.x1 || near_y != layout.y0 || far_y != layout.y1)
	    && sink != NULL && sink->diagnostic != NULL)
		sink->diagnostic(sink->context, ST_RIP3_COMMAND_COORDINATES_CLAMPED);
	*operation = true;
	return host->text_window->define(host->text_window->context, &layout);
}

static int
extended_text_window(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation)
{
	struct st_rip3_environment environment;
	unsigned                   number;
	int                        status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	struct st_rip3_fields wire = {command + 1, length - 1};
	uint64_t              fields[8];
	for (unsigned i = 0; i < 7; i++) {
		size_t width = i < 4 ? environment.coordinate_size : i < 6 ? 2 : 1;
		status       = st_rip3_command_next_field(&wire, fields + i, width, environment.base_math, false);
		if (status != 0)
			return status;
	}
	/* Flags can end at the delimiter when no reserved field follows. */
	status = st_rip3_command_next_field(&wire, fields + 7, 4, environment.base_math, true);
	if (status == 0 && wire.remaining != 0)
		status = st_rip3_command_next_field(&wire, NULL, 3, environment.base_math, true);
	if (status != 0)
		return status;
	if (fields[4] == 0 || fields[5] == 0 || fields[7] > 63 || (!(fields[7] & 8) && fields[6] > 4))
		return IO_ERROR_OUT_OF_RANGE;
	if (host->text_window == NULL || host->text_window->define == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_frame frame;
	status = st_rip3_environments_frame(host->environments, number, &frame);
	struct st_rip3_text_window_layout layout;
	if (status == 0)
		status = st_rip3_text_window_extended(&layout, &frame, (uint32_t)fields[0], (uint32_t)fields[1],
		    (uint32_t)fields[2], (uint32_t)fields[3], (uint32_t)fields[4], (uint32_t)fields[5],
		    (unsigned)fields[6], (unsigned)fields[7]);
	if (status != 0)
		return status;
	*operation = true;
	return host->text_window->define(host->text_window->context, &layout);
}

static int
text_cursor(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation)
{
	uint64_t column = 0, row = 0;
	if (command[0] == 'g') {
		struct st_rip3_environment environment;
		unsigned                   number;
		struct st_rip3_fields      fields = {command + 1, length - 1};
		int                        status = st_rip3_command_environment(host, &number, &environment);
		if (status == 0)
			status = st_rip3_command_next_field(&fields, &column, environment.coordinate_size,
			    environment.base_math, false);
		if (status == 0)
			status = st_rip3_command_next_field(&fields, &row, environment.coordinate_size,
			    environment.base_math, true);
		if (status != 0)
			return status;
	}
	if (host->text_window == NULL || host->text_window->position == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	return host->text_window->position(host->text_window->context, (uint32_t)column, (uint32_t)row);
}

static int
color_mode(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment value;
	unsigned                   number;
	uint64_t                   mode, bits;
	struct st_rip3_fields      fields = {command + 1, length - 1};
	int                        status = st_rip3_command_environment(host, &number, &value);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 1, value.base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &bits, 1, value.base_math, true);
	if (status != 0)
		return status;
	if (mode > 1)
		return IO_ERROR_OUT_OF_RANGE;
	/* The wire setter's RGB gate differs from the COLORMODE variable:
	 * unsupported precision leaves the current mode unchanged. Palette
	 * mode ignores the precision field after its numeric validation. */
	if (mode == 1 && bits != 8)
		return 0;
	value.rgb_bits = mode == 0 ? 0 : 8;
	*operation     = true;
	return st_rip3_environments_set(host->environments, number, &value);
}

struct drawing_command {
	const struct st_rip3_command_host *host;
	const uint8_t                     *bytes;
	size_t                             length;
	bool                              *operation;
	int (*execute)(const struct st_rip3_command_host *, const uint8_t *, size_t, bool *);
};

static int
draw_command(void *context)
{
	const struct drawing_command *call = context;
	return call->execute(call->host, call->bytes, call->length, call->operation);
}

static int
execute_drawing(const struct st_rip3_command_host *host, const uint8_t *bytes, size_t length, bool *operation,
    int (*handler)(const struct st_rip3_command_host *, const uint8_t *, size_t, bool *))
{
	struct st_rip3_style style;
	unsigned             current;
	if (st_rip3_styles_current(host->styles, &current) != 0
	    || st_rip3_styles_get(host->styles, current, &style) != 0)
		return handler(host, bytes, length, operation);
	struct drawing_command call = {host, bytes, length, operation, handler};
	return st_rip3_ports_draw(host->ports, style.write_mode, draw_command, &call);
}

static int
execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length, bool *operation,
    const struct st_rip3_command_sink *sink)
{
	struct st_rip3_environment value;
	unsigned                   number;
	uint64_t                   first, second;
	bool                       delay;
	int                        status;

	*operation = false;
	if (host == NULL || command == NULL || length == 0 || length == SIZE_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length >= 2 && command[0] == '1' && command[1] == 0x1b)
		return host_query(host, command, length, operation);
	if (memchr(command, 0, length) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	if (length >= 2 && command[0] == '3' && command[1] == 'e') {
		struct st_rip3_fields fields = {command + 2, length - 2};
		status                       = st_rip3_command_environment(host, &number, &value);
		if (status == 0)
			status = st_rip3_command_next_field(&fields, &first, 2, value.base_math, true);
		if (status != 0)
			return status;
		return st_rip3_environments_set_playback_rate(host->environments, (uint32_t)first);
	}
	delay = length >= 2 && command[0] == '3' && command[1] == 'D';
	/* Comments and the inert F descriptor bypass every expansion/service.
	 * Grouping commands carry no terminal state, even when unmatched. */
	if (command[0] == '!' || command[0] == '(' || command[0] == ')' || command[0] == 'F')
		return 0;
	if (command[0] == '*') {
		if (host->soft_reset == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		*operation = true;
		return host->soft_reset(host->reset_context);
	}
	if (command[0] == 'w')
		return normal_text_window(host, command, length, operation, sink);
	if (command[0] == 'b')
		return extended_text_window(host, command, length, operation);
	if (command[0] == 'M')
		return color_mode(host, command, length, operation);
	if (length >= 2 && command[0] == '1' && command[1] == 'U')
		return st_rip3_button_create_execute(host, command, length, operation);
	if (length >= 2 && command[0] == '1'
	    && (command[1] == 'M' || command[1] == 'K' || command[1] == 'k' || command[1] == 'c'))
		return st_rip3_mouse_commands_execute(host, command, length, operation);
	if (length >= 2 && (command[0] == '1' || command[0] == '2') && command[1] == 'B')
		return st_rip3_button_commands_execute(host, command, length, operation, sink);
	if (command[0] == 'g' || command[0] == 'H')
		return text_cursor(host, command, length, operation);
	if (command[0] == 'e' || command[0] == '>') {
		if (host->text_window == NULL || host->text_window->erase == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		*operation = true;
		return host->text_window->erase(host->text_window->context, command[0] == 'e');
	}
	if (length >= 2 && command[0] == '2' && command[1] == 'Y')
		return switch_style(host, command, length, operation, sink);
	if (length >= 2 && command[0] == '2' && command[1] == 'A')
		return switch_palette(host, command, length, operation, sink);
	if (length >= 2 && command[0] == '2' && command[1] == 'E')
		return switch_environment(host, command, length, operation, sink);
	if (length >= 2 && command[0] == '2' && command[1] == 'T')
		return switch_text_window(host, command, length, operation);
	if (length >= 2 && command[0] == '2' && command[1] == 'R') {
		struct st_rip3_fields fields = {command + 2, length - 2};
		status                       = st_rip3_command_environment(host, &number, &value);
		if (status == 0)
			status = st_rip3_command_next_field(&fields, NULL, 4, value.base_math, false);
		if (status != 0)
			return status;
		*operation = true;
		return st_rip3_refresh_define(host->refresh, fields.bytes, fields.remaining);
	}
	if (length >= 2 && (command[0] == '1' || command[0] == '2') && command[1] == 'W')
		return st_rip3_bitmap_commands_execute(host, command, length, operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'G' || command[1] == 'g'))
		return st_rip3_scroll_blit_command(host, command, length, operation);
	if (length >= 2 && command[0] == '1' && command[1] == 'C')
		return st_rip3_get_image_commands_execute(host, command, length, operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'b' || command[1] == 'I'))
		return st_rip3_bitmap_load_commands_execute(host, command, length, operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'p' || command[1] == 'i'))
		return st_rip3_image_commands_execute(host, command, length, operation);
	if ((length >= 2 && command[0] == '1' && command[1] == 'P') || command[0] == 'v'
	    || (length >= 2 && command[0] == '2'
	        && (command[1] == 'P' || command[1] == 'p' || command[1] == 'C' || command[1] == 's')))
		return st_rip3_port_commands_execute(host, command, length, operation);
	if (command[0] == 'm' || command[0] == 'T' || command[0] == '@' || command[0] == '"')
		return st_rip3_text_commands_execute(host, command, length, operation);
	if (command[0] == 'y' || command[0] == 'q')
		return st_rip3_font_commands_execute(host, command, length, operation);
	if (command[0] == 'A' || command[0] == 'C' || command[0] == 'G' || command[0] == 'o' || command[0] == 'O'
	    || command[0] == 'V' || command[0] == 'I' || command[0] == 'i' || command[0] == '_')
		return execute_drawing(host, command, length, operation, st_rip3_curve_commands_execute);
	if (command[0] == '&' || command[0] == '-' || command[0] == ']' || command[0] == '+' || command[0] == '['
	    || command[0] == ';')
		return execute_drawing(host, command, length, operation, st_rip3_skew_commands_execute);
	if (command[0] == ',' || command[0] == '.' || command[0] == ':' || command[0] == '`' || command[0] == '{')
		return execute_drawing(host, command, length, operation, st_rip3_affine_command_execute);
	if (command[0] == 'Z' || command[0] == 'z' || command[0] == 't' || command[0] == 'x')
		return execute_drawing(host, command, length, operation, st_rip3_bezier_commands_execute);
	if (command[0] == 'U' || command[0] == 'u')
		return execute_drawing(host, command, length, operation, st_rip3_rounded_commands_execute);
	if (command[0] == 'P' || command[0] == 'l' || command[0] == 'p' || command[0] == '<')
		return execute_drawing(host, command, length, operation, st_rip3_path_commands_execute);
	if (command[0] == 'L' || command[0] == 'R' || command[0] == 'B' || command[0] == 'K' || command[0] == 'X'
	    || command[0] == 'j')
		return execute_drawing(host, command, length, operation, st_rip3_drawing_commands_execute);
	if (command[0] == 'c' || command[0] == 'k' || command[0] == 'W' || command[0] == 'Y' || command[0] == 'S'
	    || command[0] == 's' || command[0] == 'E' || command[0] == '=' || command[0] == 'N')
		return st_rip3_drawing_commands_execute(host, command, length, operation);
	if (command[0] == 'Q' || command[0] == 'a')
		return desktop_palette(host, command, length, operation);
	if (command[0] == 'D' || command[0] == 'd')
		return drawing_palette(host, command, length, operation);
	if (command[0] != 'J' && command[0] != 'n' && command[0] != 'f' && !delay)
		return IO_ERROR_NOT_SUPPORTED;
	size_t                prefix = delay ? 2 : 1;
	struct st_rip3_fields fields = {command + prefix, length - prefix};
	if (command[0] == 'J') {
		status = st_rip3_command_next_field(&fields, &first, 2, 36, true);
		if (status != 0)
			return status;
		status = st_rip3_command_environment(host, &number, &value);
		if (status != 0)
			return status;
		value.base_math = first == 64 ? 64 : 36;
	}
	else {
		status = st_rip3_command_environment(host, &number, &value);
		if (status != 0)
			return status;
		if (delay) {
			status = st_rip3_command_next_field(&fields, &first, 4, value.base_math, true);
			if (status != 0)
				return status;
			if (host->input == NULL)
				return IO_ERROR_NOT_SUPPORTED;
			*operation = true;
			st_rip3_input_delay(host->input, (uint32_t)first);
			return 0;
		}
		else if (command[0] == 'n') {
			status = st_rip3_command_next_field(&fields, &first, 1, value.base_math, false);
			if (status == 0 && fields.remaining != 0)
				status = st_rip3_command_next_field(&fields, NULL, 3, value.base_math, true);
			if (status != 0)
				return status;
			if (first == 0)
				value.coordinate_size = 2;
			else if (first >= 2 && first <= 5)
				value.coordinate_size = (unsigned)first;
		}
		else {
			size_t width = value.coordinate_size;

			status = st_rip3_command_next_field(&fields, &first, width, value.base_math, false);
			if (status == 0)
				status =
				    st_rip3_command_next_field(&fields, &second, width, value.base_math, true);
			if (status != 0)
				return status;
			if (first > 65535 || second > 65535)
				return IO_ERROR_OUT_OF_RANGE;
			value.world_width  = (uint32_t)first;
			value.world_height = (uint32_t)second;
		}
	}
	*operation = true;
	return st_rip3_environments_set(host->environments, number, &value);
}

void
st_rip3_commands_process(const struct st_rip3_command_host *host, const struct st_rip3_command_sink *sink,
    const uint8_t *command, size_t length)
{
	bool operation;
	if (host != NULL)
		st_rip3_ports_bind_drawing_palette(host->ports, st_rip3_palettes_published(host->palettes));
	int status = execute(host, command, length, &operation, sink);
	st_rip3_command_result(sink, status, operation);
}

void
st_rip3_command_result(const struct st_rip3_command_sink *sink, int status, bool operation)
{
	enum st_rip3_command_diagnostic kind;

	if (status == 0)
		return;
	if (status == IO_ERROR_NOT_SUPPORTED)
		kind = ST_RIP3_COMMAND_UNSUPPORTED;
	else if (status == IO_ERROR_OUT_OF_MEMORY)
		kind = ST_RIP3_COMMAND_RESOURCE_FAILURE;
	else if (!operation
	         && (status == IO_ERROR_INVALID_ARGUMENT || status == IO_ERROR_INVALID_FORMAT
	             || status == IO_ERROR_OUT_OF_RANGE || status == IO_ERROR_INVALID_ENCODING))
		kind = ST_RIP3_COMMAND_INVALID;
	else
		kind = ST_RIP3_COMMAND_OPERATION_FAILED;
	/* The command boundary is owned by the enclosing framer. Discard this
	 * command only; neither syntax nor service errors escape to the session. */
	if (sink != NULL && sink->diagnostic != NULL)
		sink->diagnostic(sink->context, kind);
}
