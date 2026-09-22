/* Strict RFC 4648 Base64 encoding and decoding. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_BASE64_H_
#define ST_BASE64_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The encoded size excludes the terminating NUL. */
bool st_base64_encoded_size(size_t source_size, size_t *result);
bool st_base64_decoded_size(const char *source, size_t source_size,
	size_t *result);

/* Encoding appends a NUL; decoding writes exactly the validated byte count. */
bool st_base64_encode(char *destination, size_t destination_capacity,
	const void *source, size_t source_size);
bool st_base64_decode(void *destination, size_t destination_capacity,
	const char *source, size_t source_size);

#ifdef __cplusplus
}
#endif

#endif
