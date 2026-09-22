/* Picker consent tokens binding a path to its file contents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSENT_TOKEN_H_
#define ST_CONSENT_TOKEN_H_

#include <stddef.h>
#include <stdint.h>

#define ST_CONSENT_TOKEN_KEY_SIZE 32U

/* Returns an XP_CRYPTO_* status. */
int st_consent_token_generate_key(
	uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE]);

/* Returns a malloc-owned Base64 token or verified path on success. */
int st_consent_token_sign(char **result,
	const uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE], const char *path);
int st_consent_token_verify(char **result,
	const uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE], const char *token);

#endif
