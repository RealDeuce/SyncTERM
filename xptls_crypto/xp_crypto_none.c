/* Built-in digest support when no external cryptographic provider is used. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "xp_cipher.h"
#include "xp_kdf.h"
#include "xp_crypto.h"

#include <stdint.h>
#include <string.h>

struct sha256_context {
	uint32_t state[8];
	uint64_t byte_count;
	uint8_t buffer[64];
};

static const uint32_t sha256_constants[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t
rotate_right(uint32_t value, unsigned count)
{
	return (value >> count) | (value << (32 - count));
}

static void
sha256_transform(struct sha256_context *context, const uint8_t block[64])
{
	uint32_t words[64];
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint32_t e;
	uint32_t f;
	uint32_t g;
	uint32_t h;

	for (unsigned i = 0; i < 16; ++i) {
		words[i] = ((uint32_t)block[i * 4] << 24)
		    | ((uint32_t)block[i * 4 + 1] << 16)
		    | ((uint32_t)block[i * 4 + 2] << 8)
		    | (uint32_t)block[i * 4 + 3];
	}
	for (unsigned i = 16; i < 64; ++i) {
		uint32_t s0 = rotate_right(words[i - 15], 7)
		    ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
		uint32_t s1 = rotate_right(words[i - 2], 17)
		    ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);

		words[i] = words[i - 16] + s0 + words[i - 7] + s1;
	}
	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	f = context->state[5];
	g = context->state[6];
	h = context->state[7];
	for (unsigned i = 0; i < 64; ++i) {
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13)
		    ^ rotate_right(a, 22);
		uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11)
		    ^ rotate_right(e, 25);
		uint32_t first = h + sum1 + choice + sha256_constants[i] + words[i];
		uint32_t second = sum0 + majority;

		h = g;
		g = f;
		f = e;
		e = d + first;
		d = c;
		c = b;
		b = a;
		a = first + second;
	}
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;
}

static void
sha256_init(struct sha256_context *context)
{
	*context = (struct sha256_context){
		.state = {
			0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
		},
	};
}

static void
sha256_update(struct sha256_context *context, const uint8_t *input,
	size_t input_size)
{
	size_t offset = (size_t)(context->byte_count % sizeof(context->buffer));

	context->byte_count += input_size;
	if (offset != 0) {
		size_t available = sizeof(context->buffer) - offset;

		if (input_size < available) {
			memcpy(context->buffer + offset, input, input_size);
			return;
		}
		memcpy(context->buffer + offset, input, available);
		sha256_transform(context, context->buffer);
		input += available;
		input_size -= available;
	}
	while (input_size >= sizeof(context->buffer)) {
		sha256_transform(context, input);
		input += sizeof(context->buffer);
		input_size -= sizeof(context->buffer);
	}
	if (input_size != 0)
		memcpy(context->buffer, input, input_size);
}

static void
sha256_final(struct sha256_context *context, uint8_t result[32])
{
	uint64_t bit_count = context->byte_count * 8;
	size_t offset = (size_t)(context->byte_count % sizeof(context->buffer));

	context->buffer[offset++] = 0x80;
	if (offset > sizeof(context->buffer) - sizeof(bit_count)) {
		memset(context->buffer + offset, 0,
		    sizeof(context->buffer) - offset);
		sha256_transform(context, context->buffer);
		offset = 0;
	}
	memset(context->buffer + offset, 0,
	    sizeof(context->buffer) - sizeof(bit_count) - offset);
	for (unsigned i = 0; i < sizeof(bit_count); ++i)
		context->buffer[sizeof(context->buffer) - sizeof(bit_count) + i] =
		    (uint8_t)(bit_count >> (56 - i * 8));
	sha256_transform(context, context->buffer);
	for (unsigned i = 0; i < 8; ++i) {
		result[i * 4] = (uint8_t)(context->state[i] >> 24);
		result[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
		result[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
		result[i * 4 + 3] = (uint8_t)context->state[i];
	}
	xp_crypto_clear(context, sizeof(*context));
}

const char *xp_crypto_provider_name(void) { return "none"; }
const char *xp_crypto_provider_version(void) { return ""; }

int
xp_crypto_random(void *result, size_t size)
{
	return result == NULL && size != 0
	    ? XP_CRYPTO_ERR_INVALID : XP_CRYPTO_ERR_DISABLED;
}

bool
xp_cipher_supported(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction)
{
	(void)algorithm;
	(void)direction;
	return false;
}

int
xp_cipher_crypt(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction, const void *key, size_t key_size,
	const void *iv, size_t iv_size, uint32_t initial_counter,
	const void *input, size_t input_size, void *output, size_t output_size)
{
	(void)algorithm;
	(void)direction;
	(void)key;
	(void)key_size;
	(void)iv;
	(void)iv_size;
	(void)initial_counter;
	(void)input;
	(void)input_size;
	(void)output;
	(void)output_size;
	return XP_CRYPTO_ERR_DISABLED;
}

int
xp_digest(enum xp_digest_algorithm digest, const void *input,
	size_t input_size, void *result, size_t result_size)
{
	struct sha256_context context;

	if ((input == NULL && input_size != 0) || result == NULL
	    || result_size != xp_digest_size(digest))
		return XP_CRYPTO_ERR_INVALID;
	if (digest != XP_DIGEST_SHA256)
		return XP_CRYPTO_ERR_DISABLED;
	sha256_init(&context);
	sha256_update(&context, input, input_size);
	sha256_final(&context, result);
	return XP_CRYPTO_OK;
}

size_t
xp_digest_size(enum xp_digest_algorithm digest)
{
	return digest == XP_DIGEST_MD5 ? 16
	    : digest == XP_DIGEST_SHA1 ? 20
	    : digest == XP_DIGEST_SHA256 ? 32
	    : digest == XP_DIGEST_SHA384 ? 48
	    : digest == XP_DIGEST_SHA512 ? 64 : 0;
}

int
xp_kdf_pbkdf2(enum xp_digest_algorithm digest,
	const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t iterations,
	void *result, size_t result_size)
{
	(void)digest;
	(void)password;
	(void)password_size;
	(void)salt;
	(void)salt_size;
	(void)iterations;
	(void)result;
	(void)result_size;
	return XP_CRYPTO_ERR_DISABLED;
}

int
xp_kdf_scrypt(const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t n, uint32_t r, uint32_t p,
	void *result, size_t result_size)
{
	(void)password;
	(void)password_size;
	(void)salt;
	(void)salt_size;
	(void)n;
	(void)r;
	(void)p;
	(void)result;
	(void)result_size;
	return XP_CRYPTO_ERR_DISABLED;
}
