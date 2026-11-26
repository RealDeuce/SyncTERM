/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "io_error.h"
#include "st_rip3_number.h"

static int
digit_value(unsigned char byte, unsigned radix)
{
	if (byte >= '0' && byte <= '9')
		return byte - '0';
	if (byte >= 'A' && byte <= 'Z')
		return byte - 'A' + 10;
	if (byte >= 'a' && byte <= 'z')
		return byte - 'a' + (radix == 36 ? 10 : 36);
	if (radix == 64 && byte == '#')
		return 62;
	if (radix == 64 && byte == '&')
		return 63;
	return -1;
}

int
st_rip3_number_decode(uint64_t *result, const void *bytes, size_t size, unsigned radix)
{
	const unsigned char *input = bytes;
	uint64_t             value = 0;

	if (result == NULL || bytes == NULL || size == 0 || (radix != 36 && radix != 64))
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < size; ++i) {
		int digit = digit_value(input[i], radix);
		if (digit < 0)
			return IO_ERROR_INVALID_FORMAT;
		if (value > (UINT64_MAX - (unsigned)digit) / radix)
			return IO_ERROR_OUT_OF_RANGE;
		value = value * radix + (unsigned)digit;
	}
	*result = value;
	return 0;
}

int
st_rip3_number_encode(char *output, size_t capacity, uint64_t value, size_t width, unsigned radix)
{
	static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
	uint64_t          remaining  = value;
	size_t            digits     = 1;

	if (output == NULL || width == 0 || (radix != 36 && radix != 64))
		return IO_ERROR_INVALID_ARGUMENT;
	while (remaining >= radix) {
		remaining /= radix;
		++digits;
	}
	if (width > capacity || digits > width)
		return IO_ERROR_OUT_OF_RANGE;
	memset(output, '0', width);
	do {
		output[--width] = alphabet[value % radix];
		value /= radix;
	} while (value != 0);
	return 0;
}
