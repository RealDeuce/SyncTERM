/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_list_crypto.h"

#include "io_error.h"
#include "list_crypto_internal.h"

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

static FILE *
stream_from(const void *data, size_t size)
{
	FILE *stream = tmpfile();

	if (stream == NULL)
		return NULL;
	if ((size != 0 && fwrite(data, 1, size, stream) != size)
	    || fflush(stream) != 0 || fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return NULL;
	}
	return stream;
}

static bool
same_document(const ini_document_t *document, const char *expected)
{
	char *serialized = NULL;
	size_t size = 0;
	bool result = ini_document_serialize(document, &serialized, &size) == 0
	    && size == strlen(expected) && memcmp(serialized, expected, size) == 0;

	free(serialized);
	return result;
}

static bool
test_plain(void)
{
	static const char input[] = "; comment\r\n[Example]\r\nPassword=secret\r\n";
	struct st_list_crypto_options options = {ST_LIST_CIPHER_NONE, 0, 0, 0, 0};
	struct st_list_crypto_info info;
	ini_document_t *document = NULL;
	ini_document_t *read = NULL;
	FILE *stream = stream_from(input, sizeof(input) - 1);
	long position;

	CHECK(stream != NULL);
	CHECK(fseek(stream, 3, SEEK_SET) == 0);
	position = ftell(stream);
	CHECK(st_list_crypto_inspect(stream, &info) == 0);
	CHECK(ftell(stream) == position);
	CHECK(info.format == ST_LIST_CRYPTO_PLAIN
	    && info.cipher == ST_LIST_CIPHER_NONE);
	CHECK(st_list_crypto_read(&read, stream, NULL, 0, 0, &info) == 0);
	CHECK(ftell(stream) == position && same_document(read, input));
	fclose(stream);

	CHECK(ini_document_parse(&document,
	    "DecryptionCheck=true\n[Example]\nPassword=secret\n",
	    sizeof("DecryptionCheck=true\n[Example]\nPassword=secret\n") - 1) == 0);
	stream = tmpfile();
	CHECK(stream != NULL);
	CHECK(st_list_crypto_write(stream, document, &options, NULL, 0) == 0);
	CHECK(fflush(stream) == 0 && fseek(stream, 0, SEEK_SET) == 0);
	ini_document_free(&read);
	CHECK(st_list_crypto_read(&read, stream, NULL, 0, 0, NULL) == 0);
	CHECK(!ini_section_has_key(ini_document_root(read), "DecryptionCheck"));
	CHECK(strcmp(ini_section_find(ini_document_find_section(read, "Example"),
	    "Password"), "secret") == 0);
	fclose(stream);
	ini_document_free(&read);
	ini_document_free(&document);
	return true;
}

