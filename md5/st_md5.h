/* Streaming MD5 for protocol-required file checksums, not authentication. */
/* Derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm;
 * see LICENSE in this directory for the retained upstream notice. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef ST_MD5_H_
#define ST_MD5_H_

#include <stddef.h>
#include <stdint.h>

struct st_md5 {
	uint32_t state[4];
	uint64_t bytes;
	uint8_t block[64];
};

void st_md5_init(struct st_md5 *context);
/* data may be NULL only when size is zero. Counts wrap modulo 2^64 bytes. */
void st_md5_update(struct st_md5 *context, const void *data, size_t size);
/* Finishes and consumes context, writing exactly 16 digest bytes. */
void st_md5_finish(struct st_md5 *context, uint8_t digest[16]);

#endif
