/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_stream.h"
#include "st_rip3_bezier.h"
#include "st_rip3_environments.h"
#include "st_rip3_header.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

enum record_list { RECORDS_NONE, RECORDS_HEADER, RECORDS_BEZIER };

struct st_rip3_stream {
	struct st_rip3_stream_sink sink;
	const struct st_rip3_environments *environments;
	uint8_t *command;
	size_t length, capacity;
	const char *fields;
	enum record_list records;
	unsigned base_math, coordinate_width, field_digits, level_digits;
	bool direct_rgb, strict_prefix;
	uint8_t introducer;
	bool active, line_start, quoted, comment, discard_tail, continued, dropping, skip_lf;
	bool last_backslash, prior_backslash, numeric_continuation;
};

/* Widths from the 3.0.7 descriptors (table RVA 0x80820). x is XY, C is CM,
 * R is a required four-digit reserve, and literal widths are hexadecimal
 * digits. T starts text, * repeats the
 * preceding width. Typed lists continue with literal selectors after the
 * initial widths and after each complete record. */
static const char *const level_zero[128] = {
	['!'] = "T", ['"'] = "xxxx2T", ['#'] = "", ['&'] = "xx222",
	['('] = "", [')'] = "", ['*'] = "",
	['+'] = "xxxx222", [','] = "xxxxxxxxxx", ['-'] = "xxxx2", ['.'] = "xxxxxx",
	[':'] = "xxxxxxxxxx1", [';'] = "xx2xx22", ['<'] = "22x*", ['='] = "1142", ['>'] = "",
	['@'] = "xxT", ['A'] = "xx22x", ['B'] = "xxxx", ['C'] = "xxx", ['D'] = "2214*",
	['E'] = "", ['F'] = "",
	['G'] = "xxx", ['H'] = "", ['I'] = "xx22x", ['J'] = "2", ['K'] = "xxxx",
	['L'] = "xxxx", ['M'] = "11", ['N'] = "2", ['O'] = "xx22xx", ['P'] = "2x*",
	['Q'] = "2222222222222222", ['R'] = "xxxx", ['S'] = "2C", ['T'] = "T", ['U'] = "xxxxx",
	['V'] = "xx22xx", ['W'] = "2", ['X'] = "xx", ['Y'] = "2222", ['Z'] = "xxxxxxxx2",
	['['] = "xxxx222", [']'] = "xxxx222", ['_'] = "xx22xx", ['`'] = "xxxxxxxxxx1",
	['a'] = "22", ['b'] = "xxxx22143", ['c'] = "C", ['d'] = "214", ['e'] = "",
	['f'] = "xx", ['g'] = "xx", ['h'] = ST_RIP3_HEADER_BASE_FIELDS, ['i'] = "xx22xx", ['j'] = "xx", ['k'] = "C",
	['l'] = "2x*", ['m'] = "xx", ['n'] = "13", ['o'] = "xxxx", ['p'] = "2x*", ['q'] = "2",
	['r'] = "114T", ['s'] = "22222222C", ['t'] = ST_RIP3_BEZIER_LIST_FIELDS, ['u'] = "xxxxx", ['v'] = "xxxx",
	['w'] = "222211", ['x'] = ST_RIP3_BEZIER_LIST_FIELDS, ['y'] = "11422222226T",
	['z'] = ST_RIP3_BEZIER_LIST_FIELDS, ['{'] = "xxxxxx",
};
static const char *const level_one[128] = {
	[0x1b] = "112T",
	['A'] = "24", ['B'] = "xx24222222222215", ['C'] = "xxxx1", ['E'] = "",
	['D'] = "32T", ['F'] = "24T", ['G'] = "xxxx11x", ['I'] = "xx11111T", ['K'] = "",
	['M'] = "2xxxx1123T", ['P'] = "xx21", ['R'] = "26T", ['T'] = "xxxx11",
	['U'] = "xxxx211T", ['W'] = "1T", ['b'] = "xxxx11224T", ['c'] = "24",
	['e'] = "xxxx11428T", ['g'] = "xxxxxx11", ['i'] = "xxxx4c", ['k'] = "xxxx4",
	['p'] = "4T", ['t'] = "1T", ['w'] = "13T",
};
static const char *const level_two[128] = {
	[0x1b] = "4T",
	['A'] = "12", ['B'] = "12", ['C'] = "1xxxx1xxxx15", ['E'] = "12",
	['P'] = "1xxxx44", ['R'] = "4T", ['T'] = "12", ['W'] = "1xxxx22T", ['Y'] = "12",
	['p'] = "112", ['s'] = "12",
};
static const char *const level_nine[128] = {
	[0x1b] = "11222T", ['D'] = "RT", ['G'] = "8T", ['R'] = "428T", ['U'] = "24",
};

