/* Password-based key derivation primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_KDF_H_
#define ST_KDF_H_

#include "st_crypto.h"
#include "st_digest.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum st_kdf_algorithm {
	ST_KDF_PBKDF2 = 1,
	ST_KDF_SCRYPT,
};

#define ST_KDF_SCRYPT_MAX_MEMORY (UINT64_C(1025) * 1024 * 1024)

bool st_kdf_scrypt_parameters_valid(uint64_t n, uint32_t r, uint32_t p);

/* Passwords and salts are counted byte strings and may contain zero bytes. */
int st_kdf_pbkdf2(enum st_digest_algorithm digest,
	const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t iterations,
	void *result, size_t result_size);
/* Requests exceeding ST_KDF_SCRYPT_MAX_MEMORY are rejected. */
int st_kdf_scrypt(const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t n, uint32_t r, uint32_t p,
	void *result, size_t result_size);

#ifdef __cplusplus
}
#endif

#endif
