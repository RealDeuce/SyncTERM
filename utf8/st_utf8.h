/* Strict, locale-independent Unicode scalar encoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_UTF8_H_
#define ST_UTF8_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool st_unicode_scalar_valid(uint32_t value);

/* Outputs are unchanged on failure. */
int st_utf8_decode_one(uint32_t *result, size_t *consumed,
	const void *data, size_t size);

/*
 * Returns the required byte count or a negative IO_ERROR_* value.  A NULL
 * result is permitted only when result_size is zero.  An undersized buffer is
 * unchanged.
 */
ptrdiff_t st_utf8_encode_one(char *result, size_t result_size,
	uint32_t value);

#endif
