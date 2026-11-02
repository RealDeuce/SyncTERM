/* Optional encryption for SyncTERM personal BBS-list INI documents. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_list_crypto.h"

#include "file_map.h"
#include "io_error.h"
#include "list_crypto_internal.h"
#include "st_hex.h"
#include "st_cipher.h"
#include "st_crypto.h"
#include "st_kdf.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define V1_PREFIX "; Encrypted INI File, Algorithm: "
#define V2_PREFIX "; Encrypted INI File v2, Algorithm: "
#define MAX_HEADER_SIZE 4096
#define MAX_SALT_SIZE 64
#define V2_SALT_SIZE 16

struct parsed_header {
	struct st_list_crypto_info info;
	uint8_t salt[MAX_SALT_SIZE];
	size_t salt_size;
	size_t payload_offset;
};

bool
st_list_crypto_encryption_available(void)
{
	return strcmp(st_crypto_provider_name(), "none") != 0;
}

static bool
span_equal(const char *value, size_t size, const char *expected)
{
	return strlen(expected) == size && memcmp(value, expected, size) == 0;
}

static bool
header_space(char value)
{
	return value == ' ' || value == '\t' || value == '\f' || value == '\v';
}

static bool
parse_uint64_span(const char *text, size_t size, uint64_t *result)
{
	uint64_t value = 0;

	if (size == 0)
		return false;
	for (size_t i = 0; i < size; i++) {
		unsigned digit;

		if (text[i] < '0' || text[i] > '9')
			return false;
		digit = (unsigned)(text[i] - '0');
		if (value > (UINT64_MAX - digit) / 10)
			return false;
		value = value * 10 + digit;
	}
	*result = value;
	return true;
}

static enum st_list_cipher
parse_cipher(const char *text, size_t size)
{
	if (span_equal(text, size, "3DES")) return ST_LIST_CIPHER_3DES;
	if (span_equal(text, size, "IDEA")) return ST_LIST_CIPHER_IDEA;
	if (span_equal(text, size, "CAST")) return ST_LIST_CIPHER_CAST;
	if (span_equal(text, size, "RC2")) return ST_LIST_CIPHER_RC2;
	if (span_equal(text, size, "RC4")) return ST_LIST_CIPHER_RC4;
	if (span_equal(text, size, "AES")) return ST_LIST_CIPHER_AES;
	if (span_equal(text, size, "ChaCha20")) return ST_LIST_CIPHER_CHACHA20;
	return ST_LIST_CIPHER_NONE;
}

static const char *
cipher_name(enum st_list_cipher cipher)
{
	switch (cipher) {
	case ST_LIST_CIPHER_3DES: return "3DES";
	case ST_LIST_CIPHER_IDEA: return "IDEA";
	case ST_LIST_CIPHER_CAST: return "CAST";
	case ST_LIST_CIPHER_RC2: return "RC2";
	case ST_LIST_CIPHER_RC4: return "RC4";
	case ST_LIST_CIPHER_AES: return "AES";
	case ST_LIST_CIPHER_CHACHA20: return "ChaCha20";
	case ST_LIST_CIPHER_NONE: return "NONE";
	}
	return NULL;
}

static uint32_t
default_key_bits(enum st_list_cipher cipher)
{
	if (cipher == ST_LIST_CIPHER_3DES)
		return 192;
	if (cipher == ST_LIST_CIPHER_AES || cipher == ST_LIST_CIPHER_CHACHA20)
		return 256;
	return 128;
}

static bool
valid_key_bits(enum st_list_cipher cipher, uint32_t bits)
{
	if (bits == 0 || (bits & 7U) != 0 || bits > 1024)
		return false;
	switch (cipher) {
	case ST_LIST_CIPHER_AES: return bits == 128 || bits == 192 || bits == 256;
	case ST_LIST_CIPHER_CHACHA20: return bits == 256;
	case ST_LIST_CIPHER_3DES: return bits == 192;
	case ST_LIST_CIPHER_IDEA: return bits == 128;
	case ST_LIST_CIPHER_CAST: return bits >= 40 && bits <= 128;
	case ST_LIST_CIPHER_RC2: return bits <= 256;
	case ST_LIST_CIPHER_RC4: return bits <= 256;
	case ST_LIST_CIPHER_NONE: return false;
	}
	return false;
}

static bool
scrypt_fits(uint8_t cost_log2, uint32_t r, uint32_t p)
{
	if (cost_log2 < 8 || cost_log2 >= 64)
		return false;
	return st_kdf_scrypt_parameters_valid(UINT64_C(1) << cost_log2, r, p);
}

static bool
parse_scrypt(const char *text, size_t size, struct st_list_crypto_info *info)
{
	static const char prefix[] = "scrypt-N";
	size_t offset = sizeof(prefix) - 1;
	uint64_t value;

	if (size < offset || memcmp(text, prefix, offset) != 0)
		return false;
	size_t end = offset;
	while (end < size && text[end] >= '0' && text[end] <= '9')
		end++;
	if (!parse_uint64_span(text + offset, end - offset, &value) || value > UINT8_MAX)
		return false;
	info->kdf = ST_LIST_KDF_SCRYPT;
	info->cost_log2 = (uint8_t)value;
	info->block_size = 8;
	info->parallelism = 1;
	offset = end;
	while (offset < size) {
		char tag;

		if (text[offset++] != '-' || offset >= size)
			return false;
		tag = text[offset++];
		end = offset;
		while (end < size && text[end] >= '0' && text[end] <= '9')
			end++;
		if (!parse_uint64_span(text + offset, end - offset, &value)
		    || value > UINT32_MAX)
			return false;
		if (tag == 'r')
			info->block_size = (uint32_t)value;
		else if (tag == 'p')
			info->parallelism = (uint32_t)value;
		else
			return false;
		offset = end;
	}
	return scrypt_fits(info->cost_log2, info->block_size,
	    info->parallelism);
}

static bool
parse_kdf(const char *text, size_t size, struct st_list_crypto_info *info)
{
	static const char prefix[] = "PBKDF2-SHA256-i";
	uint64_t iterations;

	if (parse_scrypt(text, size, info))
		return true;
	if (size <= sizeof(prefix) - 1
	    || memcmp(text, prefix, sizeof(prefix) - 1) != 0
	    || !parse_uint64_span(text + sizeof(prefix) - 1,
	    size - (sizeof(prefix) - 1), &iterations)
	    || iterations == 0 || iterations > INT_MAX)
		return false;
	info->kdf = ST_LIST_KDF_PBKDF2_SHA256;
	info->iterations = iterations;
	return true;
}

static int
parse_header(const void *input, size_t input_size, struct parsed_header *header)
{
	const char *data = input;
	const char *newline;
	size_t line_size;
	const char *value;
	size_t value_size;
	bool v2;
	const char *separator;
	const char *dash;
	uint64_t bits;

	memset(header, 0, sizeof(*header));
	header->info.format = ST_LIST_CRYPTO_PLAIN;
	if (input_size == 0)
		return 0;
	newline = memchr(data, '\n', input_size);
	line_size = newline == NULL ? input_size : (size_t)(newline - data);
	if (line_size != 0 && data[line_size - 1] == '\r')
		line_size--;
	if (line_size >= strlen(V2_PREFIX)
	    && memcmp(data, V2_PREFIX, strlen(V2_PREFIX)) == 0) {
		if (line_size > MAX_HEADER_SIZE)
			return IO_ERROR_INVALID_FORMAT;
		value = data + strlen(V2_PREFIX);
		value_size = line_size - strlen(V2_PREFIX);
		v2 = true;
	}
	else if (line_size >= strlen(V1_PREFIX)
	    && memcmp(data, V1_PREFIX, strlen(V1_PREFIX)) == 0) {
		if (line_size > MAX_HEADER_SIZE)
			return IO_ERROR_INVALID_FORMAT;
		value = data + strlen(V1_PREFIX);
		value_size = line_size - strlen(V1_PREFIX);
		v2 = false;
	}
	else
		return 0;
	header->info.format = v2 ? ST_LIST_CRYPTO_V2 : ST_LIST_CRYPTO_V1;
	separator = memchr(value, v2 ? ',' : ' ', value_size);
	if (separator == NULL)
		return IO_ERROR_INVALID_FORMAT;
	dash = memchr(value, '-', (size_t)(separator - value));
	header->info.cipher = parse_cipher(value,
	    dash == NULL ? (size_t)(separator - value) : (size_t)(dash - value));
	if (header->info.cipher == ST_LIST_CIPHER_NONE)
		return IO_ERROR_INVALID_FORMAT;
	if (dash == NULL)
		header->info.key_bits = default_key_bits(header->info.cipher);
	else {
		if (!parse_uint64_span(dash + 1, (size_t)(separator - dash - 1), &bits)
		    || bits > UINT32_MAX)
			return IO_ERROR_INVALID_FORMAT;
		header->info.key_bits = (uint32_t)bits;
	}
	if (!valid_key_bits(header->info.cipher, header->info.key_bits))
		return IO_ERROR_INVALID_FORMAT;
	if (v2) {
		static const char kdf_label[] = ", KDF: ";
		static const char salt_label[] = ", Salt: ";
		const char *kdf = separator;
		const char *salt;
		const char *end = value + value_size;

		if ((size_t)(end - kdf) < sizeof(kdf_label) - 1
		    || memcmp(kdf, kdf_label, sizeof(kdf_label) - 1) != 0)
			return IO_ERROR_INVALID_FORMAT;
		kdf += sizeof(kdf_label) - 1;
		salt = NULL;
		for (const char *scan = kdf;
		    (size_t)(end - scan) >= sizeof(salt_label) - 1; scan++) {
			if (memcmp(scan, salt_label, sizeof(salt_label) - 1) == 0) {
				salt = scan;
				break;
			}
		}
		if (salt == NULL)
			return IO_ERROR_INVALID_FORMAT;
		const char *kdf_end = salt;
		while (kdf < kdf_end && header_space(*kdf))
			kdf++;
		while (kdf_end > kdf && header_space(kdf_end[-1]))
			kdf_end--;
		if (!parse_kdf(kdf, (size_t)(kdf_end - kdf), &header->info))
			return IO_ERROR_INVALID_FORMAT;
		salt += sizeof(salt_label) - 1;
		while (end > salt && header_space(end[-1]))
			end--;
		size_t salt_text_size = (size_t)(end - salt);

		if (salt_text_size == 0
		    || !st_hex_decode(header->salt, sizeof(header->salt), salt,
		    salt_text_size))
			return IO_ERROR_INVALID_FORMAT;
		header->salt_size = salt_text_size / 2;
	}
	else {
		const char *salt = separator + 1;
		const char *end = value + value_size;

		while (end > salt && header_space(end[-1]))
			end--;
		size_t salt_size = (size_t)(end - salt);

		if (salt_size == 0 || salt_size > sizeof(header->salt))
			return IO_ERROR_INVALID_FORMAT;
		for (size_t i = 0; i < salt_size; i++) {
			unsigned char character = (unsigned char)salt[i];

			if (character < 0x20 || character > 0x7e)
				return IO_ERROR_INVALID_FORMAT;
		}
		memcpy(header->salt, salt, salt_size);
		header->salt_size = salt_size;
		header->info.kdf = ST_LIST_KDF_PBKDF2_SHA256;
	}
	header->payload_offset = newline == NULL ? input_size
	    : (size_t)(newline - data) + 1;
	header->info.rewrite_recommended = !v2
	    || header->info.kdf != ST_LIST_KDF_SCRYPT
	    || (header->info.cipher != ST_LIST_CIPHER_AES
	    && header->info.cipher != ST_LIST_CIPHER_CHACHA20);
	return 0;
}

static int
map_stream(file_map_t **map, FILE *stream)
{
	return stream == NULL ? IO_ERROR_INVALID_ARGUMENT
	    : file_map_open_stream(map, stream);
}

int
st_list_crypto_inspect(FILE *stream, struct st_list_crypto_info *info)
{
	file_map_t *map = NULL;
	struct parsed_header header;
	int error;

	if (info == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	error = map_stream(&map, stream);
	if (error != 0)
		return error;
	error = parse_header(file_map_data(map), file_map_size(map), &header);
	file_map_close(map);
	if (error != 0)
		return error;
	*info = header.info;
	return 0;
}

static size_t
iv_size(enum st_list_cipher cipher)
{
	switch (cipher) {
	case ST_LIST_CIPHER_AES: return 16;
	case ST_LIST_CIPHER_CHACHA20: return 16;
	case ST_LIST_CIPHER_3DES:
	case ST_LIST_CIPHER_IDEA:
	case ST_LIST_CIPHER_CAST:
	case ST_LIST_CIPHER_RC2: return 8;
	case ST_LIST_CIPHER_RC4: return 0;
	case ST_LIST_CIPHER_NONE: return SIZE_MAX;
	}
	return SIZE_MAX;
}

static size_t
block_size(enum st_list_cipher cipher)
{
	if (cipher == ST_LIST_CIPHER_AES)
		return 16;
	if (cipher == ST_LIST_CIPHER_CHACHA20 || cipher == ST_LIST_CIPHER_RC4)
		return 1;
	return 8;
}

static enum st_cipher_algorithm
xp_algorithm(enum st_list_cipher cipher)
{
	switch (cipher) {
	case ST_LIST_CIPHER_AES: return ST_CIPHER_AES;
	case ST_LIST_CIPHER_CHACHA20: return ST_CIPHER_CHACHA20;
	case ST_LIST_CIPHER_3DES: return ST_CIPHER_3DES;
	case ST_LIST_CIPHER_CAST: return ST_CIPHER_CAST128;
	case ST_LIST_CIPHER_RC4: return ST_CIPHER_RC4;
	default: return 0;
	}
}

static int
crypto_result(int status, bool decrypting)
{
	if (status == ST_CRYPTO_OK)
		return 0;
	if (status == ST_CRYPTO_ERR_DISABLED
	    || status == ST_CRYPTO_ERR_UNSUPPORTED)
		return IO_ERROR_NOT_SUPPORTED;
	return decrypting ? IO_ERROR_DECRYPTION_FAILED : IO_ERROR_IO;
}

static int
derive_key(const struct st_list_crypto_info *info, const uint8_t *salt,
	size_t salt_size, const void *password, size_t password_size,
	uint8_t *key, size_t key_size)
{
	int status;

	if (info->kdf == ST_LIST_KDF_PBKDF2_SHA256) {
		status = st_kdf_pbkdf2(ST_DIGEST_SHA256, password, password_size,
		    salt, salt_size, info->iterations, key, key_size);
	}
	else {
		status = st_kdf_scrypt(password, password_size, salt, salt_size,
		    UINT64_C(1) << info->cost_log2, info->block_size,
		    info->parallelism, key, key_size);
	}
	return status;
}

static int
crypt_buffer(enum st_list_cipher cipher, enum st_cipher_direction direction,
	const uint8_t *key, size_t key_size, const uint8_t *iv,
	uint8_t *data, size_t size)
{
	if (cipher == ST_LIST_CIPHER_IDEA) {
		return direction == ST_CIPHER_DECRYPT
		    && st_legacy_idea_decrypt_cbc(key, key_size, iv, data, size) == 0
		    ? 0 : IO_ERROR_NOT_SUPPORTED;
	}
	if (cipher == ST_LIST_CIPHER_RC2) {
		return direction == ST_CIPHER_DECRYPT
		    && st_legacy_rc2_decrypt_cbc(key, key_size, iv, data, size) == 0
		    ? 0 : IO_ERROR_NOT_SUPPORTED;
	}
	enum st_cipher_algorithm algorithm = xp_algorithm(cipher);
	const uint8_t *native_iv = iv;
	size_t native_iv_size = iv_size(cipher);
	uint32_t counter = 0;

	if (cipher == ST_LIST_CIPHER_CHACHA20) {
		counter = (uint32_t)iv[0] | (uint32_t)iv[1] << 8
		    | (uint32_t)iv[2] << 16 | (uint32_t)iv[3] << 24;
		native_iv += 4;
		native_iv_size = 12;
	}
	int status = st_cipher_crypt(algorithm, direction, key, key_size,
	    native_iv, native_iv_size, counter, data, size, data, size);
	return crypto_result(status, direction == ST_CIPHER_DECRYPT);
}

int
st_list_crypto_read(ini_document_t **result, FILE *stream,
	const void *password, size_t password_size, uint64_t v1_iterations,
	struct st_list_crypto_info *info)
{
	file_map_t *map = NULL;
	struct parsed_header header;
	ini_document_t *document = NULL;
	uint8_t key[128];
	uint8_t *plain = NULL;
	const uint8_t *data;
	size_t size;
	size_t expected_iv_size;
	size_t cipher_size;
	int error;

	if (result == NULL || (password == NULL && password_size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	error = map_stream(&map, stream);
	if (error != 0)
		return error;
	data = file_map_data(map);
	size = file_map_size(map);
	error = parse_header(data, size, &header);
	if (error != 0)
		goto done;
	if (header.info.format == ST_LIST_CRYPTO_PLAIN) {
		error = ini_document_parse(&document, data, size);
		goto parsed;
	}
	if (password == NULL || password_size == 0) {
		error = IO_ERROR_PASSWORD_REQUIRED;
		goto done;
	}
	if (header.info.format == ST_LIST_CRYPTO_V1) {
		header.info.iterations = v1_iterations == 0 ? 50000 : v1_iterations;
		if (header.info.iterations > INT_MAX) {
			error = IO_ERROR_INVALID_FORMAT;
			goto done;
		}
	}
	expected_iv_size = iv_size(header.info.cipher);
	if (header.payload_offset > size || expected_iv_size == SIZE_MAX) {
		error = IO_ERROR_INVALID_FORMAT;
		goto done;
	}
	const uint8_t *iv = NULL;
	if (expected_iv_size != 0) {
		if (size - header.payload_offset < 2) {
			error = IO_ERROR_INVALID_FORMAT;
			goto done;
		}
		size_t encoded = ((size_t)(uint8_t)data[header.payload_offset] << 8)
		    | (uint8_t)data[header.payload_offset + 1];
		header.payload_offset += 2;
		if (encoded != expected_iv_size
		    || size - header.payload_offset < encoded) {
			error = IO_ERROR_INVALID_FORMAT;
			goto done;
		}
		iv = data + header.payload_offset;
		header.payload_offset += encoded;
	}
	cipher_size = size - header.payload_offset;
	if (cipher_size == 0 || (block_size(header.info.cipher) > 1
	    && cipher_size % block_size(header.info.cipher) != 0)) {
		error = IO_ERROR_INVALID_FORMAT;
		goto done;
	}
	if (cipher_size == SIZE_MAX) {
		error = IO_ERROR_OUT_OF_RANGE;
		goto done;
	}
	plain = malloc(cipher_size + 1);
	if (plain == NULL) {
		error = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(plain, data + header.payload_offset, cipher_size);
	size_t key_size = header.info.key_bits / 8;
	error = crypto_result(derive_key(&header.info, header.salt,
	    header.salt_size, password, password_size, key, key_size), true);
	if (error != 0)
		goto done;
	error = crypt_buffer(header.info.cipher, ST_CIPHER_DECRYPT, key,
	    key_size, iv, plain, cipher_size);
	if (error != 0)
		goto done;
	if (block_size(header.info.cipher) > 1) {
		while (cipher_size != 0 && plain[cipher_size - 1] == 0)
			cipher_size--;
	}
	plain[cipher_size] = 0;
	error = ini_document_parse(&document, plain, cipher_size);
	if (error != 0 && error != IO_ERROR_OUT_OF_MEMORY)
		error = IO_ERROR_DECRYPTION_FAILED;
	if (error != 0)
		goto done;
	ini_section_t *root = ini_document_root(document);
	if (!ini_section_has_key(root, "DecryptionCheck")
	    || !ini_section_get_bool(root, "DecryptionCheck", false)) {
		error = IO_ERROR_DECRYPTION_FAILED;
		goto done;
	}

parsed:
	error = 0;

done:
	st_crypto_clear(key, sizeof(key));
	if (plain != NULL) {
		st_crypto_clear(plain, size - header.payload_offset + 1);
		free(plain);
	}
	file_map_close(map);
	if (error == 0) {
		ini_document_free(result);
		*result = document;
		document = NULL;
		if (info != NULL)
			*info = header.info;
	}
	ini_document_free(&document);
	return error;
}

static int
document_for_write(ini_document_t **result, const ini_document_t *source,
	bool encrypted)
{
	char *serialized = NULL;
	size_t size = 0;
	int error = ini_document_serialize(source, &serialized, &size);

	if (error == 0)
		error = ini_document_parse(result, serialized, size);
	free(serialized);
	if (error != 0)
		return error;
	ini_section_t *root = ini_document_root(*result);
	if (encrypted)
		return ini_section_set_bool(root, "DecryptionCheck", true,
		    &ini_default_style);
	(void)ini_section_remove_key(root, "DecryptionCheck");
	return 0;
}

static int
write_all(FILE *stream, const void *data, size_t size)
{
	return size != 0 && fwrite(data, 1, size, stream) != size
	    ? IO_ERROR_IO : ferror(stream) ? IO_ERROR_IO : 0;
}

int
st_list_crypto_write(FILE *stream, const ini_document_t *document,
	const struct st_list_crypto_options *options,
	const void *password, size_t password_size)
{
	ini_document_t *copy = NULL;
	char *serialized = NULL;
	uint8_t salt[V2_SALT_SIZE];
	uint8_t iv[16];
	uint8_t key[32];
	uint8_t *ciphertext = NULL;
	size_t serialized_size = 0;
	size_t padded_size = 0;
	size_t cipher_iv_size;
	uint32_t key_bits;
	int error;

	if (stream == NULL || document == NULL || options == NULL
	    || (password == NULL && password_size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (options->cipher == ST_LIST_CIPHER_NONE) {
		error = document_for_write(&copy, document, false);
		if (error == 0)
			error = ini_document_write(copy, stream);
		ini_document_free(&copy);
		return error;
	}
	if (password == NULL || password_size == 0)
		return IO_ERROR_PASSWORD_REQUIRED;
	key_bits = options->key_bits == 0 ? 256 : options->key_bits;
	if ((options->cipher != ST_LIST_CIPHER_AES
	    && options->cipher != ST_LIST_CIPHER_CHACHA20)
	    || !valid_key_bits(options->cipher, key_bits)
	    || (options->cipher == ST_LIST_CIPHER_AES && key_bits == 192)
	    || !scrypt_fits(options->cost_log2, options->block_size,
	    options->parallelism))
		return IO_ERROR_INVALID_ARGUMENT;
	error = document_for_write(&copy, document, true);
	if (error == 0)
		error = ini_document_serialize(copy, &serialized, &serialized_size);
	ini_document_free(&copy);
	if (error != 0)
		goto done;
	padded_size = serialized_size;
	if (options->cipher == ST_LIST_CIPHER_AES) {
		if (padded_size > SIZE_MAX - 15) {
			error = IO_ERROR_OUT_OF_RANGE;
			goto done;
		}
		padded_size = (padded_size + 15) & ~(size_t)15;
	}
	ciphertext = calloc(padded_size == 0 ? 1 : padded_size, 1);
	if (ciphertext == NULL) {
		error = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy(ciphertext, serialized, serialized_size);
	int crypto_status = st_crypto_random(salt, sizeof(salt));
	if (crypto_status == ST_CRYPTO_OK)
		crypto_status = st_crypto_random(iv, sizeof(iv));
	if (crypto_status != ST_CRYPTO_OK) {
		error = crypto_result(crypto_status, false);
		goto done;
	}
	struct st_list_crypto_info derivation = {
		.kdf = ST_LIST_KDF_SCRYPT,
		.cost_log2 = options->cost_log2,
		.block_size = options->block_size,
		.parallelism = options->parallelism,
	};
	error = crypto_result(derive_key(&derivation, salt, sizeof(salt),
	    password, password_size, key, key_bits / 8), false);
	if (error != 0)
		goto done;
	cipher_iv_size = iv_size(options->cipher);
	error = crypt_buffer(options->cipher, ST_CIPHER_ENCRYPT, key,
	    key_bits / 8, iv, ciphertext, padded_size);
	if (error != 0)
		goto done;
	char salt_hex[V2_SALT_SIZE * 2 + 1];
	if (!st_hex_encode(salt_hex, sizeof(salt_hex), salt, sizeof(salt))) {
		error = IO_ERROR_OUT_OF_RANGE;
		goto done;
	}
	if (fprintf(stream, "%s%s-%" PRIu32 ", KDF: scrypt-N%" PRIu32
	    "-r%" PRIu32 "-p%" PRIu32 ", Salt: %s\n",
	    V2_PREFIX, cipher_name(options->cipher), key_bits,
	    (uint32_t)options->cost_log2, options->block_size,
	    options->parallelism,
	    salt_hex) < 0) {
		error = IO_ERROR_IO;
		goto done;
	}
	uint8_t iv_header[2] = {
		(uint8_t)(cipher_iv_size >> 8), (uint8_t)cipher_iv_size
	};
	error = write_all(stream, iv_header, sizeof(iv_header));
	if (error == 0)
		error = write_all(stream, iv, cipher_iv_size);
	if (error == 0)
		error = write_all(stream, ciphertext, padded_size);

done:
	if (serialized != NULL) {
		st_crypto_clear(serialized, serialized_size + 1);
		free(serialized);
	}
	if (ciphertext != NULL) {
		st_crypto_clear(ciphertext, padded_size);
		free(ciphertext);
	}
	st_crypto_clear(key, sizeof(key));
	st_crypto_clear(iv, sizeof(iv));
	st_crypto_clear(salt, sizeof(salt));
	return error;
}
