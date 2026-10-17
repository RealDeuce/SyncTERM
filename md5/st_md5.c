/* Derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm.
 *
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

#include "st_md5.h"

#include <string.h>

static void
transform(struct st_md5 *context)
{
	static const uint32_t constants[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
		0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
		0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
	};
	static const unsigned shifts[4][4] = {
		{7,12,17,22}, {5,9,14,20}, {4,11,16,23}, {6,10,15,21},
	};
	uint32_t words[16];
	uint32_t a = context->state[0], b = context->state[1];
	uint32_t c = context->state[2], d = context->state[3];

	for (unsigned i = 0; i < 16; ++i) {
		const uint8_t *p = context->block + i * 4;
		words[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8
		    | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	}
	for (unsigned i = 0; i < 64; ++i) {
		uint32_t f, next;
		unsigned word, shift = shifts[i / 16][i % 4];

		if (i < 16) { f = (b & c) | (~b & d); word = i; }
		else if (i < 32) { f = (d & b) | (~d & c); word = (5 * i + 1) % 16; }
		else if (i < 48) { f = b ^ c ^ d; word = (3 * i + 5) % 16; }
		else { f = c ^ (b | ~d); word = (7 * i) % 16; }
		next = a + f + constants[i] + words[word];
		a = d;
		d = c;
		c = b;
		b += (next << shift) | (next >> (32 - shift));
	}
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
}

void
st_md5_init(struct st_md5 *context)
{
	*context = (struct st_md5){.state = {
		0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
	}};
}

void
st_md5_update(struct st_md5 *context, const void *data, size_t size)
{
	const uint8_t *bytes = data;
	size_t used = (size_t)(context->bytes % 64);

	context->bytes += size;
	while (size != 0) {
		size_t count = size < 64 - used ? size : 64 - used;

		memcpy(context->block + used, bytes, count);
		used += count;
		bytes += count;
		size -= count;
		if (used == 64) {
			transform(context);
			used = 0;
		}
	}
}

void
st_md5_finish(struct st_md5 *context, uint8_t digest[16])
{
	static const uint8_t padding[64] = {0x80};
	uint64_t bits = context->bytes * 8;
	uint8_t length[8];
	size_t used = (size_t)(context->bytes % 64);

	for (unsigned i = 0; i < 8; ++i)
		length[i] = (uint8_t)(bits >> (i * 8));
	st_md5_update(context, padding, used < 56 ? 56 - used : 120 - used);
	st_md5_update(context, length, sizeof(length));
	for (unsigned i = 0; i < 16; ++i)
		digest[i] = (uint8_t)(context->state[i / 4] >> ((i % 4) * 8));
	memset(context, 0, sizeof(*context));
}