static bool
round_trip(enum st_list_cipher cipher, uint32_t key_bits)
{
	static const char input[] =
	    "; preserved\n\n[First]\nAddress=example.com\nPassword=secret\n";
	static const char password[] = "correct horse battery staple";
	struct st_list_crypto_options options = {
		cipher, key_bits, 8, 8, 1
	};
	struct st_list_crypto_info info;
	ini_document_t *document = NULL;
	ini_document_t *read = NULL;
	ini_document_t *sentinel = NULL;
	FILE *stream;
	int status;
	uint32_t expected_key_bits = key_bits == 0 ? 256 : key_bits;

	CHECK(ini_document_parse(&document, input, sizeof(input) - 1) == 0);
	stream = tmpfile();
	CHECK(stream != NULL);
	status = st_list_crypto_write(stream, document, &options, password,
	    sizeof(password) - 1);
	if (status == IO_ERROR_NOT_SUPPORTED) {
		fclose(stream);
		ini_document_free(&document);
		return true;
	}
	CHECK(status == 0);
	CHECK(fflush(stream) == 0 && fseek(stream, 0, SEEK_SET) == 0);
	CHECK(st_list_crypto_inspect(stream, &info) == 0);
	CHECK(info.format == ST_LIST_CRYPTO_V2 && info.cipher == cipher
	    && info.key_bits == expected_key_bits
	    && info.kdf == ST_LIST_KDF_SCRYPT
	    && info.cost_log2 == 8 && info.block_size == 8
	    && info.parallelism == 1 && !info.rewrite_recommended);
	CHECK(st_list_crypto_read(&read, stream, NULL, 0, 0, NULL)
	    == IO_ERROR_PASSWORD_REQUIRED);
	CHECK(ini_document_create(&sentinel) == 0);
	read = sentinel;
	CHECK(st_list_crypto_read(&read, stream, "wrong", 5, 0, NULL)
	    == IO_ERROR_DECRYPTION_FAILED);
	CHECK(read == sentinel);
	CHECK(st_list_crypto_read(&read, stream, password, sizeof(password) - 1,
	    0, &info) == 0);
	CHECK(read != sentinel);
	CHECK(ini_section_get_bool(ini_document_root(read), "DecryptionCheck",
	    false));
	CHECK(strcmp(ini_section_find(ini_document_find_section(read, "first"),
	    "Password"), "secret") == 0);
	char *serialized = NULL;
	size_t serialized_size = 0;
	CHECK(ini_document_serialize(read, &serialized, &serialized_size) == 0);
	CHECK(serialized_size != 0 && strstr(serialized, "; preserved\n\n")
	    != NULL);
	free(serialized);
	CHECK(info.format == ST_LIST_CRYPTO_V2 && info.cipher == cipher);
	fclose(stream);
	ini_document_free(&read);
	ini_document_free(&document);
	return true;
}

static bool
test_invalid(void)
{
	static const char malformed[] =
	    "; Encrypted INI File v2, Algorithm: AES-256, KDF: no, Salt: 00\n";
	static const char truncated[] =
	    "; Encrypted INI File v2, Algorithm: AES-256, "
	    "KDF: scrypt-N8-r8-p1, Salt: 000102030405060708090a0b0c0d0e0f\n"
	    "\000\020short";
	struct st_list_crypto_info info;
	struct st_list_crypto_options invalid = {
		ST_LIST_CIPHER_AES, 192, 8, 8, 1
	};
	ini_document_t *document = NULL;
	FILE *stream = stream_from(malformed, sizeof(malformed) - 1);

	CHECK(stream != NULL);
	CHECK(st_list_crypto_inspect(stream, &info) == IO_ERROR_INVALID_FORMAT);
	fclose(stream);
	stream = stream_from(truncated, sizeof(truncated) - 1);
	CHECK(stream != NULL);
	CHECK(st_list_crypto_read(&document, stream, "password", 8, 0, NULL)
	    == IO_ERROR_INVALID_FORMAT);
	fclose(stream);
	CHECK(ini_document_create(&document) == 0);
	stream = tmpfile();
	CHECK(stream != NULL);
	CHECK(st_list_crypto_write(stream, document, &invalid, "password", 8)
	    == IO_ERROR_INVALID_ARGUMENT);
	invalid.key_bits = 256;
	invalid.cost_log2 = 7;
	CHECK(st_list_crypto_write(stream, document, &invalid, "password", 8)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_list_crypto_write(stream, document, &invalid, NULL, 0)
	    == IO_ERROR_PASSWORD_REQUIRED);
	fclose(stream);
	ini_document_free(&document);
	return true;
}

