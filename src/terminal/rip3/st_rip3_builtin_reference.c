/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_builtin_reference.h"

#include "io_error.h"

#include <stdbool.h>
#include <string.h>

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

int
st_rip3_builtin_reference_parse(struct st_rip3_builtin_reference *result,
    const uint8_t *text, size_t length)
{
	struct st_rip3_builtin_reference reference = {0};
	size_t at = 1, name_length = 0, end;

	if (result == NULL || text == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length < 3 || text[0] != '$' || text[length - 1] != '$')
		return IO_ERROR_INVALID_FORMAT;
	end = length - 1;
	if (!letter(text[at]))
		return IO_ERROR_INVALID_FORMAT;
	while (at < end && (letter(text[at]) || (text[at] >= '0' && text[at] <= '9')
	    || text[at] == '_')) {
		if (name_length == 20)
			return IO_ERROR_OUT_OF_RANGE;
		reference.name[name_length++] = (char)upper(text[at++]);
	}
	if (at == end) {
		*result = reference;
		return 0;
	}
	if (text[at] != '(' || text[end - 1] != ')')
		return IO_ERROR_INVALID_FORMAT;
	at++;
	end--; /* Closing parenthesis. */
	{
		size_t nonspace = at;
		while (nonspace < end && text[nonspace] == ' ')
			nonspace++;
		if (nonspace == end) {
			*result = reference;
			return 0;
		}
	}
	for (;;) {
		size_t first = at, stop;
		struct st_rip3_builtin_parameter *parameter;

		if (reference.count == 40)
			return IO_ERROR_OUT_OF_RANGE;
		while (at < end && text[at] != ',') {
			if (text[at] == 0 || text[at] == '$' || text[at] == '(' || text[at] == ')')
				return IO_ERROR_INVALID_FORMAT;
			at++;
		}
		stop = at;
		while (first < stop && text[first] == ' ')
			first++;
		while (stop > first && text[stop - 1] == ' ')
			stop--;
		if (stop - first > 12)
			return IO_ERROR_OUT_OF_RANGE;
		parameter = &reference.parameters[reference.count++];
		parameter->length = stop - first;
		memcpy(parameter->text, text + first, parameter->length);
		if (at == end)
			break;
		at++; /* Comma; a final comma leaves an explicit empty parameter. */
	}
	*result = reference;
	return 0;
}
