/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_stream.h"
#include "io_error.h"

#include <stdlib.h>

struct st_rip3_stream {
	struct st_rip3_stream_sink sink;
	uint8_t *command;
	size_t length, capacity;
	const char *fields;
	struct st_rip3_stream_numbers numbers;
	unsigned field_digits;
	uint8_t introducer, previous_text;
	bool active, line_start, quoted, comment, discard_tail, continued, dropping, skip_lf;
};

/* Widths from the 3.0.7 fixed numeric descriptors (table RVA 0x80820).
 * x is XY, C is CM, and other widths are hexadecimal digits. NULL denotes
 * a string, variable-length, special collector or unsupported command. */
static const char *const level_zero[128] = {
	['#'] = "", ['&'] = "xx222", ['('] = "", [')'] = "", ['*'] = "",
	['+'] = "xxxx222", [','] = "xxxxxxxxxx", ['-'] = "xxxx2", ['.'] = "xxxxxx",
	[':'] = "xxxxxxxxxx1", [';'] = "xx2xx22", ['='] = "1142", ['>'] = "",
	['A'] = "xx22x", ['B'] = "xxxx", ['C'] = "xxx", ['E'] = "", ['F'] = "",
	['G'] = "xxx", ['H'] = "", ['I'] = "xx22x", ['J'] = "2", ['K'] = "xxxx",
	['L'] = "xxxx", ['M'] = "11", ['N'] = "2", ['O'] = "xx22xx",
	['Q'] = "2222222222222222", ['R'] = "xxxx", ['S'] = "2C", ['U'] = "xxxxx",
	['V'] = "xx22xx", ['W'] = "2", ['X'] = "xx", ['Y'] = "2222", ['Z'] = "xxxxxxxx2",
	['['] = "xxxx222", [']'] = "xxxx222", ['_'] = "xx22xx", ['`'] = "xxxxxxxxxx1",
	['a'] = "22", ['b'] = "xxxx22143", ['c'] = "C", ['d'] = "214", ['e'] = "",
	['f'] = "xx", ['g'] = "xx", ['i'] = "xx22xx", ['j'] = "xx", ['k'] = "C",
	['m'] = "xx", ['n'] = "13", ['o'] = "xxxx", ['q'] = "2", ['s'] = "22222222C",
	['u'] = "xxxxx", ['v'] = "xxxx", ['w'] = "222211", ['{'] = "xxxxxx",
};
static const char *const level_one[128] = {
	['A'] = "24", ['B'] = "xx24222222222215", ['C'] = "xxxx1", ['E'] = "",
	['G'] = "xxxx11x", ['K'] = "", ['P'] = "xx21", ['T'] = "xxxx11",
	['c'] = "24", ['g'] = "xxxxxx11", ['i'] = "xxxx4c", ['k'] = "xxxx4",
};
static const char *const level_two[128] = {
	['A'] = "12", ['B'] = "12", ['C'] = "1xxxx1xxxx15", ['E'] = "12",
	['P'] = "1xxxx44", ['T'] = "12", ['Y'] = "12", ['p'] = "112", ['s'] = "12",
};

static void
start_fields(struct st_rip3_stream *stream, uint8_t level, uint8_t opcode)
{
	const char *fields = NULL;
	if (opcode >= 128)
		return;
	switch (level) {
	case 0: fields = level_zero[opcode]; break;
	case '1': fields = level_one[opcode]; break;
	case '2': fields = level_two[opcode]; break;
	case '3': fields = opcode == 'D' ? "4" : opcode == 'e' ? "2" : NULL; break;
	case '9': if (opcode == 'U') fields = "24"; break;
	}
	if (fields == NULL)
		return;
	if (*fields == '\0') {
		stream->discard_tail = true;
		return;
	}
	stream->fields = fields;
	stream->numbers = (struct st_rip3_stream_numbers){36, 2, false};
	if (stream->sink.number_settings != NULL)
		stream->sink.number_settings(stream->sink.context, &stream->numbers);
	if (level == 0 && (opcode == 'J' || opcode == 'N'))
		stream->numbers.base_math = 36;
	else if (level == 0 && opcode == 'd')
		stream->numbers.base_math = 64;
}