static bool
test_header_variants(void)
{
	static const struct {
		const char *header;
		enum st_list_crypto_format format;
		enum st_list_cipher cipher;
		uint32_t key_bits;
	} cases[] = {
		{"; Encrypted INI File, Algorithm: 3DES salt\n",
		    ST_LIST_CRYPTO_V1, ST_LIST_CIPHER_3DES, 192},
		{"; Encrypted INI File, Algorithm: IDEA salt\n",
		    ST_LIST_CRYPTO_V1, ST_LIST_CIPHER_IDEA, 128},
		{"; Encrypted INI File, Algorithm: CAST-40 salt\n",
		    ST_LIST_CRYPTO_V1, ST_LIST_CIPHER_CAST, 40},
		{"; Encrypted INI File, Algorithm: RC2-256 salt\n",
		    ST_LIST_CRYPTO_V1, ST_LIST_CIPHER_RC2, 256},
		{"; Encrypted INI File, Algorithm: RC4 salt\n",
		    ST_LIST_CRYPTO_V1, ST_LIST_CIPHER_RC4, 128},
		{"; Encrypted INI File v2, Algorithm: AES-192, KDF:  "
		    "PBKDF2-SHA256-i2  , Salt: 00\n",
		    ST_LIST_CRYPTO_V2, ST_LIST_CIPHER_AES, 192},
		{"; Encrypted INI File v2, Algorithm: ChaCha20, KDF: "
		    "scrypt-N8, Salt: 00\n",
		    ST_LIST_CRYPTO_V2, ST_LIST_CIPHER_CHACHA20, 256},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct st_list_crypto_info info;
		FILE *stream = stream_from(cases[i].header, strlen(cases[i].header));

		CHECK(stream != NULL);
		CHECK(st_list_crypto_inspect(stream, &info) == 0);
		CHECK(info.format == cases[i].format
		    && info.cipher == cases[i].cipher
		    && info.key_bits == cases[i].key_bits);
		fclose(stream);
	}
	return true;
}

static const uint8_t v1_aes_fixture[] = {
	0x3b,0x20,0x45,0x6e,0x63,0x72,0x79,0x70,0x74,0x65,0x64,0x20,
	0x49,0x4e,0x49,0x20,0x46,0x69,0x6c,0x65,0x2c,0x20,0x41,0x6c,
	0x67,0x6f,0x72,0x69,0x74,0x68,0x6d,0x3a,0x20,0x41,0x45,0x53,
	0x2d,0x32,0x35,0x36,0x20,0x6c,0x65,0x67,0x61,0x63,0x79,0x20,
	0x73,0x61,0x6c,0x74,0x20,0x20,0x20,0x0d,0x0a,0x00,0x10,0x10,
	0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,
	0x1d,0x1e,0x1f,0x82,0x42,0xf4,0xa8,0x4b,0x26,0xf2,0x9a,0xae,
	0x42,0x1d,0x32,0xce,0x09,0x42,0x03,0xcf,0x1c,0x97,0x92,0x57,
	0x97,0xa1,0x8c,0x6e,0x21,0x62,0xe7,0xea,0x6c,0xe8,0x5f,0xb6,
	0xd3,0x55,0xb8,0xd0,0x29,0x59,0x26,0x85,0x99,0x6a,0x23,0xc8,
	0x29,0xd9,0x39,
};

static const uint8_t v2_pbkdf2_fixture[] = {
	0x3b,0x20,0x45,0x6e,0x63,0x72,0x79,0x70,0x74,0x65,0x64,0x20,
	0x49,0x4e,0x49,0x20,0x46,0x69,0x6c,0x65,0x20,0x76,0x32,0x2c,
	0x20,0x41,0x6c,0x67,0x6f,0x72,0x69,0x74,0x68,0x6d,0x3a,0x20,
	0x41,0x45,0x53,0x2d,0x31,0x32,0x38,0x2c,0x20,0x4b,0x44,0x46,
	0x3a,0x20,0x50,0x42,0x4b,0x44,0x46,0x32,0x2d,0x53,0x48,0x41,
	0x32,0x35,0x36,0x2d,0x69,0x32,0x2c,0x20,0x53,0x61,0x6c,0x74,
	0x3a,0x20,0x30,0x30,0x30,0x31,0x30,0x32,0x30,0x33,0x30,0x34,
	0x30,0x35,0x30,0x36,0x30,0x37,0x30,0x38,0x30,0x39,0x30,0x61,
	0x30,0x62,0x30,0x63,0x30,0x64,0x30,0x65,0x30,0x66,0x20,0x20,
	0x0d,0x0a,0x00,0x10,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
	0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0xf4,0x72,0xf8,0xa6,
	0x90,0xeb,0x2e,0xa0,0xe7,0xb4,0x06,0x99,0x62,0x55,0xf3,0x6d,
	0x20,0x38,0x86,0x3c,0x46,0x38,0x1c,0xef,0xa3,0xd4,0x9f,0xc1,
	0xc0,0x98,0x55,0x24,0x6b,0xd1,0x67,0xa3,0x76,0xdf,0xb9,0x0c,
	0xe1,0xce,0xaf,0x19,0xae,0x1e,0x1a,0xad,
};

