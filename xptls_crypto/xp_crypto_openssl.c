/* OpenSSL provider for the xptls crypto primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "xp_cipher.h"
#include "xp_kdf.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/provider.h>
#include <openssl/rand.h>

static CRYPTO_ONCE legacy_once = CRYPTO_ONCE_STATIC_INIT;
static OSSL_PROVIDER *legacy_provider;

static void
load_legacy_provider(void)
{
	legacy_provider = OSSL_PROVIDER_load(NULL, "legacy");
}

static bool
legacy_algorithm(enum xp_cipher_algorithm algorithm)
{
	return algorithm == XP_CIPHER_3DES || algorithm == XP_CIPHER_CAST128
	    || algorithm == XP_CIPHER_RC4;
}

static const char *
cipher_name(enum xp_cipher_algorithm algorithm, size_t key_size)
{
	switch (algorithm) {
	case XP_CIPHER_AES:
		if (key_size == 16)
			return "AES-128-CBC";
		if (key_size == 24)
			return "AES-192-CBC";
		return key_size == 32 ? "AES-256-CBC" : NULL;
	case XP_CIPHER_CHACHA20:
		return key_size == 32 ? "ChaCha20" : NULL;
	case XP_CIPHER_3DES:
		return key_size == 24 ? "DES-EDE3-CBC" : NULL;
	case XP_CIPHER_CAST128:
		return key_size >= 5 && key_size <= 16 ? "CAST5-CBC" : NULL;
	case XP_CIPHER_RC4:
		return key_size > 0 && key_size <= 32 ? "RC4" : NULL;
	}
	return NULL;
}

static EVP_CIPHER *
fetch_cipher(enum xp_cipher_algorithm algorithm, size_t key_size)
{
	const char *name = cipher_name(algorithm, key_size);
	EVP_CIPHER *cipher;

	if (name == NULL)
		return NULL;
	cipher = EVP_CIPHER_fetch(NULL, name, NULL);
	if (cipher == NULL && legacy_algorithm(algorithm)) {
		CRYPTO_THREAD_run_once(&legacy_once, load_legacy_provider);
		cipher = EVP_CIPHER_fetch(NULL, name, NULL);
	}
	return cipher;
}

const char *xp_crypto_provider_name(void) { return "OpenSSL"; }
const char *xp_crypto_provider_version(void) { return OpenSSL_version(OPENSSL_VERSION); }

int
xp_crypto_random(void *result, size_t size)
{
	unsigned char *output = result;

	if (result == NULL && size != 0)
		return XP_CRYPTO_ERR_INVALID;
	while (size != 0) {
		int chunk = size > INT_MAX ? INT_MAX : (int)size;

		if (RAND_bytes(output, chunk) != 1)
			return XP_CRYPTO_ERR;
		output += chunk;
		size -= (size_t)chunk;
	}
	return XP_CRYPTO_OK;
}

bool
xp_cipher_supported(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction)
{
	size_t key_size = algorithm == XP_CIPHER_3DES ? 24
	    : algorithm == XP_CIPHER_CAST128 || algorithm == XP_CIPHER_RC4
	    ? 16 : 32;
	EVP_CIPHER *cipher;

	if (direction != XP_CIPHER_ENCRYPT && direction != XP_CIPHER_DECRYPT)
		return false;
	if (direction == XP_CIPHER_ENCRYPT && legacy_algorithm(algorithm))
		return false;
	cipher = fetch_cipher(algorithm, key_size);
	if (cipher == NULL)
		return false;
	EVP_CIPHER_free(cipher);
	return true;
}

int
xp_cipher_crypt(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction, const void *key, size_t key_size,
	const void *iv, size_t iv_size, uint32_t initial_counter,
	const void *input, size_t input_size, void *output, size_t output_size)
{
	EVP_CIPHER *cipher = NULL;
	EVP_CIPHER_CTX *context = NULL;
	unsigned char native_iv[16];
	const unsigned char *use_iv = iv;
	const unsigned char *source = input;
	unsigned char empty_output;
	unsigned char *destination = output == NULL ? &empty_output : output;
	size_t remaining = input_size;
	int result = XP_CRYPTO_ERR;
	int produced;

	if ((direction != XP_CIPHER_ENCRYPT && direction != XP_CIPHER_DECRYPT)
	    || key == NULL || (iv == NULL && iv_size != 0)
	    || (input == NULL && input_size != 0)
	    || (output == NULL && input_size != 0) || output_size < input_size)
		return XP_CRYPTO_ERR_INVALID;
	if ((algorithm == XP_CIPHER_CHACHA20 && iv_size != 12)
	    || (algorithm == XP_CIPHER_RC4 && iv_size != 0)
	    || (algorithm == XP_CIPHER_AES && iv_size != 16)
	    || ((algorithm == XP_CIPHER_3DES || algorithm == XP_CIPHER_CAST128)
	    && iv_size != 8))
		return XP_CRYPTO_ERR_POLICY;
	cipher = fetch_cipher(algorithm, key_size);
	if (cipher == NULL)
		return cipher_name(algorithm, key_size) == NULL
		    ? XP_CRYPTO_ERR_POLICY : XP_CRYPTO_ERR_UNSUPPORTED;
	context = EVP_CIPHER_CTX_new();
	if (context == NULL)
		goto done;
	if (algorithm == XP_CIPHER_CHACHA20) {
		native_iv[0] = (unsigned char)initial_counter;
		native_iv[1] = (unsigned char)(initial_counter >> 8);
		native_iv[2] = (unsigned char)(initial_counter >> 16);
		native_iv[3] = (unsigned char)(initial_counter >> 24);
		memcpy(native_iv + 4, iv, 12);
		use_iv = native_iv;
	}
	if (EVP_CipherInit_ex(context, cipher, NULL, NULL, NULL,
	    direction == XP_CIPHER_ENCRYPT) != 1
	    || EVP_CIPHER_CTX_set_padding(context, 0) != 1)
		goto done;
	if (algorithm == XP_CIPHER_CAST128 || algorithm == XP_CIPHER_RC4) {
		if (EVP_CIPHER_CTX_set_key_length(context, (int)key_size) != 1)
			goto done;
	}
	if (EVP_CipherInit_ex(context, NULL, NULL, key, use_iv, -1) != 1)
		goto done;
	while (remaining != 0) {
		size_t limit = INT_MAX;
		int block_size = EVP_CIPHER_get_block_size(cipher);

		if (block_size > 1)
			limit -= limit % (size_t)block_size;
		size_t chunk = remaining > limit ? limit : remaining;
		if (EVP_CipherUpdate(context, destination, &produced, source,
		    (int)chunk) != 1 || produced < 0 || (size_t)produced != chunk)
			goto done;
		source += chunk;
		destination += chunk;
		remaining -= chunk;
	}
	if (EVP_CipherFinal_ex(context, destination, &produced) != 1
	    || produced != 0)
		goto done;
	result = XP_CRYPTO_OK;

done:
	xp_crypto_clear(native_iv, sizeof(native_iv));
	EVP_CIPHER_CTX_free(context);
	EVP_CIPHER_free(cipher);
	return result;
}

int
xp_digest(enum xp_digest_algorithm digest, const void *input,
	size_t input_size, void *result, size_t result_size)
{
	const EVP_MD *md = digest == XP_DIGEST_SHA256 ? EVP_sha256()
	    : digest == XP_DIGEST_SHA384 ? EVP_sha384()
	    : digest == XP_DIGEST_SHA512 ? EVP_sha512() : NULL;
	unsigned int produced = 0;

	if ((input == NULL && input_size != 0) || result == NULL || md == NULL
	    || result_size != xp_digest_size(digest))
		return XP_CRYPTO_ERR_INVALID;
	return EVP_Digest(input, input_size, result, &produced, md, NULL) == 1
	    && produced == result_size ? XP_CRYPTO_OK : XP_CRYPTO_ERR;
}

size_t
xp_digest_size(enum xp_digest_algorithm digest)
{
	return digest == XP_DIGEST_SHA256 ? 32
	    : digest == XP_DIGEST_SHA384 ? 48
	    : digest == XP_DIGEST_SHA512 ? 64 : 0;
}

int
xp_kdf_pbkdf2(enum xp_digest_algorithm digest,
	const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t iterations,
	void *result, size_t result_size)
{
	const EVP_MD *md = digest == XP_DIGEST_SHA256 ? EVP_sha256()
	    : digest == XP_DIGEST_SHA384 ? EVP_sha384()
	    : digest == XP_DIGEST_SHA512 ? EVP_sha512() : NULL;

	if ((password == NULL && password_size != 0)
	    || (salt == NULL && salt_size != 0)
	    || (result == NULL && result_size != 0) || md == NULL
	    || iterations == 0 || iterations > INT_MAX || password_size > INT_MAX
	    || salt_size > INT_MAX || result_size > INT_MAX)
		return XP_CRYPTO_ERR_INVALID;
	return PKCS5_PBKDF2_HMAC(password, (int)password_size, salt,
	    (int)salt_size, (int)iterations, md, (int)result_size, result) == 1
	    ? XP_CRYPTO_OK : XP_CRYPTO_ERR;
}

int
xp_kdf_scrypt(const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t n, uint32_t r, uint32_t p,
	void *result, size_t result_size)
{
	EVP_KDF *algorithm;
	EVP_KDF_CTX *context;
	uint64_t max_memory = XP_KDF_SCRYPT_MAX_MEMORY;
	int status;

	if ((password == NULL && password_size != 0)
	    || (salt == NULL && salt_size != 0)
	    || (result == NULL && result_size != 0)
	    || !xp_kdf_scrypt_parameters_valid(n, r, p))
		return XP_CRYPTO_ERR_INVALID;
	algorithm = EVP_KDF_fetch(NULL, "SCRYPT", NULL);
	if (algorithm == NULL)
		return XP_CRYPTO_ERR_UNSUPPORTED;
	context = EVP_KDF_CTX_new(algorithm);
	EVP_KDF_free(algorithm);
	if (context == NULL)
		return XP_CRYPTO_ERR;
	OSSL_PARAM parameters[] = {
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
		    (void *)password, password_size),
		OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
		    (void *)salt, salt_size),
		OSSL_PARAM_construct_uint64(OSSL_KDF_PARAM_SCRYPT_N, &n),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SCRYPT_R, &r),
		OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SCRYPT_P, &p),
		OSSL_PARAM_construct_uint64(OSSL_KDF_PARAM_SCRYPT_MAXMEM,
		    &max_memory),
		OSSL_PARAM_construct_end()
	};
	status = EVP_KDF_derive(context, result, result_size, parameters) == 1
	    ? XP_CRYPTO_OK : XP_CRYPTO_ERR_POLICY;
	EVP_KDF_CTX_free(context);
	return status;
}
