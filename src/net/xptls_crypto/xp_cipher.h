/* Whole-buffer symmetric cipher operations needed by encrypted INI files. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef XP_CIPHER_H_
#define XP_CIPHER_H_

#include "xp_crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum xp_cipher_algorithm {
	XP_CIPHER_AES = 1,
	XP_CIPHER_CHACHA20,
	XP_CIPHER_3DES,
	XP_CIPHER_CAST128,
	XP_CIPHER_RC4,
};

enum xp_cipher_direction {
	XP_CIPHER_ENCRYPT = 1,
	XP_CIPHER_DECRYPT,
};

bool xp_cipher_supported(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction);
/*
 * Process one complete buffer without padding. input and output may be the
 * same buffer, and output_size must be at least input_size. CBC inputs must
 * contain complete blocks. AES uses a 16-byte IV; 3DES and CAST-128 use an
 * 8-byte IV; RC4 has no IV. ChaCha20 uses a 12-byte IETF nonce plus the
 * separately supplied 32-bit initial block counter.
 */
int xp_cipher_crypt(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction, const void *key, size_t key_size,
	const void *iv, size_t iv_size, uint32_t initial_counter,
	const void *input, size_t input_size, void *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif
