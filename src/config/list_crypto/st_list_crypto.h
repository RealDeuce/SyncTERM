/* Optional encryption for SyncTERM personal BBS-list INI documents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_LIST_CRYPTO_H_
#define ST_LIST_CRYPTO_H_

#include "ini.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum st_list_crypto_format {
	ST_LIST_CRYPTO_PLAIN,
	ST_LIST_CRYPTO_V1,
	ST_LIST_CRYPTO_V2,
};

enum st_list_cipher {
	ST_LIST_CIPHER_NONE,
	ST_LIST_CIPHER_3DES,
	ST_LIST_CIPHER_IDEA,
	ST_LIST_CIPHER_CAST,
	ST_LIST_CIPHER_RC2,
	ST_LIST_CIPHER_RC4,
	ST_LIST_CIPHER_AES,
	ST_LIST_CIPHER_CHACHA20,
};

enum st_list_kdf {
	ST_LIST_KDF_NONE,
	ST_LIST_KDF_PBKDF2_SHA256,
	ST_LIST_KDF_SCRYPT,
};

struct st_list_crypto_info {
	enum st_list_crypto_format format;
	enum st_list_cipher cipher;
	enum st_list_kdf kdf;
	uint32_t key_bits;
	uint64_t iterations;
	uint8_t cost_log2;
	uint32_t block_size;
	uint32_t parallelism;
	bool rewrite_recommended;
};

struct st_list_crypto_options {
	enum st_list_cipher cipher;
	uint32_t key_bits;
	uint8_t cost_log2;
	uint32_t block_size;
	uint32_t parallelism;
};

#define ST_LIST_CRYPTO_OPTIONS_AES256 \
	{ ST_LIST_CIPHER_AES, 256, 15, 8, 1 }

/* True when the selected crypto provider can write encrypted lists. */
bool st_list_crypto_encryption_available(void);

/*
 * Plain INI and encrypted v1/v2 files are recognized from their first line.
 * Inspection never requests or derives a password and preserves stream state.
 */
int st_list_crypto_inspect(FILE *stream, struct st_list_crypto_info *info);

/*
 * A zero v1_iterations value selects the historical value of 50000. An
 * encrypted file requires a nonempty counted password; it may contain zero
 * bytes. Results are replaced only on complete success, and stream state is
 * preserved. Wrong passwords and decrypted data without DecryptionCheck=true
 * report IO_ERROR_DECRYPTION_FAILED. The legacy format is not authenticated;
 * the marker detects ordinary wrong-password and corruption cases only.
 */
int st_list_crypto_read(ini_document_t **result, FILE *stream,
	const void *password, size_t password_size, uint64_t v1_iterations,
	struct st_list_crypto_info *info);

/*
 * ST_LIST_CIPHER_NONE writes ordinary INI and ignores password/KDF fields.
 * AES-128, AES-256, and ChaCha20 writes use the v2 format and scrypt. The
 * caller must supply a stream positioned at the start and already opened for
 * replacement or truncation; this function does not perform atomic file
 * replacement.
 */
int st_list_crypto_write(FILE *stream, const ini_document_t *document,
	const struct st_list_crypto_options *options,
	const void *password, size_t password_size);

#endif
