/* Picker consent tokens binding a path to its file contents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_consent_token.h"

#include "file_map.h"
#include "st_base64.h"
#include "xp_crypto.h"
#include "xp_digest.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN_HEADER_SIZE 4U
#define TOKEN_DIGEST_SIZE 32U
#define TOKEN_TAG_SIZE 32U
#define TOKEN_FIXED_SIZE \
	(TOKEN_HEADER_SIZE + TOKEN_DIGEST_SIZE + TOKEN_TAG_SIZE)
#define HMAC_BLOCK_SIZE 64U

static int
sha256(const void *data, size_t size, uint8_t result[TOKEN_DIGEST_SIZE])
{
	return xp_digest(XP_DIGEST_SHA256, data, size, result,
	    TOKEN_DIGEST_SIZE);
}

static int
hmac_sha256(const uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE],
	const void *data, size_t size, uint8_t result[TOKEN_TAG_SIZE])
{
	uint8_t inner[TOKEN_DIGEST_SIZE];
	uint8_t inner_input[HMAC_BLOCK_SIZE + TOKEN_DIGEST_SIZE];
	uint8_t *input;
	int status;

	if (size > SIZE_MAX - HMAC_BLOCK_SIZE)
		return XP_CRYPTO_ERR_INVALID;
	input = malloc(HMAC_BLOCK_SIZE + size);
	if (input == NULL)
		return XP_CRYPTO_ERR;
	memset(input, 0x36, HMAC_BLOCK_SIZE);
	for (size_t i = 0; i < ST_CONSENT_TOKEN_KEY_SIZE; ++i)
		input[i] ^= key[i];
	memcpy(input + HMAC_BLOCK_SIZE, data, size);
	status = sha256(input, HMAC_BLOCK_SIZE + size, inner);
	xp_crypto_clear(input, HMAC_BLOCK_SIZE + size);
	free(input);
	if (status != XP_CRYPTO_OK)
		return status;
	memset(inner_input, 0x5c, HMAC_BLOCK_SIZE);
	for (size_t i = 0; i < ST_CONSENT_TOKEN_KEY_SIZE; ++i)
		inner_input[i] ^= key[i];
	memcpy(inner_input + HMAC_BLOCK_SIZE, inner, sizeof(inner));
	status = sha256(inner_input, sizeof(inner_input), result);
	xp_crypto_clear(inner, sizeof(inner));
	xp_crypto_clear(inner_input, sizeof(inner_input));
	return status;
}

static int
file_sha256(const char *path, uint8_t result[TOKEN_DIGEST_SIZE])
{
	file_map_t *mapping = NULL;
	int status = file_map_open(&mapping, path);

	if (status != 0)
		return XP_CRYPTO_ERR;
	status = sha256(file_map_data(mapping), file_map_size(mapping), result);
	file_map_close(mapping);
	return status;
}

static bool
equal_constant_time(const uint8_t *left, const uint8_t *right, size_t size)
{
	uint8_t difference = 0;

	for (size_t i = 0; i < size; ++i)
		difference |= left[i] ^ right[i];
	return difference == 0;
}

int
st_consent_token_generate_key(uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE])
{
	if (key == NULL)
		return XP_CRYPTO_ERR_INVALID;
	return xp_crypto_random(key, ST_CONSENT_TOKEN_KEY_SIZE);
}

int
st_consent_token_sign(char **result,
	const uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE], const char *path)
{
	uint8_t digest[TOKEN_DIGEST_SIZE];
	uint8_t *binary;
	char *encoded;
	size_t path_size;
	size_t binary_size;
	size_t encoded_size;
	int status;

	if (result == NULL || key == NULL || path == NULL || path[0] == '\0')
		return XP_CRYPTO_ERR_INVALID;
	*result = NULL;
	path_size = strlen(path);
	if (path_size > UINT16_MAX)
		return XP_CRYPTO_ERR_INVALID;
	status = file_sha256(path, digest);
	if (status != XP_CRYPTO_OK)
		return status;
	binary_size = TOKEN_FIXED_SIZE + path_size;
	binary = malloc(binary_size);
	if (binary == NULL)
		return XP_CRYPTO_ERR;
	binary[0] = (uint8_t)(path_size >> 24);
	binary[1] = (uint8_t)(path_size >> 16);
	binary[2] = (uint8_t)(path_size >> 8);
	binary[3] = (uint8_t)path_size;
	memcpy(binary + TOKEN_HEADER_SIZE, path, path_size);
	memcpy(binary + TOKEN_HEADER_SIZE + path_size, digest, sizeof(digest));
	status = hmac_sha256(key, binary,
	    TOKEN_HEADER_SIZE + path_size + TOKEN_DIGEST_SIZE,
	    binary + TOKEN_HEADER_SIZE + path_size + TOKEN_DIGEST_SIZE);
	if (status != XP_CRYPTO_OK) {
		free(binary);
		return status;
	}
	if (!st_base64_encoded_size(binary_size, &encoded_size)
	    || encoded_size == SIZE_MAX) {
		free(binary);
		return XP_CRYPTO_ERR_INVALID;
	}
	encoded = malloc(encoded_size + 1);
	if (encoded == NULL) {
		free(binary);
		return XP_CRYPTO_ERR;
	}
	if (!st_base64_encode(encoded, encoded_size + 1, binary, binary_size)) {
		free(encoded);
		free(binary);
		return XP_CRYPTO_ERR;
	}
	free(binary);
	*result = encoded;
	return XP_CRYPTO_OK;
}

int
st_consent_token_verify(char **result,
	const uint8_t key[ST_CONSENT_TOKEN_KEY_SIZE], const char *token)
{
	uint8_t expected[TOKEN_TAG_SIZE];
	uint8_t digest[TOKEN_DIGEST_SIZE];
	uint8_t *binary;
	char *path;
	size_t token_size;
	size_t binary_size;
	size_t path_size;
	int status;

	if (result == NULL || key == NULL || token == NULL)
		return XP_CRYPTO_ERR_INVALID;
	*result = NULL;
	token_size = strlen(token);
	if (!st_base64_decoded_size(token, token_size, &binary_size)
	    || binary_size < TOKEN_FIXED_SIZE)
		return XP_CRYPTO_ERR_FORMAT;
	binary = malloc(binary_size);
	if (binary == NULL)
		return XP_CRYPTO_ERR;
	if (!st_base64_decode(binary, binary_size, token, token_size)) {
		free(binary);
		return XP_CRYPTO_ERR_FORMAT;
	}
	path_size = ((size_t)binary[0] << 24) | ((size_t)binary[1] << 16)
	    | ((size_t)binary[2] << 8) | binary[3];
	if (path_size == 0 || binary_size != TOKEN_FIXED_SIZE + path_size
	    || memchr(binary + TOKEN_HEADER_SIZE, '\0', path_size) != NULL) {
		free(binary);
		return XP_CRYPTO_ERR_FORMAT;
	}
	status = hmac_sha256(key, binary,
	    TOKEN_HEADER_SIZE + path_size + TOKEN_DIGEST_SIZE, expected);
	if (status != XP_CRYPTO_OK) {
		free(binary);
		return status;
	}
	if (!equal_constant_time(expected,
	    binary + TOKEN_HEADER_SIZE + path_size + TOKEN_DIGEST_SIZE,
	    TOKEN_TAG_SIZE)) {
		free(binary);
		return XP_CRYPTO_ERR_VERIFY;
	}
	path = malloc(path_size + 1);
	if (path == NULL) {
		free(binary);
		return XP_CRYPTO_ERR;
	}
	memcpy(path, binary + TOKEN_HEADER_SIZE, path_size);
	path[path_size] = '\0';
	status = file_sha256(path, digest);
	if (status == XP_CRYPTO_OK && !equal_constant_time(digest,
	    binary + TOKEN_HEADER_SIZE + path_size, TOKEN_DIGEST_SIZE))
		status = XP_CRYPTO_ERR_VERIFY;
	free(binary);
	if (status != XP_CRYPTO_OK) {
		free(path);
		return status;
	}
	*result = path;
	return XP_CRYPTO_OK;
}
