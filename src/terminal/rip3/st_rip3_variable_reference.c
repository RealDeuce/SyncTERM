/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_variable_reference.h"
#include "st_rip3_graphical_bytes.h"

#include "io_error.h"

#include <stdio.h>
#include <string.h>

struct parser {
	const uint8_t *text;
	size_t at, end;
};

static uint8_t
upper(uint8_t ch)
{
	return ch >= 'a' && ch <= 'z' ? (uint8_t)(ch - 'a' + 'A') : ch;
}

static bool
letter(uint8_t ch)
{
	return upper(ch) >= 'A' && upper(ch) <= 'Z';
}

static bool
digit(uint8_t ch)
{
	return ch >= '0' && ch <= '9';
}

static uint8_t
peek(const struct parser *parser)
{
	return parser->at < parser->end ? parser->text[parser->at] : 0;
}

static unsigned
directive(uint8_t ch)
{
	switch (ch) {
	case '*': return ST_RIP3_VARIABLE_REQUIRED;
	case '+': return ST_RIP3_VARIABLE_PERSISTENT;
	case '=': return ST_RIP3_VARIABLE_SESSION;
	case '#': return ST_RIP3_VARIABLE_MASKED;
	case '-': return ST_RIP3_VARIABLE_SET;
	case '&': return ST_RIP3_VARIABLE_GET;
	default: return 0;
	}
}

static int
decimal(struct parser *parser, uint32_t limit, uint32_t *result)
{
	uint32_t value = 0;

	if (!digit(peek(parser)))
		return IO_ERROR_INVALID_FORMAT;
	while (digit(peek(parser))) {
		unsigned add = peek(parser) - '0';

		if (value > limit / 10 || (value == limit / 10 && add > limit % 10))
			return IO_ERROR_OUT_OF_RANGE;
		value = value * 10 + add;
		parser->at++;
	}
	*result = value;
	return 0;
}

static int
coordinate(struct parser *parser, int32_t *result, bool *present)
{
	uint32_t value;
	int status;

	if (!digit(peek(parser)))
		return 0;
	status = decimal(parser, UINT16_MAX, &value);
	if (status != 0)
		return status;
	*result = (int32_t)value;
	*present = true;
	return 0;
}

static bool
word(const uint8_t *text, size_t length, const char *expected)
{
	if (length != strlen(expected))
		return false;
	for (size_t i = 0; i < length; i++) {
		if (upper(text[i]) != (uint8_t)expected[i])
			return false;
	}
	return true;
}

static int
options(struct parser *parser, struct st_rip3_variable_reference *reference)
{
	bool mode_seen = false, conversion_seen = false;

	parser->at++; /* '(' */
	if (peek(parser) == ')') {
		parser->at++;
		return 0;
	}
	for (unsigned option = 0; option < 2; option++) {
		size_t start = parser->at, length;
		int mode = -1, conversion = -1;

		while (letter(peek(parser)))
			parser->at++;
		length = parser->at - start;
		if (word(parser->text + start, length, "ANY")) mode = ST_RIP3_VARIABLE_ANY;
		if (word(parser->text + start, length, "ALPHA")) mode = ST_RIP3_VARIABLE_ALPHA;
		if (word(parser->text + start, length, "NUMBER")) mode = ST_RIP3_VARIABLE_NUMBER;
		if (word(parser->text + start, length, "ALPHANUM")) mode = ST_RIP3_VARIABLE_ALPHANUM;
		if (word(parser->text + start, length, "TONAME")) conversion = ST_RIP3_VARIABLE_TONAME;
		if (word(parser->text + start, length, "TOUPPER")) conversion = ST_RIP3_VARIABLE_TOUPPER;
		if (word(parser->text + start, length, "TOLOWER")) conversion = ST_RIP3_VARIABLE_TOLOWER;
		if (mode >= 0 && !mode_seen && !conversion_seen) {
			reference->mode = (enum st_rip3_variable_mode)mode;
			mode_seen = true;
		}
		else if (conversion >= 0 && !conversion_seen) {
			reference->conversion = (enum st_rip3_variable_conversion)conversion;
			conversion_seen = true;
		}
		else
			return IO_ERROR_INVALID_FORMAT;
		if (peek(parser) == ')') {
			parser->at++;
			return 0;
		}
		if (peek(parser) != ',')
			return IO_ERROR_INVALID_FORMAT;
		parser->at++;
	}
	return IO_ERROR_INVALID_FORMAT;
}

static void
prompt_text(struct st_rip3_variable_prompt_text *result,
    const uint8_t *text, size_t length)
{
	result->length = length > 100 ? 100 : length;
	memcpy(result->bytes, text, result->length);
}

static void
definition_text(struct st_rip3_variable_prompt_text *result, const uint8_t *text,
    size_t length, size_t budget, bool utf8, bool converted)
{
	for (size_t offset = 0; offset < length;) {
		if (text[offset] == '\\' && offset + 1 < length && text[offset + 1] == '|')
			offset++;
		size_t size = utf8 ? st_rip3_graphical_character(text + offset, length - offset) : 1;
		size_t units = converted ? 1 : size;
		if (units > budget)
			break;
		memcpy(result->bytes + result->length, text + offset, size);
		result->length += size;
		offset += size;
		budget -= units;
	}
}

