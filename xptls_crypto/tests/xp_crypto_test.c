/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "xp_cipher.h"
#include "xp_kdf.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #condition); \
		return false; \
	} \
} while (0)

static bool
test_disabled(void)
{
	uint8_t output[32];
	uint8_t input[32] = {0};

	CHECK(xp_crypto_random(output, sizeof(output)) == XP_CRYPTO_ERR_DISABLED);
	CHECK(xp_kdf_pbkdf2(XP_DIGEST_SHA256, "p", 1, "s", 1, 1,
	    output, sizeof(output)) == XP_CRYPTO_ERR_DISABLED);
	CHECK(xp_kdf_scrypt("p", 1, "s", 1, 16, 1, 1, output,
	    sizeof(output)) == XP_CRYPTO_ERR_DISABLED);
	CHECK(xp_cipher_crypt(XP_CIPHER_AES, XP_CIPHER_ENCRYPT, input, 16,
	    input, 16, 0, input, 16, output, sizeof(output))
	    == XP_CRYPTO_ERR_DISABLED);
	return true;
}

static bool
test_kdf(void)
{
	static const uint8_t pbkdf2_expected[32] = {
		0x12,0x0f,0xb6,0xcf,0xfc,0xf8,0xb3,0x2c,
		0x43,0xe7,0x22,0x52,0x56,0xc4,0xf8,0x37,
		0xa8,0x65,0x48,0xc9,0x2c,0xcc,0x35,0x48,
		0x08,0x05,0x98,0x7c,0xb7,0x0b,0xe1,0x7b,
	};
	static const uint8_t scrypt_expected[64] = {
		0x77,0xd6,0x57,0x62,0x38,0x65,0x7b,0x20,
		0x3b,0x19,0xca,0x42,0xc1,0x8a,0x04,0x97,
		0xf1,0x6b,0x48,0x44,0xe3,0x07,0x4a,0xe8,
		0xdf,0xdf,0xfa,0x3f,0xed,0xe2,0x14,0x42,
		0xfc,0xd0,0x06,0x9d,0xed,0x09,0x48,0xf8,
		0x32,0x6a,0x75,0x3a,0x0f,0xc8,0x1f,0x17,
		0xe8,0xd3,0xe0,0xfb,0x2e,0x0d,0x36,0x28,
		0xcf,0x35,0xe2,0x0c,0x38,0xd1,0x89,0x06,
	};
	uint8_t output[64];

	CHECK(xp_kdf_pbkdf2(XP_DIGEST_SHA256, "password", 8, "salt", 4,
	    1, output, 32) == XP_CRYPTO_OK);
	CHECK(memcmp(output, pbkdf2_expected, sizeof(pbkdf2_expected)) == 0);
	CHECK(xp_kdf_scrypt(NULL, 0, NULL, 0, 16, 1, 1, output,
	    sizeof(output)) == XP_CRYPTO_OK);
	CHECK(memcmp(output, scrypt_expected, sizeof(scrypt_expected)) == 0);
	xp_crypto_clear(output, sizeof(output));
	for (size_t index = 0; index < sizeof(output); index++)
		CHECK(output[index] == 0);
	return true;
}

static bool
test_scrypt_parameters(void)
{
	CHECK(xp_kdf_scrypt_parameters_valid(2, 1, 1));
	CHECK(xp_kdf_scrypt_parameters_valid(16, 8, 1));
	CHECK(!xp_kdf_scrypt_parameters_valid(0, 1, 1));
	CHECK(!xp_kdf_scrypt_parameters_valid(3, 1, 1));
	CHECK(!xp_kdf_scrypt_parameters_valid(2, 0, 1));
	CHECK(!xp_kdf_scrypt_parameters_valid(2, 1, 0));
	CHECK(!xp_kdf_scrypt_parameters_valid(UINT64_C(1) << 63,
	    UINT32_MAX, UINT32_MAX));
	return true;
}

static bool
test_aes(void)
{
	static const uint8_t key[16] = {
		0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
		0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c,
	};
	static const uint8_t iv[16] = {
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	};
	static const uint8_t plain[16] = {
		0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
		0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
	};
	static const uint8_t encrypted[16] = {
		0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,
		0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d,
	};
	uint8_t output[16];

	CHECK(xp_cipher_supported(XP_CIPHER_AES, XP_CIPHER_ENCRYPT));
	CHECK(xp_cipher_crypt(XP_CIPHER_AES, XP_CIPHER_ENCRYPT,
	    key, sizeof(key), iv, sizeof(iv), 0, plain, sizeof(plain),
	    output, sizeof(output)) == XP_CRYPTO_OK);
	CHECK(memcmp(output, encrypted, sizeof(encrypted)) == 0);
	CHECK(xp_cipher_crypt(XP_CIPHER_AES, XP_CIPHER_DECRYPT,
	    key, sizeof(key), iv, sizeof(iv), 0, encrypted, sizeof(encrypted),
	    output, sizeof(output)) == XP_CRYPTO_OK);
	CHECK(memcmp(output, plain, sizeof(plain)) == 0);
	memcpy(output, plain, sizeof(output));
	CHECK(xp_cipher_crypt(XP_CIPHER_AES, XP_CIPHER_ENCRYPT,
	    key, sizeof(key), iv, sizeof(iv), 0, output, sizeof(output),
	    output, sizeof(output)) == XP_CRYPTO_OK);
	CHECK(memcmp(output, encrypted, sizeof(encrypted)) == 0);
	CHECK(xp_cipher_crypt(XP_CIPHER_AES, XP_CIPHER_DECRYPT,
	    key, sizeof(key), iv, sizeof(iv), 0, output, sizeof(output),
	    output, sizeof(output)) == XP_CRYPTO_OK);
	CHECK(memcmp(output, plain, sizeof(plain)) == 0);
	CHECK(xp_cipher_crypt(XP_CIPHER_AES, XP_CIPHER_ENCRYPT,
	    key, sizeof(key), iv, sizeof(iv), 0, NULL, 0, NULL, 0)
	    == XP_CRYPTO_OK);
	return true;
}

