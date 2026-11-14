/* RIPterm descriptor-driven base-36 fields. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_rip_args.h"

int
cterm_rip_signed_byte(uint8_t byte)
{
	return byte < 128u ? byte : (int)byte - 256;
}

int
cterm_rip_signed_word(uint16_t word)
{
	return word < 32768u ? (int)word : (int)((int32_t)word - 65536);
}

bool
cterm_rip_read_number(const char **cursor, size_t width, int *value)
{
	const char *text = *cursor;
	uint16_t magnitude = 0;
	bool negative = false, stopped = false;
	size_t used = 0;

	if (width == 0 || width > 5)
		return false;
	for (; used < width; ++used) {
		unsigned char ch = (unsigned char)text[used];
		unsigned digit;

		if (used != 0 && (ch == '\0' || ch == '|'))
			break;
		if (ch == '-') {
			if (used == 0)
				negative = true;
			else
				stopped = true;
			continue;
		}
		if (ch >= '0' && ch <= '9')
			digit = ch - '0';
		else if (ch >= 'A' && ch <= 'Z')
			digit = ch - 'A' + 10;
		else if (ch >= 'a' && ch <= 'z')
			digit = ch - 'a' + 10;
		else
			return false;
		/* Validate the whole field even after strtol would stop at '-'.
		 * Unsigned reduction models AX without signed host overflow. */
		if (!stopped)
			magnitude = (uint16_t)((uint32_t)magnitude * 36u + digit);
	}
	uint16_t word = negative ? (uint16_t)(0u - magnitude) : magnitude;
	*value = cterm_rip_signed_word(word);
	*cursor = text + used;
	return true;
}

size_t
cterm_rip_read_fields(const char **cursor, const uint8_t *widths,
    size_t count, int *values)
{
	size_t parsed = 0;
	while (parsed < count
	    && cterm_rip_read_number(cursor, widths[parsed], &values[parsed]))
		++parsed;
	return parsed;
}