static const uint8_t v2_chacha_fixture[] = {
	0x3b,0x20,0x45,0x6e,0x63,0x72,0x79,0x70,0x74,0x65,0x64,0x20,
	0x49,0x4e,0x49,0x20,0x46,0x69,0x6c,0x65,0x20,0x76,0x32,0x2c,
	0x20,0x41,0x6c,0x67,0x6f,0x72,0x69,0x74,0x68,0x6d,0x3a,0x20,
	0x43,0x68,0x61,0x43,0x68,0x61,0x32,0x30,0x2d,0x32,0x35,0x36,
	0x2c,0x20,0x4b,0x44,0x46,0x3a,0x20,0x73,0x63,0x72,0x79,0x70,
	0x74,0x2d,0x4e,0x38,0x2d,0x72,0x38,0x2d,0x70,0x31,0x2c,0x20,
	0x53,0x61,0x6c,0x74,0x3a,0x20,0x30,0x30,0x30,0x31,0x30,0x32,
	0x30,0x33,0x30,0x34,0x30,0x35,0x30,0x36,0x30,0x37,0x30,0x38,
	0x30,0x39,0x30,0x61,0x30,0x62,0x30,0x63,0x30,0x64,0x30,0x65,
	0x30,0x66,0x0a,0x00,0x10,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
	0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00,0xb4,0xa2,0xc3,
	0x35,0x11,0x40,0x11,0xec,0xe5,0xac,0xc1,0xdb,0xc0,0x36,0x35,
	0x4d,0x99,0x37,0xff,0x9a,0xe3,0x3c,0xf0,0xf8,0x32,0xb9,0xfd,
	0x8a,0xd5,0x88,0x02,0xe0,0xd7,0x16,0x6e,0x63,0x31,0xab,0xc8,
	0xc1,0x75,0x42,0x5f,0x71,0xdf,0x1e,0x35,
};

static bool
known_file(const void *data, size_t size, uint64_t v1_iterations,
	enum st_list_crypto_format format, enum st_list_cipher cipher,
	enum st_list_kdf kdf, bool rewrite_recommended)
{
	struct st_list_crypto_info info;
	ini_document_t *document = NULL;
	FILE *stream = stream_from(data, size);
	int status;

	CHECK(stream != NULL);
	CHECK(st_list_crypto_inspect(stream, &info) == 0);
	CHECK(info.format == format && info.cipher == cipher && info.kdf == kdf
	    && info.rewrite_recommended == rewrite_recommended);
	status = st_list_crypto_read(&document, stream, "password", 8,
	    v1_iterations, &info);
	if (status == IO_ERROR_NOT_SUPPORTED) {
		fclose(stream);
		return true;
	}
	CHECK(status == 0);
	CHECK(ini_section_get_bool(ini_document_root(document),
	    "DecryptionCheck", false));
	CHECK(strcmp(ini_section_find(ini_document_find_section(document,
	    "example"), "password"), "secret") == 0);
	CHECK(info.format == format && info.cipher == cipher && info.kdf == kdf);
	ini_document_free(&document);
	fclose(stream);
	return true;
}

