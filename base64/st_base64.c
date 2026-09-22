/* Strict RFC 4648 Base64 encoding and decoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_base64.h"

#include <stdint.h>

static const char alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool
st_base64_encoded_size(size_t source_size, size_t *result)
{
	if (result == NULL || source_size > (SIZE_MAX - 2) / 3)
		return false;
	*result = ((source_size + 2) / 3) * 4;
	return true;
}

static bool
value(unsigned char character, uint8_t *result)
{
	if (character >= 'A' && character <= 'Z')
		*result = (uint8_t)(character - 'A');
	else if (character >= 'a' && character <= 'z')
		*result = (uint8_t)(character - 'a' + 26);
	else if (character >= '0' && character <= '9')
		*result = (uint8_t)(character - '0' + 52);
	else if (character == '+')
		*result = 62;
	else if (character == '/')
		*result = 63;
	else
		return false;
	return true;
}

bool
st_base64_decoded_size(const char *source, size_t source_size, size_t *result)
{
	size_t padding = 0;
	uint8_t ignored;

	if (result == NULL || (source == NULL && source_size != 0)
	    || source_size % 4 != 0)
		return false;
	if (source_size != 0 && source[source_size - 1] == '=')
		padding++;
	if (source_size > 1 && source[source_size - 2] == '=')
		padding++;
	for (size_t i = 0; i < source_size - padding; ++i) {
		if (!value((unsigned char)source[i], &ignored))
			return false;
	}
	for (size_t i = source_size - padding; i < source_size; ++i) {
		if (source[i] != '=')
			return false;
	}
	if (padding > 2)
		return false;
	if (source_size != 0) {
		uint8_t tail;

		if (!value((unsigned char)source[source_size - padding - 1], &tail))
			return false;
		if ((padding == 1 && (tail & 0x03U) != 0)
		    || (padding == 2 && (tail & 0x0fU) != 0))
			return false;
	}
	*result = source_size / 4 * 3 - padding;
	return true;
}

bool
st_base64_encode(char *destination, size_t destination_capacity,
	const void *source, size_t source_size)
{
	const uint8_t *input = source;
	size_t encoded;
	size_t output = 0;

	if (destination == NULL || (source == NULL && source_size != 0)
	    || !st_base64_encoded_size(source_size, &encoded)
	    || encoded == SIZE_MAX || destination_capacity <= encoded)
		return false;
	for (size_t i = 0; i < source_size; i += 3) {
		uint32_t block = (uint32_t)input[i] << 16;
		size_t remaining = source_size - i;

		if (remaining > 1)
			block |= (uint32_t)input[i + 1] << 8;
		if (remaining > 2)
			block |= input[i + 2];
		destination[output++] = alphabet[(block >> 18) & 0x3fU];
		destination[output++] = alphabet[(block >> 12) & 0x3fU];
		destination[output++] = remaining > 1
		    ? alphabet[(block >> 6) & 0x3fU] : '=';
		destination[output++] = remaining > 2
		    ? alphabet[block & 0x3fU] : '=';
	}
	destination[output] = '\0';
	return true;
}

bool
st_base64_decode(void *destination, size_t destination_capacity,
	const char *source, size_t source_size)
{
	uint8_t *output = destination;
	size_t decoded;
	size_t position = 0;

	if ((destination == NULL && destination_capacity != 0)
	    || !st_base64_decoded_size(source, source_size, &decoded)
	    || destination_capacity < decoded)
		return false;
	for (size_t i = 0; i < source_size; i += 4) {
		uint8_t a, b, c = 0, d = 0;
		uint32_t block;

		(void)value((unsigned char)source[i], &a);
		(void)value((unsigned char)source[i + 1], &b);
		if (source[i + 2] != '=')
			(void)value((unsigned char)source[i + 2], &c);
		if (source[i + 3] != '=')
			(void)value((unsigned char)source[i + 3], &d);
		block = ((uint32_t)a << 18) | ((uint32_t)b << 12)
		    | ((uint32_t)c << 6) | d;
		if (position < decoded)
			output[position++] = (uint8_t)(block >> 16);
		if (position < decoded)
			output[position++] = (uint8_t)(block >> 8);
		if (position < decoded)
			output[position++] = (uint8_t)block;
	}
	return true;
}
