/* Consent-token contract tests. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_consent_token.h"

#include "filesystem.h"
#include "xp_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

int
main(void)
{
	static const uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	};
	uint8_t other[ST_CONSENT_TOKEN_KEY_SIZE];
	char *token = NULL;
	char *path = NULL;
	char *tampered;

	(void)fs_remove_file(ST_CONSENT_TOKEN_TEST_DIRECTORY);
	CHECK(fs_replace_file(ST_CONSENT_TOKEN_TEST_DIRECTORY, "original", 8)
	    == 0);
	CHECK(st_consent_token_sign(&token, key,
	    ST_CONSENT_TOKEN_TEST_DIRECTORY) == XP_CRYPTO_OK);
	CHECK(token != NULL);
	CHECK(st_consent_token_verify(&path, key, token) == XP_CRYPTO_OK);
	CHECK(path != NULL && strcmp(path, ST_CONSENT_TOKEN_TEST_DIRECTORY) == 0);
	free(path);
	path = NULL;
	tampered = malloc(strlen(token) + 1);
	CHECK(tampered != NULL);
	if (tampered != NULL) {
		strcpy(tampered, token);
		tampered[5] = tampered[5] == 'A' ? 'B' : 'A';
		CHECK(st_consent_token_verify(&path, key, tampered)
		    != XP_CRYPTO_OK);
		free(tampered);
	}
	memcpy(other, key, sizeof(other));
	other[0] ^= 1;
	CHECK(st_consent_token_verify(&path, other, token)
	    == XP_CRYPTO_ERR_VERIFY);
	CHECK(fs_replace_file(ST_CONSENT_TOKEN_TEST_DIRECTORY, "changed", 7)
	    == 0);
	CHECK(st_consent_token_verify(&path, key, token)
	    == XP_CRYPTO_ERR_VERIFY);
	free(token);
	CHECK(fs_remove_file(ST_CONSENT_TOKEN_TEST_DIRECTORY) == 0);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