static void
collect_field(struct st_rip3_stream *stream, uint8_t byte)
{
	if (byte == '\\' && !stream->quoted)
		return; /* Framing removes a continuation slash before the next digit. */
	char field = *stream->fields;
	const struct st_rip3_stream_numbers *numbers = &stream->numbers;
	bool rgb = field == 'C' && numbers->direct_rgb;
	bool ultra = rgb || numbers->base_math == 64;
	unsigned width = field == 'x' ? numbers->coordinate_width
	    : field == 'C' ? (rgb ? 4 : 2) : field >= 'a' ? (unsigned)(field - 'a' + 10) : (unsigned)(field - '0');
	bool digit = (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z')
	    || (ultra && ((byte >= 'a' && byte <= 'z') || byte == '#' || byte == '&'));
	if (stream->quoted || (!digit && (byte != '.' || stream->field_digits != 0))) {
		stream->fields = NULL;
		return; /* The command decoder diagnoses a malformed field. */
	}
	if (byte == '.' || ++stream->field_digits == width) {
		stream->field_digits = 0;
		stream->fields++;
		stream->discard_tail = *stream->fields == '\0';
	}
}

static bool
line_end(uint8_t byte)
{
	return byte == '\r' || byte == '\n' || byte == '\f';
}

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
	stream->line_start = line_end(byte) || byte == 0 || byte == '\v';
}

static void
clear_command(struct st_rip3_stream *stream)
{
	stream->length = 0;
	stream->fields = NULL;
	stream->field_digits = 0;
	stream->quoted = false;
	stream->comment = false;
	stream->discard_tail = false;
	stream->continued = false;
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
	stream->skip_lf = false;
	stream->line_start = true;
}

bool
st_rip3_stream_pending_lf(const struct st_rip3_stream *stream)
{
	return stream->skip_lf;
}

void
st_rip3_stream_clear_pending_lf(struct st_rip3_stream *stream)
{
	stream->skip_lf = false;
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
		if (stream->sink.framing_enabled != NULL && !*stream->sink.framing_enabled) {
			raw(stream, byte);
			continue;
		}
		if (stream->skip_lf) {
			stream->skip_lf=false;
			if (byte=='\n') continue;
		}

		if (stream->active) {
			if (line_end(byte)) {
				if (stream->continued) {
					/* The descriptor's delimiter wait skips only the first
					 * two line-ending bytes after a backslash. */
					if (stream->discard_tail)
						stream->continued = false;
					continue;
				}
				if (stream->quoted) {
					/* Only the continuation slash is removed. The
					 * remaining even run stays quoted for later stages. */
					if (!stream->dropping && !stream->discard_tail)
						stream->length--;
					stream->quoted = false;
					stream->continued = true;
				}
				else {
					stream->skip_lf = byte == '\r';
					dispatch(stream);
					stream->active = false;
					stream->line_start = true;
					stream->previous_text = byte;
				}
				continue;
			}
			stream->continued = false;
			/* Comments and completed fixed fields end at any pipe. */
			if (byte == '|' && (!stream->quoted || stream->comment || stream->discard_tail)) {
				dispatch(stream);
				continue;
			}
			if (stream->discard_tail) {
				/* Retain the collected fields. The last suffix byte can
				 * introduce a continuation regardless of slash parity. */
				stream->quoted = byte == '\\';
				continue;
			}
			if (!stream->dropping) {
				if (stream->length == 0) {
					stream->comment = byte == '!';
					start_fields(stream, 0, byte);
				}
				else if (stream->fields != NULL)
					collect_field(stream, byte);
				else if (stream->length == 1 && !stream->quoted && stream->command[0] != 0)
					start_fields(stream, stream->command[0], byte);
			}
			if (stream->quoted && !stream->comment && (byte == '|' || byte == 1 || byte == 2)) {
				/* These bytes were quoted for framing. Replace the stored
				 * slash with the literal byte before passing the command on. */
				if (!stream->dropping)
					stream->command[stream->length-1] = byte;
			}
			else
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
		if (byte == '!' && stream->sink.ansi_owns_bang != NULL && stream->sink.ansi_owns_bang(stream->sink.context)) {
			raw(stream, byte);
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
