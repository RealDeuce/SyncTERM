/* Strict binary hexadecimal encoding and decoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_hex.h"

#include <stdint.h>

bool
st_hex_digit_value(unsigned char character, uint8_t *result)
{
	uint8_t value;

	if (result == NULL)
		return false;
	if (character >= '0' && character <= '9')
		value = (uint8_t)(character - '0');
	else if (character >= 'a' && character <= 'f')
		value = (uint8_t)(character - 'a' + 10);
	else if (character >= 'A' && character <= 'F')
		value = (uint8_t)(character - 'A' + 10);
	else
		return false;
	*result = value;
	return true;
}

bool
st_hex_encode(char *destination, size_t destination_capacity,
	const void *source, size_t source_size)
{
	static const char digits[] = "0123456789abcdef";
	const unsigned char *bytes = source;

	if (destination == NULL || source == NULL
	    || source_size > (SIZE_MAX - 1) / 2
	    || destination_capacity < source_size * 2 + 1)
		return false;
	for (size_t i = 0; i < source_size; ++i) {
		destination[i * 2] = digits[bytes[i] >> 4];
		destination[i * 2 + 1] = digits[bytes[i] & 15];
	}
	destination[source_size * 2] = '\0';
	return true;
}

bool
st_hex_decode(void *destination, size_t destination_capacity,
	const char *source, size_t source_size)
{
	unsigned char *bytes = destination;
	size_t decoded_size;

	if (destination == NULL || source == NULL || (source_size & 1U) != 0)
		return false;
	decoded_size = source_size / 2;
	if (destination_capacity < decoded_size)
		return false;
	for (size_t i = 0; i < source_size; ++i) {
		uint8_t value;

		if (!st_hex_digit_value((unsigned char)source[i], &value))
			return false;
	}
	for (size_t i = 0; i < decoded_size; ++i) {
		uint8_t high;
		uint8_t low;

		if (!st_hex_digit_value((unsigned char)source[i * 2], &high)
		    || !st_hex_digit_value((unsigned char)source[i * 2 + 1], &low))
			return false;
		bytes[i] = (unsigned char)(high * 16U + low);
	}
	return true;
}