static bool
test_known_files(void)
{
	return known_file(v1_aes_fixture, sizeof(v1_aes_fixture), 2,
	    ST_LIST_CRYPTO_V1, ST_LIST_CIPHER_AES,
	    ST_LIST_KDF_PBKDF2_SHA256, true)
	    && known_file(v2_pbkdf2_fixture, sizeof(v2_pbkdf2_fixture), 0,
	    ST_LIST_CRYPTO_V2, ST_LIST_CIPHER_AES,
	    ST_LIST_KDF_PBKDF2_SHA256, true)
	    && known_file(v2_chacha_fixture, sizeof(v2_chacha_fixture), 0,
	    ST_LIST_CRYPTO_V2, ST_LIST_CIPHER_CHACHA20,
	    ST_LIST_KDF_SCRYPT, false);
}

static const uint8_t v1_3des_ciphertext[48] = {
	0x54,0x43,0xf0,0x32,0xfb,0x5f,0x42,0x01,0xee,0xc3,0xb9,0xc7,
	0x0a,0x36,0xe0,0xfb,0x5c,0xa6,0x6b,0x31,0xaf,0x5e,0x83,0x30,
	0xb7,0x54,0xe4,0xa2,0xb5,0xac,0x7d,0x26,0x0f,0x24,0x28,0x47,
	0x0e,0x0b,0x69,0xbf,0x72,0x24,0x28,0x30,0x4e,0xa0,0xc5,0x62,
};

static const uint8_t v1_idea_ciphertext[48] = {
	0x4e,0x73,0x08,0x66,0x34,0x17,0x00,0x8c,0x9c,0x3a,0x82,0x58,
	0xf2,0x12,0x31,0x05,0x40,0x1a,0x88,0xca,0x35,0x1c,0x79,0x8d,
	0x06,0x23,0x1f,0x0b,0xd5,0x4c,0x5a,0x52,0x73,0xe9,0x23,0x6a,
	0xf3,0x42,0xb6,0xe1,0x4d,0x73,0xaf,0x79,0x1b,0x59,0x0f,0x13,
};

static const uint8_t v1_cast_ciphertext[48] = {
	0xa1,0xab,0x8b,0x0e,0xa4,0x3e,0x4f,0x16,0x46,0xff,0x8f,0x17,
	0x54,0xcd,0x16,0x73,0x48,0x7b,0xc5,0x5d,0x63,0x39,0xff,0x80,
	0x05,0x8b,0x33,0x83,0x78,0xf8,0x91,0x79,0x26,0x94,0x88,0x03,
	0xc4,0x20,0xdb,0xce,0x31,0x9c,0xa8,0x77,0x37,0x51,0xa9,0x10,
};

static const uint8_t v1_rc2_ciphertext[48] = {
	0x32,0xeb,0x86,0x47,0xfb,0xde,0x50,0x72,0xa4,0x5d,0x49,0x45,
	0x8b,0x5e,0xec,0xbd,0xa3,0xc2,0x54,0xa4,0x5d,0x3c,0x62,0x31,
	0xf4,0x34,0x56,0xc2,0x74,0x77,0x9a,0xf4,0x28,0x9a,0xb5,0xf0,
	0x69,0xa0,0xcb,0xa8,0xc7,0x9e,0xe3,0x4a,0x41,0xf8,0x2d,0x47,
};

static const uint8_t v1_rc4_ciphertext[47] = {
	0x38,0x3e,0x20,0x3e,0x2b,0x19,0x1c,0xd1,0xe4,0xe9,0xa9,0x26,
	0x60,0x49,0x0d,0x2a,0xb7,0x93,0xaa,0xe3,0x18,0x43,0x37,0x07,
	0xe6,0x74,0xf0,0x44,0x62,0xb0,0x4e,0x49,0x9a,0x4d,0x59,0xaf,
	0x38,0x21,0xb7,0xd9,0x9a,0xdf,0xa5,0xe3,0x33,0xeb,0x13,
};

