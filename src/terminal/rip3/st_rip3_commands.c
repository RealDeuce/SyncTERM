/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_audio.h"
#include "st_rip3_environments.h"
#include "st_rip3_input.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_number.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
st_rip3_command_field(uint64_t *result, const uint8_t *bytes, size_t length, unsigned radix)
{
	uint8_t digits[5];

	if (length == 0 || length > sizeof(digits))
		return IO_ERROR_INVALID_FORMAT;
	/* This is a documented wire quirk, not a change to the strict reusable
	 * digit codec or to the frozen legacy number reader. */
	for (size_t i = 0; i < length; i++)
		digits[i] = radix == 36 && bytes[i] == '-' ? '0' : bytes[i];
	return st_rip3_number_decode(result, digits, length, radix);
}

int
st_rip3_command_environment(const struct st_rip3_command_host *host, unsigned *number,
    struct st_rip3_environment *value)
{
	int status;

	if (host->environments == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_environments_current(host->environments, number);
	if (status == 0)
		status = st_rip3_environments_get(host->environments, *number, value);
	return status;
}

int
st_rip3_command_frame(const struct st_rip3_command_host *host,
    unsigned environment, struct st_rip3_frame *result)
{
	struct st_rip3_frame frame;
	struct st_rip3_port_info screen;
	int status = st_rip3_environments_frame(host->environments, environment, &frame);

	if (status == 0)
		status = st_rip3_ports_info(host->ports, 0, &screen);
	if (status != 0)
		return status;
	if (screen.bounds.x0 != 0 || screen.bounds.y0 != 0
	    || (uint32_t)screen.bounds.x1 + 1 != frame.device_width
	    || (uint32_t)screen.bounds.y1 + 1 != frame.device_height)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = frame;
	return 0;
}

static int
desktop_palette(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	unsigned number, colors[16];
	uint64_t value;
	size_t count = length - 1, start = 0, entries = 16, offset = 0;
	const uint8_t *arguments = command + 1;
	int status;

	/* General comment boundaries remain unresolved under #7. Do not treat
	 * trailing text as a partially valid palette update. */
	if (memchr(arguments, ' ', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	if (command[0] == 'a') {
		if (count < 3 || count > 4)
			return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&value, arguments, 2, environment.base_math);
		if (status != 0)
			return status;
		if (value > 15)
			return IO_ERROR_OUT_OF_RANGE;
		start = (size_t)value;
		entries = 1;
		offset = 2;
	}
	else if (count < 31 || count > 32)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < entries; i++) {
		size_t width = i + 1 == entries ? count - offset : 2;

		status = st_rip3_command_field(&value, arguments + offset, width, environment.base_math);
		if (status != 0)
			return status;
		if (value > 63)
			return IO_ERROR_OUT_OF_RANGE;
		colors[i] = (unsigned)value;
		offset += width;
	}
	/* No state changes before every field passes. ENV supplies only the
	 * radix; the bound palette's current entry and protection own the edit. */
	*operation = true;
	return st_rip3_palettes_desktop(host->palettes, start, colors, entries);
}

static int
immediate_query(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	unsigned number;
	uint64_t mode, window, reserved;
	int status;

	if (length < 6)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_environment(host,&number,&environment);
	if (status == 0)
		status = st_rip3_command_field(&mode,command+2,1,environment.base_math);
	if (status == 0)
		status = st_rip3_command_field(&window,command+3,1,environment.base_math);
	if (status == 0)
		status = st_rip3_command_field(&reserved,command+4,2,environment.base_math);
	if (status != 0)
		return status;
	(void)window; /* Immediate queries do not address a text window or port. */
	if (mode != 0 || reserved != 0)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	/* Private ownership/preflight status is consumed by the void command
	 * boundary. Nested variable diagnostics remain inside its void parser. */
	return st_rip3_query_start(host->query,command+6,length-6);
}

static int
execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment value;
	unsigned number;
	uint64_t first, second;
	const uint8_t *arguments, *comment;
	size_t count;
	bool baud, delay, commented;
	int status;