static bool
test_chacha20(void)
{
	static const uint8_t key[32] = {
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
		0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
	};
	static const uint8_t nonce[12] = {
		0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,
		0x00,0x00,0x00,0x00,
	};
	static const uint8_t expected[64] = {
		0x10,0xf1,0xe7,0xe4,0xd1,0x3b,0x59,0x15,
		0x50,0x0f,0xdd,0x1f,0xa3,0x20,0x71,0xc4,
		0xc7,0xd1,0xf4,0xc7,0x33,0xc0,0x68,0x03,
		0x04,0x22,0xaa,0x9a,0xc3,0xd4,0x6c,0x4e,
		0xd2,0x82,0x64,0x46,0x07,0x9f,0xaa,0x09,
		0x14,0xc2,0xd7,0x05,0xd9,0x8b,0x02,0xa2,
		0xb5,0x12,0x9c,0xd1,0xde,0x16,0x4e,0xb9,
		0xcb,0xd0,0x83,0xe8,0xa2,0x50,0x3c,0x4e,
	};
	uint8_t zero[64] = {0};
	uint8_t output[64];

	CHECK(xp_cipher_supported(XP_CIPHER_CHACHA20, XP_CIPHER_ENCRYPT));
	CHECK(xp_cipher_crypt(XP_CIPHER_CHACHA20, XP_CIPHER_ENCRYPT,
	    key, sizeof(key), nonce, sizeof(nonce), 1, zero, sizeof(zero),
	    output, sizeof(output)) == XP_CRYPTO_OK);
	CHECK(memcmp(output, expected, sizeof(expected)) == 0);
	return true;
}

static bool
test_digest(void)
{
	static const unsigned char md5_abc[16] = {
		0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
		0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72,
	};
	static const unsigned char sha1_abc[20] = {
		0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
		0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
		0x9c, 0xd0, 0xd8, 0x9d,
	};
	static const unsigned char sha256_abc[32] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
		0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
		0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
	};
	unsigned char digest[32];

	CHECK(xp_digest_size(XP_DIGEST_SHA256) == sizeof(digest));
	CHECK(xp_digest(XP_DIGEST_SHA256, "abc", 3, digest, sizeof(digest))
	    == XP_CRYPTO_OK);
	CHECK(memcmp(digest, sha256_abc, sizeof(digest)) == 0);
	if (strcmp(xp_crypto_provider_name(), "none") != 0) {
		CHECK(xp_digest(XP_DIGEST_MD5, "abc", 3, digest,
		    sizeof(md5_abc)) == XP_CRYPTO_OK);
		CHECK(memcmp(digest, md5_abc, sizeof(md5_abc)) == 0);
		CHECK(xp_digest(XP_DIGEST_SHA1, "abc", 3, digest,
		    sizeof(sha1_abc)) == XP_CRYPTO_OK);
		CHECK(memcmp(digest, sha1_abc, sizeof(sha1_abc)) == 0);
	}
	return true;
}

static bool
test_provider(void)
{
	uint8_t random[32];

	CHECK(xp_crypto_provider_name() != NULL);
	CHECK(xp_crypto_provider_version() != NULL);
	if (strcmp(xp_crypto_provider_name(), "none") == 0)
		return test_disabled() && test_digest();
	CHECK(xp_crypto_random(NULL, 0) == XP_CRYPTO_OK);
	CHECK(xp_crypto_random(random, sizeof(random)) == XP_CRYPTO_OK);
	return test_digest() && test_kdf() && test_aes() && test_chacha20();
}

static bool
test_digest_padding(void)
{
	static const unsigned char empty_expected[32] = {
		0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
		0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
		0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
		0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55,
	};
	static const char boundary[] =
	    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	static const unsigned char boundary_expected[32] = {
		0x24,0x8d,0x6a,0x61,0xd2,0x06,0x38,0xb8,
		0xe5,0xc0,0x26,0x93,0x0c,0x3e,0x60,0x39,
		0xa3,0x3c,0xe4,0x59,0x64,0xff,0x21,0x67,
		0xf6,0xec,0xed,0xd4,0x19,0xdb,0x06,0xc1,
	};
	unsigned char digest[32];

	CHECK(xp_digest(XP_DIGEST_SHA256, NULL, 0, digest, sizeof(digest))
	    == XP_CRYPTO_OK);
	CHECK(memcmp(digest, empty_expected, sizeof(digest)) == 0);
	CHECK(sizeof(boundary) - 1 == 56);
	CHECK(xp_digest(XP_DIGEST_SHA256, boundary, sizeof(boundary) - 1,
	    digest, sizeof(digest)) == XP_CRYPTO_OK);
	CHECK(memcmp(digest, boundary_expected, sizeof(digest)) == 0);
	CHECK(xp_digest(XP_DIGEST_SHA256, NULL, 1, digest, sizeof(digest))
	    == XP_CRYPTO_ERR_INVALID);
	CHECK(xp_digest(XP_DIGEST_SHA256, boundary, sizeof(boundary) - 1,
	    digest, sizeof(digest) - 1) == XP_CRYPTO_ERR_INVALID);
	xp_crypto_clear(NULL, 0);
	return true;
}

int
main(void)
{
	if (!test_scrypt_parameters() || !test_digest_padding())
		return EXIT_FAILURE;
	return test_provider() ? EXIT_SUCCESS : EXIT_FAILURE;
}
