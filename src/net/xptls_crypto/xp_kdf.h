/* Password-based key derivation primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef XP_KDF_H_
#define XP_KDF_H_

#include "xp_crypto.h"
#include "xp_digest.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum xp_kdf_algorithm {
	XP_KDF_PBKDF2 = 1,
	XP_KDF_SCRYPT,
};

#define XP_KDF_SCRYPT_MAX_MEMORY (UINT64_C(1025) * 1024 * 1024)

bool xp_kdf_scrypt_parameters_valid(uint64_t n, uint32_t r, uint32_t p);

/* Passwords and salts are counted byte strings and may contain zero bytes. */
int xp_kdf_pbkdf2(enum xp_digest_algorithm digest,
	const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t iterations,
	void *result, size_t result_size);
/* Requests exceeding XP_KDF_SCRYPT_MAX_MEMORY are rejected. */
int xp_kdf_scrypt(const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t n, uint32_t r, uint32_t p,
	void *result, size_t result_size);

#ifdef __cplusplus
}
#endif

#endif