	*operation = false;
	if (host == NULL || command == NULL || length == 0 || length == SIZE_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (memchr(command, 0, length) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	baud = length >= 2 && command[0] == '3' && command[1] == 'e';
	delay = length >= 2 && command[0] == '3' && command[1] == 'D';
	/* Comments bypass every expansion/service, and terminal-only grouping
	 * commands carry no state, including when nested or unmatched. */
	if (command[0] == '!')
		return 0;
	if (command[0] == '(' || command[0] == ')')
		return length == 1 ? 0 : IO_ERROR_INVALID_FORMAT;
	if (length >= 2 && command[0] == '1' && command[1] == 0x1b)
		return immediate_query(host,command,length,operation);
	if (length >= 2 && (command[0] == '1' || command[0] == '2') && command[1] == 'W')
		return st_rip3_bitmap_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && command[1] == 'C')
		return st_rip3_get_image_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'b' || command[1] == 'I'))
		return st_rip3_bitmap_load_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && (command[1] == 'p' || command[1] == 'i'))
		return st_rip3_image_commands_execute(host,command,length,operation);
	if (length >= 2 && command[0] == '1' && command[1] == 'w') {
		char *filename;
		bool off;

		if (length < 7)
			return IO_ERROR_INVALID_FORMAT;
		if (memcmp(command + 2, "0000", 4) != 0)
			return IO_ERROR_NOT_SUPPORTED;
		off = length == 11 && memcmp(command + 6, "$OFF$", 5) == 0;
		if (host->audio == NULL || (host->resources == NULL && !off))
			return IO_ERROR_NOT_SUPPORTED;
		filename = strndup((const char *)command + 6, length - 6);
		if (filename == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		*operation = true;
		status = st_rip3_audio_play(host->audio, host->resources, filename);
		free(filename);
		return status;
	}
	if ((length >= 2 && command[0] == '1' && command[1] == 'P')
	    || command[0] == 'v' || (length >= 2 && command[0] == '2'
	    && (command[1] == 'P' || command[1] == 'p' || command[1] == 'C' || command[1] == 's')))
		return st_rip3_port_commands_execute(host, command, length, operation);
	if (command[0] == 'm' || command[0] == 'T' || command[0] == '@')
		return st_rip3_text_commands_execute(host,command,length,operation);
	if (command[0] == 'A' || command[0] == 'C' || command[0] == 'G' || command[0] == 'o'
	    || command[0] == 'O' || command[0] == 'V')
		return st_rip3_curve_commands_execute(host, command, length, operation);
	if (command[0] == 'P' || command[0] == 'l' || command[0] == 'p')
		return st_rip3_path_commands_execute(host, command, length, operation);
	if (command[0] == 'c' || command[0] == 'k' || command[0] == 'W' || command[0] == 'Y'
	    || command[0] == 'L' || command[0] == 'R' || command[0] == 'B' || command[0] == 'K'
	    || command[0] == 'S' || command[0] == 's' || command[0] == 'E' || command[0] == '='
	    || command[0] == 'X' || command[0] == 'j')
		return st_rip3_drawing_commands_execute(host, command, length, operation);
	if (command[0] == 'Q' || command[0] == 'a')
		return desktop_palette(host, command, length, operation);
	if (command[0] != 'J' && command[0] != 'n' && command[0] != 'f' && !baud && !delay)
		return IO_ERROR_NOT_SUPPORTED;
	arguments = command + (baud || delay ? 2 : 1); count = length - (baud || delay ? 2 : 1);
	/* The pinned f example explicitly retains the command before a spaced
	 * inline comment. Unseparated extra-data rules remain under issue #7.
	 * Text-valued commands never take this numeric-only branch. */
	comment = memchr(arguments, ' ', count);
	commented = comment != NULL;
	if (comment != NULL)
		count = (size_t)(comment - arguments);
	if (commented && command[0] != 'f')
		return IO_ERROR_NOT_SUPPORTED;
	if (count == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (command[0] == 'J') {
		if (count > 2)
			return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&first, arguments, count, 36);
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
		if (baud || delay) {
			if (count > 4)
				return IO_ERROR_INVALID_FORMAT;
			status = st_rip3_command_field(&first, arguments, count, value.base_math);
			if (status != 0)
				return status;
			if (delay) {
				if (host->input == NULL)
					return IO_ERROR_NOT_SUPPORTED;
				*operation = true;
				st_rip3_input_delay(host->input,(uint32_t)first);
				return 0;
			}
			value.baud_rate = first;
		}
		else if (command[0] == 'n') {
			if (count > 4)
				return IO_ERROR_INVALID_FORMAT;
			status = st_rip3_command_field(&first, arguments, 1, value.base_math);
			if (status == 0 && count > 1) {
				status = st_rip3_command_field(&second, arguments + 1, count - 1, value.base_math);
				if (status == 0 && second != 0)
					return IO_ERROR_NOT_SUPPORTED;
			}
			if (status != 0)
				return status;
			value.coordinate_size = first >= 2 && first <= 5 ? (unsigned)first : 2;
		}
		else {
			size_t width = value.coordinate_size;

			if (commented && count != width * 2)
				return IO_ERROR_NOT_SUPPORTED;
			if (count <= width || count > width * 2)
				return IO_ERROR_INVALID_FORMAT;
			status = st_rip3_command_field(&first, arguments, width, value.base_math);
			if (status == 0)
				status = st_rip3_command_field(&second, arguments + width, count - width, value.base_math);
			if (status != 0)
				return status;
			if (first == 0 || second == 0 || first > 32767 || second > 32767)
				return IO_ERROR_OUT_OF_RANGE;
			value.world_width = (uint32_t)first;
			value.world_height = (uint32_t)second;
		}
	}
	*operation = true;
	return st_rip3_environments_set(host->environments, number, &value);
}

void
st_rip3_commands_process(const struct st_rip3_command_host *host,
    const struct st_rip3_command_sink *sink, const uint8_t *command, size_t length)
{
	bool operation;
	int status = execute(host, command, length, &operation);
	enum st_rip3_command_diagnostic kind;

	if (status == 0)
		return;
	if (status == IO_ERROR_NOT_SUPPORTED)
		kind = ST_RIP3_COMMAND_UNSUPPORTED;
	else if (status == IO_ERROR_OUT_OF_MEMORY)
		kind = ST_RIP3_COMMAND_RESOURCE_FAILURE;
	else if (!operation && (status == IO_ERROR_INVALID_ARGUMENT
	    || status == IO_ERROR_INVALID_FORMAT || status == IO_ERROR_OUT_OF_RANGE
	    || status == IO_ERROR_INVALID_ENCODING))
		kind = ST_RIP3_COMMAND_INVALID;
	else
		kind = ST_RIP3_COMMAND_OPERATION_FAILED;
	/* The command boundary is owned by the enclosing framer. Discard this
	 * command only; neither syntax nor service errors escape to the session. */
	if (sink != NULL && sink->diagnostic != NULL)
		sink->diagnostic(sink->context, kind);
}