static void append(struct st_rip3_stream *stream, uint8_t byte);

static void
report(struct st_rip3_stream *stream, enum st_rip3_command_diagnostic kind)
{
	if (stream->sink.diagnostic != NULL)
		stream->sink.diagnostic(stream->sink.context, kind);
}

static void
reject_command(struct st_rip3_stream *stream, enum st_rip3_command_diagnostic kind)
{
	stream->fields = NULL;
	stream->dropping = true;
	stream->discard_tail = true;
	report(stream, kind);
}

static void
start_fields(struct st_rip3_stream *stream, uint8_t level, uint8_t opcode)
{
	const char *fields = NULL;
	if (opcode < 128) {
		switch (level) {
		case 0: fields = level_zero[opcode]; break;
		case '1': fields = level_one[opcode]; break;
		case '2': fields = level_two[opcode]; break;
		case '3': fields = opcode == 'D' ? "4" : opcode == 'e' ? "2" : NULL; break;
		case '9': fields = level_nine[opcode]; break;
		}
	}
	if (fields == NULL) {
		reject_command(stream, ST_RIP3_COMMAND_UNSUPPORTED);
		return;
	}
	if (*fields == 'T')
		return;
	if (*fields == '\0') {
		stream->discard_tail = true;
		return;
	}
	stream->fields = fields;
	/* The portable registration prefix requires all three fields. */
	stream->strict_prefix = level == '9' && opcode == 'R';
	const struct st_rip3_environments *environments = stream->environments;
	struct st_rip3_environment environment;
	stream->base_math = 36;
	stream->coordinate_width = 2;
	stream->direct_rgb = false;
	if (environments != NULL
	    && st_rip3_environments_active(environments, NULL, &environment) == 0) {
		stream->base_math = environment.base_math;
		stream->coordinate_width = environment.coordinate_size;
		stream->direct_rgb = environment.rgb_bits != 0;
	}
	if (level != 0)
		return;
	if (opcode == 'h')
		stream->records = RECORDS_HEADER;
	else if (opcode == 't' || opcode == 'x' || opcode == 'z')
		stream->records = RECORDS_BEZIER;
	if (opcode == 'J' || opcode == 'N')
		stream->base_math = 36;
	else if (opcode == 'd' || opcode == 'D' || opcode == 'h' || opcode == 'y')
		stream->base_math = 64;
}

static bool
collect_selector(struct st_rip3_stream *stream, uint8_t byte)
{
	/* In a typed list the empty descriptor expects another literal type. */
	unsigned type = byte - '0';
	if (stream->records == RECORDS_HEADER)
		stream->fields = st_rip3_header_segment_fields(type);
	else {
		unsigned coordinates = st_rip3_bezier_record_coordinates(type);
		stream->fields = coordinates == 6 ? "xxxxxx" : coordinates == 2 ? "xx" : NULL;
	}
	if (stream->fields != NULL)
		return true;
	reject_command(stream, ST_RIP3_COMMAND_INVALID);
	return false;
}

static void
pad_numeric_field(struct st_rip3_stream *stream, unsigned width)
{
	size_t digits = stream->field_digits;
	size_t first = stream->length - digits;
	size_t padding = width - digits;

	/* The wire's early terminator can finish a middle field. Widen its
	 * spelling for the command decoders, which consume ordinary fields. */
	for (size_t i = 0; i < padding && !stream->dropping; i++)
		append(stream, '0');
	if (stream->dropping || digits == 0)
		return;
	memmove(stream->command + first + padding, stream->command + first, digits);
	memset(stream->command + first, '0', padding);
}

