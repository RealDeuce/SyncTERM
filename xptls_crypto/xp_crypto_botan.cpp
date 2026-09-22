/* Botan provider for the xptls crypto primitives. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "xp_cipher.h"
#include "xp_kdf.h"

#include <botan/auto_rng.h>
#include <botan/cipher_mode.h>
#include <botan/hash.h>
#include <botan/pwdhash.h>
#include <botan/secmem.h>
#include <botan/stream_cipher.h>
#include <botan/version.h>

#include <cstring>
#include <limits>
#include <memory>
#include <string>

static bool
legacy_algorithm(enum xp_cipher_algorithm algorithm)
{
	return algorithm == XP_CIPHER_3DES || algorithm == XP_CIPHER_CAST128
	    || algorithm == XP_CIPHER_RC4;
}

static std::string
cipher_name(enum xp_cipher_algorithm algorithm, size_t key_size)
{
	switch (algorithm) {
	case XP_CIPHER_AES:
		if (key_size != 16 && key_size != 24 && key_size != 32)
			return "";
		return "AES-" + std::to_string(key_size * 8) + "/CBC/NoPadding";
	case XP_CIPHER_CHACHA20:
		return key_size == 32 ? "ChaCha(20)" : "";
	case XP_CIPHER_3DES:
		return key_size == 24 ? "TripleDES/CBC/NoPadding" : "";
	case XP_CIPHER_CAST128:
		return key_size >= 5 && key_size <= 16
		    ? "CAST-128/CBC/NoPadding" : "";
	case XP_CIPHER_RC4:
		return key_size > 0 && key_size <= 32 ? "RC4" : "";
	}
	return "";
}

extern "C" const char *xp_crypto_provider_name(void) { return "Botan"; }

extern "C" const char *
xp_crypto_provider_version(void)
{
	static const std::string version = Botan::version_string();
	return version.c_str();
}

extern "C" int
xp_crypto_random(void *result, size_t size)
{
	if (result == nullptr && size != 0)
		return XP_CRYPTO_ERR_INVALID;
	if (size == 0)
		return XP_CRYPTO_OK;
	try {
		Botan::AutoSeeded_RNG random;
		random.randomize(static_cast<uint8_t *>(result), size);
		return XP_CRYPTO_OK;
	}
	catch (...) {
		return XP_CRYPTO_ERR;
	}
}

extern "C" bool
xp_cipher_supported(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction)
{
	if (direction != XP_CIPHER_ENCRYPT && direction != XP_CIPHER_DECRYPT)
		return false;
	if (direction == XP_CIPHER_ENCRYPT && legacy_algorithm(algorithm))
		return false;
	size_t key_size = algorithm == XP_CIPHER_3DES ? 24
	    : algorithm == XP_CIPHER_CAST128 || algorithm == XP_CIPHER_RC4
	    ? 16 : 32;
	std::string name = cipher_name(algorithm, key_size);
	if (name.empty())
		return false;
	try {
		if (algorithm == XP_CIPHER_CHACHA20 || algorithm == XP_CIPHER_RC4)
			return Botan::StreamCipher::create(name) != nullptr;
		return Botan::Cipher_Mode::create(name,
		    direction == XP_CIPHER_ENCRYPT ? Botan::Cipher_Dir::Encryption
		    : Botan::Cipher_Dir::Decryption) != nullptr;
	}
	catch (...) {
		return false;
	}
}

extern "C" int
xp_cipher_crypt(enum xp_cipher_algorithm algorithm,
	enum xp_cipher_direction direction, const void *key, size_t key_size,
	const void *iv, size_t iv_size, uint32_t initial_counter,
	const void *input, size_t input_size, void *output, size_t output_size)
{
	if ((direction != XP_CIPHER_ENCRYPT && direction != XP_CIPHER_DECRYPT)
	    || key == nullptr || (iv == nullptr && iv_size != 0)
	    || (input == nullptr && input_size != 0)
	    || (output == nullptr && input_size != 0) || output_size < input_size)
		return XP_CRYPTO_ERR_INVALID;
	if ((algorithm == XP_CIPHER_CHACHA20 && iv_size != 12)
	    || (algorithm == XP_CIPHER_RC4 && iv_size != 0)
	    || (algorithm == XP_CIPHER_AES && iv_size != 16)
	    || ((algorithm == XP_CIPHER_3DES || algorithm == XP_CIPHER_CAST128)
	    && iv_size != 8))
		return XP_CRYPTO_ERR_POLICY;
	std::string name = cipher_name(algorithm, key_size);
	if (name.empty())
		return XP_CRYPTO_ERR_POLICY;
	try {
		const auto *key_bytes = static_cast<const uint8_t *>(key);
		const auto *input_bytes = static_cast<const uint8_t *>(input);
		if (algorithm == XP_CIPHER_CHACHA20 || algorithm == XP_CIPHER_RC4) {
			auto cipher = Botan::StreamCipher::create(name);
			if (!cipher)
				return XP_CRYPTO_ERR_UNSUPPORTED;
			cipher->set_key(key_bytes, key_size);
			if (iv_size != 0)
				cipher->set_iv(static_cast<const uint8_t *>(iv), iv_size);
			if (algorithm == XP_CIPHER_CHACHA20)
				cipher->seek(static_cast<uint64_t>(initial_counter) * 64);
			if (input_size != 0)
				cipher->cipher(input_bytes, static_cast<uint8_t *>(output),
				    input_size);
			return XP_CRYPTO_OK;
		}
		auto cipher = Botan::Cipher_Mode::create(name,
		    direction == XP_CIPHER_ENCRYPT ? Botan::Cipher_Dir::Encryption
		    : Botan::Cipher_Dir::Decryption);
		if (!cipher)
			return XP_CRYPTO_ERR_UNSUPPORTED;
		cipher->set_key(key_bytes, key_size);
		cipher->start(static_cast<const uint8_t *>(iv), iv_size);
		Botan::secure_vector<uint8_t> buffer(input_size);
		if (input_size != 0)
			std::memcpy(buffer.data(), input_bytes, input_size);
		cipher->finish(buffer);
		if (buffer.size() != input_size)
			return XP_CRYPTO_ERR;
		if (input_size != 0)
			std::memcpy(output, buffer.data(), input_size);
		return XP_CRYPTO_OK;
	}
	catch (...) {
		return XP_CRYPTO_ERR;
	}
}

static const char *
digest_name(enum xp_digest_algorithm digest)
{
	switch (digest) {
	case XP_DIGEST_MD5: return "MD5";
	case XP_DIGEST_SHA1: return "SHA-1";
	case XP_DIGEST_SHA256: return "SHA-256";
	case XP_DIGEST_SHA384: return "SHA-384";
	case XP_DIGEST_SHA512: return "SHA-512";
	}
	return nullptr;
}

extern "C" int
xp_digest(enum xp_digest_algorithm digest, const void *input,
	size_t input_size, void *result, size_t result_size)
{
	const char *name = digest_name(digest);
	if ((input == nullptr && input_size != 0) || result == nullptr
	    || name == nullptr || result_size != xp_digest_size(digest))
		return XP_CRYPTO_ERR_INVALID;
	try {
		auto hash = Botan::HashFunction::create_or_throw(name);
		hash->update(static_cast<const uint8_t *>(input), input_size);
		hash->final(static_cast<uint8_t *>(result));
		return XP_CRYPTO_OK;
	}
	catch (...) {
		return XP_CRYPTO_ERR;
	}
}

extern "C" size_t
xp_digest_size(enum xp_digest_algorithm digest)
{
	return digest == XP_DIGEST_MD5 ? 16
	    : digest == XP_DIGEST_SHA1 ? 20
	    : digest == XP_DIGEST_SHA256 ? 32
	    : digest == XP_DIGEST_SHA384 ? 48
	    : digest == XP_DIGEST_SHA512 ? 64 : 0;
}

extern "C" int
xp_kdf_pbkdf2(enum xp_digest_algorithm digest,
	const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t iterations,
	void *result, size_t result_size)
{
	const char *digest_value = digest == XP_DIGEST_SHA256
	    || digest == XP_DIGEST_SHA384 || digest == XP_DIGEST_SHA512
	    ? digest_name(digest) : nullptr;
	if ((password == nullptr && password_size != 0)
	    || (salt == nullptr && salt_size != 0)
	    || (result == nullptr && result_size != 0) || digest_value == nullptr
	    || iterations == 0 || iterations > std::numeric_limits<size_t>::max())
		return XP_CRYPTO_ERR_INVALID;
	try {
		auto family = Botan::PasswordHashFamily::create_or_throw(
		    std::string("PBKDF2(HMAC(") + digest_value + "))");
		auto hash = family->from_iterations(static_cast<size_t>(iterations));
		hash->derive_key(static_cast<uint8_t *>(result), result_size,
		    static_cast<const char *>(password), password_size,
		    static_cast<const uint8_t *>(salt), salt_size);
		return XP_CRYPTO_OK;
	}
	catch (...) {
		return XP_CRYPTO_ERR;
	}
}

extern "C" int
xp_kdf_scrypt(const void *password, size_t password_size,
	const void *salt, size_t salt_size, uint64_t n, uint32_t r, uint32_t p,
	void *result, size_t result_size)
{
	if ((password == nullptr && password_size != 0)
	    || (salt == nullptr && salt_size != 0)
	    || (result == nullptr && result_size != 0)
	    || !xp_kdf_scrypt_parameters_valid(n, r, p)
	    || n > std::numeric_limits<size_t>::max())
		return XP_CRYPTO_ERR_INVALID;
	try {
		auto family = Botan::PasswordHashFamily::create_or_throw("Scrypt");
		auto hash = family->from_params(static_cast<size_t>(n), r, p);
		hash->derive_key(static_cast<uint8_t *>(result), result_size,
		    static_cast<const char *>(password), password_size,
		    static_cast<const uint8_t *>(salt), salt_size);
		return XP_CRYPTO_OK;
	}
	catch (...) {
		return XP_CRYPTO_ERR;
	}
}
