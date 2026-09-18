/* Strict, locale-independent Unicode scalar encoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_utf8.h"

#include "io_error.h"

#include <string.h>

bool
st_unicode_scalar_valid(uint32_t value)
{
	return value <= UINT32_C(0x10ffff)
	    && !(value >= UINT32_C(0xd800) && value <= UINT32_C(0xdfff));
}

int
st_utf8_decode_one(uint32_t *result, size_t *consumed,
	const void *data, size_t size)
{
	const unsigned char *text = data;
	uint32_t value;
	size_t length;
	uint32_t minimum;

	if (result == NULL || consumed == NULL || (data == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (size == 0)
		return IO_ERROR_INVALID_ENCODING;
	if (text[0] < 0x80) {
		*result = text[0];
		*consumed = 1;
		return 0;
	}
	if (text[0] >= 0xc2 && text[0] <= 0xdf) {
		value = text[0] & 0x1f;
		minimum = 0x80;
		length = 2;
	}
	else if (text[0] >= 0xe0 && text[0] <= 0xef) {
		value = text[0] & 0x0f;
		minimum = 0x800;
		length = 3;
	}
	else if (text[0] >= 0xf0 && text[0] <= 0xf4) {
		value = text[0] & 0x07;
		minimum = 0x10000;
		length = 4;
	}
	else
		return IO_ERROR_INVALID_ENCODING;
	if (size < length)
		return IO_ERROR_INVALID_ENCODING;
	for (size_t i = 1; i < length; ++i) {
		if ((text[i] & 0xc0) != 0x80)
			return IO_ERROR_INVALID_ENCODING;
		value = (value << 6) | (text[i] & 0x3f);
	}
	if (value < minimum || !st_unicode_scalar_valid(value))
		return IO_ERROR_INVALID_ENCODING;
	*result = value;
	*consumed = length;
	return 0;
}

ptrdiff_t
st_utf8_encode_one(char *result, size_t result_size, uint32_t value)
{
	unsigned char encoded[4];
	size_t length;

	if ((result == NULL && result_size != 0) || !st_unicode_scalar_valid(value))
		return IO_ERROR_INVALID_ARGUMENT;
	if (value < 0x80) {
		encoded[0] = (unsigned char)value;
		length = 1;
	}
	else if (value < 0x800) {
		encoded[0] = (unsigned char)(0xc0 | value >> 6);
		encoded[1] = (unsigned char)(0x80 | (value & 0x3f));
		length = 2;
	}
	else if (value < 0x10000) {
		encoded[0] = (unsigned char)(0xe0 | value >> 12);
		encoded[1] = (unsigned char)(0x80 | ((value >> 6) & 0x3f));
		encoded[2] = (unsigned char)(0x80 | (value & 0x3f));
		length = 3;
	}
	else {
		encoded[0] = (unsigned char)(0xf0 | value >> 18);
		encoded[1] = (unsigned char)(0x80 | ((value >> 12) & 0x3f));
		encoded[2] = (unsigned char)(0x80 | ((value >> 6) & 0x3f));
		encoded[3] = (unsigned char)(0x80 | (value & 0x3f));
		length = 4;
	}
	if (result != NULL && result_size >= length)
		memcpy(result, encoded, length);
	return (ptrdiff_t)length;
}
