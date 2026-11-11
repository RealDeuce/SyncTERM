/* Incremental RIP wire recognizer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_parser.h"

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
cterm_rip_parser_init(struct cterm_rip_parser *parser, bool enabled)
{
	memset(parser, 0, sizeof(*parser));
	parser->enabled = enabled;
}

void
cterm_rip_parser_destroy(struct cterm_rip_parser *parser)
{
	if (parser == NULL)
		return;
	free(parser->command);
	memset(parser, 0, sizeof(*parser));
}

static int
emit(const struct cterm_rip_parser_sink *sink, const uint8_t *bytes,
    size_t size)
{
	return size == 0 ? 0 : sink->passthrough(sink->context, bytes, size);
}

static void
slide(struct cterm_rip_parser *parser, uint8_t byte)
{
	memmove(parser->window + 1, parser->window, sizeof(parser->window) - 1);
	parser->window[0] = byte;
}

static int
append_byte(struct cterm_rip_parser *parser, uint8_t byte)
{
	if (parser->length > SIZE_MAX - 2)
		return IO_ERROR_OUT_OF_RANGE;
	if (parser->length + 2 > parser->capacity) {
		size_t capacity = parser->capacity == 0 ? 256 : parser->capacity;
		uint8_t *grown;

		while (capacity < parser->length + 2) {
			if (capacity > SIZE_MAX / 2) {
				capacity = parser->length + 2;
				break;
			}
			capacity *= 2;
		}
		grown = realloc(parser->command, capacity);
		if (grown == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		parser->command = grown;
		parser->capacity = capacity;
	}
	parser->command[parser->length++] = byte;
	parser->command[parser->length] = 0;
	return 0;
}

static void
clear_command(struct cterm_rip_parser *parser)
{
	parser->length = 0;
	if (parser->command != NULL)
		parser->command[0] = 0;
}

static void
warn(const struct cterm_rip_parser_sink *sink, const char *operation, int status)
{
	char message[128];
	(void)snprintf(message, sizeof(message), "RIP %s rejected (%d)", operation, status);
	if (sink->warning != NULL) sink->warning(sink->context, message);
}

static int
push(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, uint8_t byte)
{
	int status = append_byte(parser, byte);
	if (status != 0) {
		warn(sink, "command buffer", status);
		clear_command(parser);
	}
	return 0;
}

static bool
accept_introducer(const struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, uint8_t byte)
{
	if (byte != '!' && byte != 0x01 && byte != 0x02)
		return false;
	if (parser->window[0] == '\\')
		return false;
	if (byte != '!')
		return true;
	if (parser->window[0] == 0 || parser->window[0] == '\r'
	    || parser->window[0] == '\n' || parser->window[0] == '\v')
		return true;
	return sink->at_carriage_return_column != NULL
	    && sink->at_carriage_return_column(sink->context);
}

static size_t
trailing_backslashes(const struct cterm_rip_parser *parser)
{
	size_t count = 0;

	while (count < parser->length
	    && parser->command[parser->length - count - 1] == '\\')
		count++;
	return count;
}

static int
dispatch_command(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink)
{
	size_t source = 0;
	size_t target = 0;
	size_t index;
	int level = 0;
	int sublevel = 0;
	uint8_t code;
	int status = 0;

	parser->ebc = true;
	if (parser->length < 2) {
		clear_command(parser);
		return 0;
	}
	while (source < parser->length) {
		uint8_t byte = parser->command[source++];

		if (byte == '\\' && source < parser->length) {
			byte = parser->command[source++];
			if (byte == '|')
				byte = 0xff;
		}
		parser->command[target++] = byte;
	}
	parser->length = target;
	parser->command[target] = 0;
	if (target < 2) {
		clear_command(parser);
		return 0;
	}
	if (parser->command[0] == '!' || parser->command[0] == 0x01
	    || parser->command[0] == 0x02) {
		if (parser->command[1] != '|') {
			clear_command(parser);
			return 0;
		}
		index = 2;
	}
	else if (parser->command[0] == '|')
		index = 1;
	else {
		clear_command(parser);
		return 0;
	}
	if (index < target && parser->command[index] >= '1'
	    && parser->command[index] <= '9') {
		level = parser->command[index++] - '0';
		if (index < target && parser->command[index] >= '1'
		    && parser->command[index] <= '9')
			sublevel = parser->command[index++] - '0';
	}
	if (index >= target) {
		clear_command(parser);
		return 0;
	}
	code = parser->command[index++];
	for (size_t i = index; i < target; i++) {
		if (parser->command[i] == 0xff)
			parser->command[i] = '|';
	}
	if (sink->boundary != NULL)
		status = sink->boundary(sink->context);
	if (status == 0 && sink->command != NULL) {
		status = sink->command(sink->context, level, sublevel, code,
		    (const char *)&parser->command[index]);
		if (status != 0) {
			char operation[80];
			(void)snprintf(operation, sizeof(operation),
			    "command level=%d sublevel=%d code=0x%02X", level, sublevel, (unsigned)code);
			warn(sink, operation, status);
			status = 0;
		}
	}
	clear_command(parser);
	return status;
}

static size_t
prepare_plain(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, uint8_t byte,
    uint8_t output[3])
{
	bool suppress = false;
	size_t size = 0;

	if (parser->ebc) {
		suppress = byte != '\v';
		parser->ebc = false;
	}
	if (sink->text_disabled != NULL
	    && sink->text_disabled(sink->context))
		suppress = true;
	if (byte == 0x0e && parser->music_pending)
		parser->music_pending = false;
	else if (byte == 0x0e || byte == 0x0f)
		suppress = true;
	if (!suppress) {
		if (byte == '\v') {
			static const uint8_t down[] = {0x1b, '[', 'B'};

			memcpy(output, down, sizeof(down));
			size = sizeof(down);
		}
		else {
			output[0] = byte;
			size = 1;
		}
	}
	slide(parser, byte);
	return size;
}

static int
emit_plain(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, uint8_t byte)
{
	uint8_t output[3];
	size_t size = prepare_plain(parser, sink, byte, output);

	return emit(sink, output, size);
}

static bool
plain_batch_byte(uint8_t byte)
{
	return (byte >= 0x20 && byte != 0x7f && byte != '!')
	    || byte == '\r' || byte == '\n';
}

static int
feed_escape(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, uint8_t byte)
{
	int status;

	if (parser->escape_length == 0) {
		parser->escape[parser->escape_length++] = 0x1b;
		return 0;
	}
	if (parser->escape_length == sizeof(parser->escape)) {
		status = emit(sink, parser->escape, parser->escape_length);
		if (status == 0)
			status = emit(sink, &byte, 1);
		parser->escape_length = 0;
		return status;
	}
	if (parser->escape_length == 1 && byte == '[') {
		parser->escape[parser->escape_length++] = byte;
		return 0;
	}
	if (byte == '!' && (parser->escape_length == 2
	    || (parser->escape_length == 3 && parser->escape[2] >= '0'
	    && parser->escape[2] <= '2'))) {
		parser->escape[parser->escape_length++] = byte;
		status = sink->boundary == NULL ? 0 : sink->boundary(sink->context);
		if (status == 0 && sink->control != NULL) {
			status = sink->control(sink->context, parser->escape, parser->escape_length);
			if (status != 0) {
				warn(sink, "control", status);
				status = 0;
			}
		}
		if (parser->escape_length == 4 && parser->escape[2] == '1')
			parser->enabled = false;
		parser->escape_length = 0;
		return status;
	}
	if (parser->escape_length >= 2
	    && ((byte >= '0' && byte <= '9') || byte == ';')) {
		parser->escape[parser->escape_length++] = byte;
		return 0;
	}
	if (parser->escape_length == 2 && sink->music_introducer != NULL
	    && sink->music_introducer(sink->context, byte))
		parser->music_pending = true;
	status = emit(sink, parser->escape, parser->escape_length);
	if (status == 0)
		status = emit(sink, &byte, 1);
	if (status == 0 && parser->utf8_input) {
		/* The invalid ESC and following non-ASCII scalar are one
		 * literal fallback, even if RIP has disabled ordinary text. */
		if (byte >= 0xc2 && byte <= 0xdf)
			parser->escape_utf8_remaining = 1;
		else if (byte >= 0xe0 && byte <= 0xef)
			parser->escape_utf8_remaining = 2;
		else if (byte >= 0xf0 && byte <= 0xf4)
			parser->escape_utf8_remaining = 3;
	}
	parser->escape_length = 0;
	return status;
}

