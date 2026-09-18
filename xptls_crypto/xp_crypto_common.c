/* Provider-neutral cryptographic primitives used by SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "xp_crypto.h"
#include "xp_kdf.h"

const char *
xp_crypto_status_string(int status)
{
	switch (status) {
	case XP_CRYPTO_OK: return "success";
	case XP_CRYPTO_ERR_FORMAT: return "invalid format";
	case XP_CRYPTO_ERR_POLICY: return "rejected by policy";
	case XP_CRYPTO_ERR_VERIFY: return "verification failed";
	case XP_CRYPTO_ERR_DISABLED: return "cryptography disabled";
	case XP_CRYPTO_ERR_INVALID: return "invalid argument";
	case XP_CRYPTO_ERR_BUFFER_TOO_SMALL: return "buffer too small";
	case XP_CRYPTO_ERR_UNSUPPORTED: return "unsupported operation";
	case XP_CRYPTO_ERR_UNAVAILABLE: return "provider unavailable";
	default: return "cryptographic operation failed";
	}
}

bool
xp_kdf_scrypt_parameters_valid(uint64_t n, uint32_t r, uint32_t p)
{
	uint64_t unit;
	uint64_t blocks;

	if (n < 2 || (n & (n - 1)) != 0 || r == 0 || p == 0)
		return false;
	unit = UINT64_C(128) * r;
	blocks = XP_KDF_SCRYPT_MAX_MEMORY / unit;
	return n <= blocks && blocks - n >= 2 && p <= blocks - n - 2;
}
