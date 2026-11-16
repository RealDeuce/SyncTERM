/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_stream.h"
#include "io_error.h"

#include <stdlib.h>

struct st_rip3_stream {
	struct st_rip3_stream_sink sink;
	uint8_t *command;
	size_t length, capacity;
	uint8_t introducer, previous_text;
	bool active, line_start, quoted, comment, dropping;
};

static void
report(struct st_rip3_stream *stream, enum st_rip3_stream_diagnostic kind)
{
	if (stream->sink.diagnostic != NULL)
		stream->sink.diagnostic(stream->sink.context, kind);
}

static void
raw(struct st_rip3_stream *stream, uint8_t byte)
{
	if (stream->sink.text != NULL)
		stream->sink.text(stream->sink.context, &byte, 1);
	stream->previous_text = byte;
	stream->line_start = byte == 0 || byte == '\r' || byte == '\n' || byte == '\v';
}

static void
clear_command(struct st_rip3_stream *stream)
{
	stream->length = 0;
	stream->quoted = false;
	stream->comment = false;
	stream->dropping = false;
}

static void
dispatch(struct st_rip3_stream *stream)
{
	if (!stream->dropping && stream->length != 0 && stream->sink.command != NULL)
		stream->sink.command(stream->sink.context, stream->command, stream->length);
	clear_command(stream);
}

static void
append(struct st_rip3_stream *stream, uint8_t byte)
{
	if (stream->dropping)
		return;
	if (stream->length == stream->capacity) {
		size_t capacity = stream->capacity == 0 ? 256
		    : stream->capacity <= SIZE_MAX / 2 ? stream->capacity * 2 : SIZE_MAX;
		uint8_t *command = capacity > stream->capacity
		    ? realloc(stream->command, capacity) : NULL;

		if (command == NULL) {
			stream->dropping = true;
			report(stream, ST_RIP3_STREAM_RESOURCE_FAILURE);
			return;
		}
		stream->command = command;
		stream->capacity = capacity;
	}
	stream->command[stream->length++] = byte;
}

int
st_rip3_stream_create(struct st_rip3_stream **result,
    const struct st_rip3_stream_sink *sink)
{
	struct st_rip3_stream *stream;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	stream = calloc(1, sizeof(*stream));
	if (stream == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (sink != NULL)
		stream->sink = *sink;
	stream->line_start = true;
	*result = stream;
	return 0;
}

void
st_rip3_stream_destroy(struct st_rip3_stream *stream)
{
	if (stream != NULL) {
		free(stream->command);
		free(stream);
	}
}

void
st_rip3_stream_reset(struct st_rip3_stream *stream)
{
	if (stream == NULL)
		return;
	clear_command(stream);
	stream->introducer = 0;
	stream->previous_text = 0;
	stream->active = false;
	stream->line_start = true;
}

void
st_rip3_stream_process(struct st_rip3_stream *stream, const uint8_t *bytes, size_t length)
{
	if (stream == NULL)
		return;
	if ((bytes == NULL && length != 0) || length == SIZE_MAX) {
		report(stream, ST_RIP3_STREAM_INVALID_INPUT);
		return;
	}
	for (size_t i = 0; i < length; i++) {
		uint8_t byte = bytes[i];

		if (stream->active) {
			if (byte == '\n')
				continue;
			if (byte == '\r') {
				if (stream->quoted) {
					/* Only the continuation slash is removed. The
					 * remaining even run stays quoted for later stages. */
					if (!stream->dropping)
						stream->length--;
					stream->quoted = false;
				}
				else {
					dispatch(stream);
					stream->active = false;
					stream->line_start = true;
					stream->previous_text = '\r';
				}
				continue;
			}
			/* Comments use any pipe as the next-command boundary;
			 * their contents never undergo text-variable expansion. */
			if (byte == '|' && (!stream->quoted || stream->comment)) {
				dispatch(stream);
				continue;
			}
			if (stream->length == 0 && !stream->dropping)
				stream->comment = byte == '!';
			append(stream, byte);
			/* Preserve parity even after allocation failure, so an
			 * escaped pipe/continued CR cannot resume in corrupt data. */
			stream->quoted = byte == '\\' ? !stream->quoted : false;
			continue;
		}
		if (stream->introducer != 0) {
			uint8_t introducer = stream->introducer;

			stream->introducer = 0;
			if (byte == '|') {
				stream->active = true;
				clear_command(stream);
			}
			else {
				raw(stream, introducer);
				raw(stream, byte);
			}
			continue;
		}
		if (stream->previous_text != '\\'
		    && (byte == 1 || byte == 2 || (byte == '!'
		    && (stream->line_start || (stream->sink.at_line_start != NULL
		    && stream->sink.at_line_start(stream->sink.context))))))
			stream->introducer = byte;
		else
			raw(stream, byte);
	}
}