static int
feed_one(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, uint8_t byte)
{
	int status;

	if (!parser->enabled || parser->suspended)
		return emit(sink, &byte, 1);
	if (parser->escape_utf8_remaining != 0) {
		if (byte >= 0x80 && byte <= 0xbf) {
			/* A continuation byte belongs to the preceding literal
			 * scalar, not to the ordinary text-suppression path. */
			parser->escape_utf8_remaining--;
			return emit(sink, &byte, 1);
		}
		parser->escape_utf8_remaining = 0;
	}
	if (parser->escape_length != 0)
		return feed_escape(parser, sink, byte);
	if (byte == 0x1b && parser->length == 0)
		return feed_escape(parser, sink, byte);
	if (parser->length == 0) {
		if (accept_introducer(parser, sink, byte))
			return push(parser, sink, byte);
		return emit_plain(parser, sink, byte);
	}
	if (parser->length == 1 && byte != '|'
	    && (parser->command[0] == '!' || parser->command[0] == 0x01
	    || parser->command[0] == 0x02)) {
		uint8_t first = parser->command[0];
		uint8_t output[4] = {first};
		size_t size;

		clear_command(parser);
		parser->ebc = false;
		size = 1 + prepare_plain(parser, sink, byte, output + 1);
		return emit(sink, output, size);
	}
	/* SyncTERM v1.10a consumes LF unconditionally inside a RIP command. */
	if (byte == '\n')
		return 0;
	if (byte == '\r') {
		if (trailing_backslashes(parser) & 1) {
			parser->command[--parser->length] = 0;
			return 0;
		}
		parser->command_terminated_cr = true;
		return dispatch_command(parser, sink);
	}
	if (byte == '|') {
		if (trailing_backslashes(parser) & 1)
			return push(parser, sink, byte);
		parser->command_terminated_cr = false;
		status = dispatch_command(parser, sink);
		return status == 0 ? push(parser, sink, '|') : status;
	}
	return push(parser, sink, byte);
}

