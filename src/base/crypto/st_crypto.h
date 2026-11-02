/* Provider-neutral cryptographic primitives used by SyncTERM. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CRYPTO_H_
#define ST_CRYPTO_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum st_crypto_status {
	ST_CRYPTO_OK = 0,
	ST_CRYPTO_ERR = -1,
	ST_CRYPTO_ERR_FORMAT = -2,
	ST_CRYPTO_ERR_POLICY = -3,
	ST_CRYPTO_ERR_VERIFY = -4,
	ST_CRYPTO_ERR_DISABLED = -5,
	ST_CRYPTO_ERR_INVALID = -6,
	ST_CRYPTO_ERR_BUFFER_TOO_SMALL = -7,
	ST_CRYPTO_ERR_UNSUPPORTED = -8,
	ST_CRYPTO_ERR_UNAVAILABLE = -9,
};

const char *st_crypto_status_string(int status);
/* Provider strings have static lifetime and are never NULL. */
const char *st_crypto_provider_name(void);
const char *st_crypto_provider_version(void);
/* A NULL result is valid only when size is zero. */
int st_crypto_random(void *result, size_t size);
/* Clear size bytes before their storage is released or reused. */
void st_crypto_clear(void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