int
st_rip3_variable_definition_parse(struct st_rip3_variable_reference *result,
    const uint8_t *text, size_t length, bool utf8, bool converted)
{
	struct st_rip3_variable_reference definition = {.has_width = true, .width = 60, .has_question = true};
	const uint8_t *colon = memchr(text, ':', length);
	if (colon == NULL || colon == text + length - 1)
		return IO_ERROR_INVALID_FORMAT;
	const uint8_t *name_end = memchr(text, ',', (size_t)(colon - text));
	if (name_end == NULL)
		name_end = colon;
	size_t name_length = (size_t)(name_end - text);
	if (name_length == 0 || name_length >= 20 || !letter(text[0]))
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 1; i < name_length; i++) {
		if (!letter(text[i]) && !digit(text[i]) && text[i] != '_')
			return IO_ERROR_INVALID_FORMAT;
	}
	memcpy(definition.name, text, name_length);
	if (name_end != colon) {
		const uint8_t *digits = name_end + 1;
		if (digits == colon || !digit(*digits))
			return IO_ERROR_INVALID_FORMAT;
		uint16_t width = 0;
		/* The definition parser consumes the decimal prefix as a WORD. */
		for (; digits != colon && digit(*digits); digits++)
			width = (uint16_t)(width * 10u + (*digits - '0'));
		if (width >= 1 && width <= 60)
			definition.width = width;
	}
	const uint8_t *end = text + length;
	const uint8_t *value = colon + 1;
	if (*value == '?') {
		const uint8_t *question = ++value;
		const uint8_t *close = memchr(question, '?', (size_t)(end - question));
		definition_text(&definition.question, question, (size_t)((close == NULL ? end : close) - question),
		    60, utf8, converted);
		value = close == NULL ? end : close + 1;
	}
	if (definition.question.length == 0) {
		int size = snprintf((char *)definition.question.bytes, sizeof(definition.question.bytes),
		    "Please Enter Information for \"%s\"", definition.name);
		if (size < 0 || (size_t)size >= sizeof(definition.question.bytes))
			return IO_ERROR_OUT_OF_RANGE;
		definition.question.length = (size_t)size;
	}
	else
		definition.question.bytes[definition.question.length++] = '?';
	definition_text(&definition.default_value, value, (size_t)(end - value), definition.width - 1, utf8, converted);
	definition.has_default = definition.default_value.length != 0;
	*result = definition;
	return 0;
}

int
st_rip3_variable_reference_parse(struct st_rip3_variable_reference *result,
    const uint8_t *text, size_t length)
{
	struct st_rip3_variable_reference reference = {0};
	struct parser parser = {text, 1, length == 0 ? 0 : length - 1};
	size_t name_length = 0;
	unsigned flag;
	int status;

	if (result == NULL || text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length < 3 || text[0] != '$' || text[length - 1] != '$')
		return IO_ERROR_INVALID_FORMAT;
	/* Delimiters are forbidden even beyond the retained 100-byte payload. */
	for (size_t i = 1; i < parser.end; i++) {
		if (text[i] == '$')
			return IO_ERROR_INVALID_FORMAT;
	}
	while ((flag = directive(peek(&parser))) != 0) {
		reference.directives |= flag;
		parser.at++;
	}
	if (peek(&parser) == ',' || digit(peek(&parser))) {
		reference.has_position = true;
		status = coordinate(&parser, &reference.x, &reference.has_x);
		if (status != 0)
			return status;
		if (peek(&parser) != ',')
			return IO_ERROR_INVALID_FORMAT;
		parser.at++;
		status = coordinate(&parser, &reference.y, &reference.has_y);
		if (status != 0)
			return status;
		if (peek(&parser) != ':')
			return IO_ERROR_INVALID_FORMAT;
		parser.at++;
	}
	if (!letter(peek(&parser)))
		return IO_ERROR_INVALID_FORMAT;
	do {
		if (name_length == 20)
			return IO_ERROR_OUT_OF_RANGE;
		reference.name[name_length++] = (char)upper(peek(&parser));
		parser.at++;
	} while (letter(peek(&parser)) || digit(peek(&parser)) || peek(&parser) == '_');
	if (peek(&parser) == '(') {
		status = options(&parser, &reference);
		if (status != 0)
			return status;
	}
	if (peek(&parser) == ',') {
		parser.at++;
		status = decimal(&parser, UINT32_MAX, &reference.width);
		if (status != 0)
			return status;
		reference.has_width = true;
	}
	if (peek(&parser) == '@') {
		size_t start = ++parser.at;

		while (parser.at < parser.end && peek(&parser) != '=')
			parser.at++;
		reference.has_question = true;
		prompt_text(&reference.question, text + start, parser.at - start);
	}
	if (peek(&parser) == '=') {
		reference.has_default = true;
		parser.at++;
		prompt_text(&reference.default_value, text + parser.at, parser.end - parser.at);
		parser.at = parser.end;
	}
	if (parser.at != parser.end)
		return IO_ERROR_INVALID_FORMAT;
	*result = reference;
	return 0;
}