int
cterm_rip_parser_feed(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, const uint8_t *bytes,
    size_t size, size_t *consumed)
{
	int status;

	if (parser == NULL || sink == NULL || sink->passthrough == NULL
	    || consumed == NULL || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = 0;
	for (size_t i = 0; i < size; i++) {
		if (parser->enabled && !parser->suspended
		    && parser->length == 0 && parser->escape_length == 0
		    && parser->escape_utf8_remaining == 0
		    && !parser->ebc && plain_batch_byte(bytes[i])) {
			size_t end = i;

			while (end < size && plain_batch_byte(bytes[end])) {
				slide(parser, bytes[end]);
				end++;
			}
			status = sink->text_disabled != NULL
			    && sink->text_disabled(sink->context)
			    ? 0 : emit(sink, bytes + i, end - i);
			*consumed = end;
			if (status != 0)
				return status;
			i = end - 1;
			continue;
		}
		status = feed_one(parser, sink, bytes[i]);
		*consumed = i + 1;
		if (status != 0)
			return status;
	}
	return 0;
}

int
cterm_rip_parser_flush(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink)
{
	if (parser == NULL || sink == NULL || sink->passthrough == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	parser->command_terminated_cr = false;
	return parser->length == 0 ? 0 : dispatch_command(parser, sink);
}
