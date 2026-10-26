/* Strict binary hexadecimal encoding and decoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_HEX_H_
#define ST_HEX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Writes two lowercase hexadecimal digits per source byte followed by a NUL.
 * Destination capacity must be at least source_size * 2 + 1. Source and
 * destination must not overlap. The destination is unchanged on failure.
 */
bool st_hex_encode(char *destination, size_t destination_capacity,
	const void *source, size_t source_size);

/*
 * Decodes exactly source_size / 2 bytes. Uppercase and lowercase digits are
 * accepted. Odd-length and non-hexadecimal input is rejected. Source and
 * destination must not overlap. The destination is unchanged on failure.
 */
bool st_hex_decode(void *destination, size_t destination_capacity,
	const char *source, size_t source_size);

/* Converts one US-ASCII hexadecimal digit without consulting the locale. */
bool st_hex_digit_value(unsigned char character, uint8_t *result);

#ifdef __cplusplus
}
#endif

#endif