static bool
known_v1_cipher(const char *algorithm, const uint8_t *ciphertext,
	size_t ciphertext_size, bool has_iv)
{
	static const uint8_t iv[8] = {0,1,2,3,4,5,6,7};
	ini_document_t *document = NULL;
	FILE *stream = tmpfile();
	int status;

	CHECK(stream != NULL);
	CHECK(fprintf(stream, "; Encrypted INI File, Algorithm: %s legacy salt\n",
	    algorithm) > 0);
	if (has_iv) {
		static const uint8_t iv_header[2] = {0, 8};

		CHECK(fwrite(iv_header, 1, sizeof(iv_header), stream)
		    == sizeof(iv_header));
		CHECK(fwrite(iv, 1, sizeof(iv), stream) == sizeof(iv));
	}
	CHECK(fwrite(ciphertext, 1, ciphertext_size, stream) == ciphertext_size);
	CHECK(fflush(stream) == 0);
	status = st_list_crypto_read(&document, stream, "password", 8, 2, NULL);
	if (status == IO_ERROR_NOT_SUPPORTED) {
		fclose(stream);
		return true;
	}
	CHECK(status == 0);
	CHECK(strcmp(ini_section_find(ini_document_find_section(document,
	    "Example"), "Password"), "secret") == 0);
	ini_document_free(&document);
	fclose(stream);
	return true;
}

static bool
test_v1_ciphers(void)
{
	return known_v1_cipher("3DES", v1_3des_ciphertext,
	    sizeof(v1_3des_ciphertext), true)
	    && known_v1_cipher("IDEA", v1_idea_ciphertext,
	    sizeof(v1_idea_ciphertext), true)
	    && known_v1_cipher("CAST", v1_cast_ciphertext,
	    sizeof(v1_cast_ciphertext), true)
	    && known_v1_cipher("RC2", v1_rc2_ciphertext,
	    sizeof(v1_rc2_ciphertext), true)
	    && known_v1_cipher("RC4", v1_rc4_ciphertext,
	    sizeof(v1_rc4_ciphertext), false);
}

static bool
test_legacy_decoders(void)
{
	static const uint8_t iv[8] = {0};
	static const uint8_t idea_key[16] = {
		0x00,0x01,0x00,0x02,0x00,0x03,0x00,0x04,
		0x00,0x05,0x00,0x06,0x00,0x07,0x00,0x08
	};
	static const uint8_t idea_expected[8] = {
		0x00,0x00,0x00,0x01,0x00,0x02,0x00,0x03
	};
	uint8_t idea[8] = {0x11,0xfb,0xed,0x2b,0x01,0x98,0x6d,0xe5};
	static const uint8_t rc2_key[16] = {
		0x88,0xbc,0xa9,0x0e,0x90,0x87,0x5a,0x7f,
		0x0f,0x79,0xc3,0x84,0x62,0x7b,0xaf,0xb2
	};
	static const uint8_t rc2_expected[8] = {0};
	uint8_t rc2[8] = {0x22,0x69,0x55,0x2a,0xb0,0xf8,0x5c,0xa6};

	CHECK(st_legacy_idea_decrypt_cbc(idea_key, sizeof(idea_key), iv,
	    idea, sizeof(idea)) == 0);
	CHECK(memcmp(idea, idea_expected, sizeof(idea)) == 0);
	CHECK(st_legacy_rc2_decrypt_cbc(rc2_key, sizeof(rc2_key), iv,
	    rc2, sizeof(rc2)) == 0);
	CHECK(memcmp(rc2, rc2_expected, sizeof(rc2)) == 0);
	return true;
}

int
main(void)
{
	if (!test_plain() || !round_trip(ST_LIST_CIPHER_AES, 128)
	    || !round_trip(ST_LIST_CIPHER_AES, 256)
	    || !round_trip(ST_LIST_CIPHER_AES, 0)
	    || !round_trip(ST_LIST_CIPHER_CHACHA20, 256)
	    || !round_trip(ST_LIST_CIPHER_CHACHA20, 0)
	    || !test_invalid() || !test_header_variants() || !test_known_files()
	    || !test_v1_ciphers() || !test_legacy_decoders())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
