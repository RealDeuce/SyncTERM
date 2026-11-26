/* RIPtel RAND conversion, arithmetic and per-session CRT generator. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_host_internal.h"

static int64_t
signed_value(uint32_t value)
{
	return value <= INT32_MAX ? (int64_t)value : (int64_t)value - INT64_C(4294967296);
}

static int
decimal(const struct st_rip3_builtin_parameter *parameter, uint32_t *result)
{
	if (parameter->length == 0 || parameter->length > 20 || parameter->text[parameter->length] != 0)
		return IO_ERROR_INVALID_FORMAT;
	uint32_t value = 0;
	for (size_t i = 0; i < parameter->length; i++) {
		unsigned char ch = (unsigned char)parameter->text[i];
		if (ch < '0' || ch > '9')
			return IO_ERROR_INVALID_FORMAT;
		/* The callback's decimal conversion wraps, including twenty-digit
		 * tokens exceeding uint64_t. Whitespace/signs are not decimal digits. */
		value = value * UINT32_C(10) + (uint32_t)(ch - '0');
	}
	*result = value;
	return 0;
}

int
st_rip3_random_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (strcmp(reference->name, "RAND") != 0)
		return IO_ERROR_NOT_SUPPORTED;
	if (reference->count == 0 || reference->count > 3)
		return IO_ERROR_INVALID_FORMAT;
	uint32_t maximum, width = 0, minimum = 0;
	int      status = decimal(&reference->parameters[0], &maximum);
	if (status == 0 && reference->count >= 2)
		status = decimal(&reference->parameters[1], &width);
	if (status == 0 && reference->count == 3)
		status = decimal(&reference->parameters[2], &minimum);
	if (status != 0)
		return status;
	if (maximum < 1 || maximum > 32767 || signed_value(width) > 5 || signed_value(minimum) >= (int64_t)maximum)
		return IO_ERROR_INVALID_FORMAT;
	if (host->random == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	/* Preview the next draw. A local output-capacity failure cannot consume
	 * generator state or publish a partially formatted replacement. */
	uint32_t next    = host->random->state * UINT32_C(214013) + UINT32_C(2531011);
	uint32_t draw    = (next >> 16) & UINT32_C(32767);
	uint32_t span    = maximum - minimum + UINT32_C(1);
	uint32_t product = span * draw;
	/* Original IMUL wraps to 32 bits before signed IDIV; addition wraps too.
	 * Decode signed representations explicitly instead of overflowing signed C
	 * arithmetic or relying on an out-of-range unsigned-to-signed cast. */
	int64_t  quotient = signed_value(product) / 32767;
	uint32_t value    = minimum + (uint32_t)quotient;
	char     number[16];
	int      count = snprintf(number, sizeof(number), "%" PRId64, signed_value(value));
	if (count < 0 || (size_t)count >= sizeof(number))
		return IO_ERROR_IO;
	size_t  length = (size_t)count, zeros = 0;
	int64_t requested = signed_value(width);
	if (requested > (int64_t)length)
		zeros = (size_t)requested - length;
	if (zeros + length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	/* RIPtel prepends zero bytes to the formatted number, including before
	 * a minus sign. printf's numeric zero-padding would produce other bytes. */
	memset(result, '0', zeros);
	memcpy(result + zeros, number, length + 1);
	host->random->state = next;
	return 0;
}
