/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_audio.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"
#include "st_rip3_palettes.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
field(uint64_t *result, const uint8_t *bytes, size_t length, unsigned radix)
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

static int
current(const struct st_rip3_command_host *host, unsigned *number,
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
	status = current(host, &number, &environment);
	if (status != 0)
		return status;
	if (command[0] == 'a') {
		if (count < 3 || count > 4)
			return IO_ERROR_INVALID_FORMAT;
		status = field(&value, arguments, 2, environment.base_math);
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

		status = field(&value, arguments + offset, width, environment.base_math);
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
execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment value;
	unsigned number;
	uint64_t first, second;
	const uint8_t *arguments, *comment;
	size_t count;
	bool commented;
	int status;

	*operation = false;
	if (host == NULL || command == NULL || length == 0 || length == SIZE_MAX)
		return IO_ERROR_INVALID_ARGUMENT;
	if (memchr(command, 0, length) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	/* Comments bypass every expansion/service, and terminal-only grouping
	 * commands carry no state, including when nested or unmatched. */
	if (command[0] == '!')
		return 0;
	if (command[0] == '(' || command[0] == ')')
		return length == 1 ? 0 : IO_ERROR_INVALID_FORMAT;
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
	if (command[0] == 'Q' || command[0] == 'a')
		return desktop_palette(host, command, length, operation);
	if (command[0] != 'J' && command[0] != 'n' && command[0] != 'f')
		return IO_ERROR_NOT_SUPPORTED;
	arguments = command + 1; count = length - 1;
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
		status = field(&first, arguments, count, 36);
		if (status != 0)
			return status;
		status = current(host, &number, &value);
		if (status != 0)
			return status;
		value.base_math = first == 64 ? 64 : 36;
	}
	else {
		status = current(host, &number, &value);
		if (status != 0)
			return status;
		if (command[0] == 'n') {
			if (count > 4)
				return IO_ERROR_INVALID_FORMAT;
			status = field(&first, arguments, 1, value.base_math);
			if (status == 0 && count > 1) {
				status = field(&second, arguments + 1, count - 1, value.base_math);
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
			status = field(&first, arguments, width, value.base_math);
			if (status == 0)
				status = field(&second, arguments + width, count - width, value.base_math);
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