static bool
collect_field(struct st_rip3_stream *stream, uint8_t byte)
{
	char field = *stream->fields;
	if (field == '\0')
		return collect_selector(stream, byte);
	bool rgb = field == 'C' && stream->direct_rgb;
	bool ultra = rgb || stream->base_math == 64;
	unsigned width = field == 'x' ? stream->coordinate_width
	    : field == 'C' ? (rgb ? 4 : 2) : field == 'R' ? 4
	    : field >= 'a' ? (unsigned)(field - 'a' + 10) : (unsigned)(field - '0');
	bool short_field = byte == '<' && field != 'R' && !stream->strict_prefix;
	bool digit = (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z')
	    || (ultra && ((byte >= 'a' && byte <= 'z') || byte == '#' || byte == '&'));
	if (short_field) {
		pad_numeric_field(stream, width);
		if (stream->dropping)
			return false;
	}
	else if (!digit && (byte != '.' || stream->field_digits != 0)) {
		reject_command(stream, ST_RIP3_COMMAND_INVALID);
		return false;
	}
	if (short_field || byte == '.' || ++stream->field_digits == width) {
		stream->field_digits = 0;
		if (stream->fields[1] != '*')
			stream->fields++;
		stream->discard_tail = *stream->fields == '\0' && stream->records == RECORDS_NONE;
		if (*stream->fields == 'T')
			stream->fields = NULL;
	}
	return !short_field;
}

static bool
line_end(uint8_t byte)
{
	return byte == '\r' || byte == '\n' || byte == '\f';
}

static void
raw(struct st_rip3_stream *stream, uint8_t byte)
{
	if (stream->sink.text != NULL)
		stream->sink.text(stream->sink.context, &byte, 1);
	stream->line_start = line_end(byte);
}

static void
clear_command(struct st_rip3_stream *stream)
{
	stream->length = 0;
	stream->fields = NULL;
	stream->strict_prefix = false;
	stream->records = RECORDS_NONE;
	stream->field_digits = 0;
	stream->level_digits = 0;
	stream->quoted = false;
	stream->comment = false;
	stream->discard_tail = false;
	stream->continued = false;
	stream->dropping = false;
	stream->last_backslash = false;
	stream->prior_backslash = false;
	stream->numeric_continuation = false;
}

static void
dispatch(struct st_rip3_stream *stream)
{
	if (!stream->dropping) {
		if (stream->fields != NULL && (stream->strict_prefix || *stream->fields == 'R'))
			report(stream, ST_RIP3_COMMAND_INVALID);
		else if (stream->length > stream->level_digits && stream->sink.command != NULL)
			stream->sink.command(stream->sink.context, stream->command, stream->length);
	}
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
			stream->discard_tail |= stream->fields != NULL;
			report(stream, ST_RIP3_COMMAND_RESOURCE_FAILURE);
			return;
		}
		stream->command = command;
		stream->capacity = capacity;
	}
	stream->command[stream->length++] = byte;
}

int
st_rip3_stream_create(struct st_rip3_stream **result,
    const struct st_rip3_stream_sink *sink, const struct st_rip3_environments *environments)
{
	struct st_rip3_stream *stream;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	stream = calloc(1, sizeof(*stream));
	if (stream == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (sink != NULL)
		stream->sink = *sink;
	stream->environments = environments;
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
		report(stream, ST_RIP3_COMMAND_INVALID);
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
			/* Delimiter wait measures continuation against wire bytes,
			 * including discarded suffix bytes and line endings. */
			bool tail_continues = stream->last_backslash || stream->prior_backslash;
			stream->prior_backslash = stream->last_backslash;
			stream->last_backslash = byte == '\\';
			if (line_end(byte)) {
				if (stream->discard_tail ? tail_continues
				    : stream->continued || stream->numeric_continuation)
					continue;
				if (!stream->discard_tail && stream->quoted) {
					/* Only the continuation slash is removed. The
					 * remaining even run stays quoted for later stages. */
					if (!stream->dropping)
						stream->length--;
					stream->quoted = false;
					stream->continued = true;
					continue;
				}
				stream->skip_lf = byte == '\r';
				dispatch(stream);
				stream->active = false;
				stream->line_start = true;
				continue;
			}
			stream->continued = false;
			/* Only string arguments can protect a pipe with a backslash. */
			if (byte == '|' && (!stream->quoted || stream->comment || stream->discard_tail)) {
				dispatch(stream);
				continue;
			}
			if (stream->discard_tail)
				continue;
			bool opcode = stream->length == stream->level_digits;
			if (!stream->dropping && (opcode || stream->fields != NULL)) {
				if (byte == 0x7f && (opcode || *stream->fields != '\0'
				    || stream->numeric_continuation))
					continue;
				stream->numeric_continuation = byte == '\\';
				if (stream->numeric_continuation)
					continue;
				if (!opcode) {
					if (collect_field(stream, byte))
						append(stream, byte);
					continue;
				}
				if (byte >= '1' && byte <= '9') {
					if (stream->level_digits == 9)
						reject_command(stream, ST_RIP3_COMMAND_UNSUPPORTED);
					else
						stream->level_digits++;
				}
				else if (stream->level_digits <= 1) {
					stream->comment = stream->level_digits == 0 && byte == '!';
					start_fields(stream, stream->level_digits == 0 ? 0 : stream->command[0], byte);
				}
				else
					reject_command(stream, ST_RIP3_COMMAND_UNSUPPORTED);
				append(stream, byte);
				continue;
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
		if (byte == 1 || byte == 2 || (byte == '!'
		    && (stream->line_start || (stream->sink.at_line_start != NULL
		    && stream->sink.at_line_start(stream->sink.context)))))
			stream->introducer = byte;
		else
			raw(stream, byte);
	}
}
