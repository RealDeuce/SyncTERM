/* IDEA-CBC decoder retained for Cryptlib-era personal lists. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "list_crypto_internal.h"

#include "xp_crypto.h"

#include <stdint.h>
#include <string.h>

static uint16_t
idea_multiply(uint16_t left, uint16_t right)
{
	uint32_t product;
	uint32_t high;
	uint32_t low;
	uint32_t result;

	if (left == 0)
		return (uint16_t)((UINT32_C(0x10001) - right) & 0xffff);
	if (right == 0)
		return (uint16_t)((UINT32_C(0x10001) - left) & 0xffff);
	product = (uint32_t)left * right;
	high = product >> 16;
	low = product & 0xffff;
	result = low >= high ? low - high : low - high + UINT32_C(0x10001);
	return (uint16_t)(result & 0xffff);
}

static uint16_t
idea_inverse(uint16_t value)
{
	int64_t current = 1;
	int64_t previous = 0;
	int64_t left = value;
	int64_t right = 0x10001;

	if (value <= 1)
		return value;
	while (left != 0) {
		int64_t quotient = right / left;
		int64_t remainder = right - quotient * left;
		int64_t coefficient = previous - quotient * current;

		right = left;
		left = remainder;
		previous = current;
		current = coefficient;
	}
	if (previous < 0)
		previous += 0x10001;
	return (uint16_t)(previous & 0xffff);
}

static void
idea_expand_key(const uint8_t key[16], uint16_t expanded[52])
{
	uint16_t words[8];
	int output = 0;

	for (int i = 0; i < 8; i++)
		words[i] = (uint16_t)((uint16_t)key[i * 2] << 8)
		    | key[i * 2 + 1];
	for (;;) {
		for (int i = 0; i < 8 && output < 52; i++)
			expanded[output++] = words[i];
		if (output >= 52)
			break;
		uint16_t rotated[8];
		for (int i = 0; i < 8; i++)
			rotated[i] = words[(i + 1) & 7];
		for (int i = 0; i < 8; i++) {
			words[i] = (uint16_t)((rotated[i] << 9)
			    | (rotated[(i + 1) & 7] >> 7));
		}
	}
	xp_crypto_clear(words, sizeof(words));
}

static void
idea_invert_key(const uint16_t encryption[52], uint16_t decryption[52])
{
	uint16_t first;
	uint16_t second;
	uint16_t third;
	int output = 52;
	int input = 0;

#define PUT(value) (decryption[--output] = (uint16_t)(value))
	first = idea_inverse(encryption[input++]);
	second = (uint16_t)-encryption[input++];
	third = (uint16_t)-encryption[input++];
	PUT(idea_inverse(encryption[input++]));
	PUT(third);
	PUT(second);
	PUT(first);
	for (int round = 0; round < 7; round++) {
		first = encryption[input++];
		PUT(encryption[input++]);
		PUT(first);
		first = idea_inverse(encryption[input++]);
		second = (uint16_t)-encryption[input++];
		third = (uint16_t)-encryption[input++];
		PUT(idea_inverse(encryption[input++]));
		PUT(second);
		PUT(third);
		PUT(first);
	}
	first = encryption[input++];
	PUT(encryption[input++]);
	PUT(first);
	first = idea_inverse(encryption[input++]);
	second = (uint16_t)-encryption[input++];
	third = (uint16_t)-encryption[input++];
	PUT(idea_inverse(encryption[input++]));
	PUT(third);
	PUT(second);
	PUT(first);
#undef PUT
}

static void
idea_decrypt_block(const uint16_t key[52], uint8_t data[8])
{
	uint16_t first = (uint16_t)((uint16_t)data[0] << 8) | data[1];
	uint16_t second = (uint16_t)((uint16_t)data[2] << 8) | data[3];
	uint16_t third = (uint16_t)((uint16_t)data[4] << 8) | data[5];
	uint16_t fourth = (uint16_t)((uint16_t)data[6] << 8) | data[7];
	const uint16_t *subkey = key;

	for (int round = 0; round < 8; round++) {
		uint16_t saved_second;
		uint16_t saved_third;

		first = idea_multiply(first, *subkey++);
		second = (uint16_t)(second + *subkey++);
		third = (uint16_t)(third + *subkey++);
		fourth = idea_multiply(fourth, *subkey++);
		saved_third = third;
		third = idea_multiply((uint16_t)(third ^ first), *subkey++);
		saved_second = second;
		second = idea_multiply((uint16_t)((second ^ fourth) + third),
		    *subkey++);
		third = (uint16_t)(third + second);
		first = (uint16_t)(first ^ second);
		fourth = (uint16_t)(fourth ^ third);
		second = (uint16_t)(second ^ saved_third);
		third = (uint16_t)(third ^ saved_second);
	}
	first = idea_multiply(first, *subkey++);
	third = (uint16_t)(third + *subkey++);
	second = (uint16_t)(second + *subkey++);
	fourth = idea_multiply(fourth, *subkey);
	data[0] = (uint8_t)(first >> 8);
	data[1] = (uint8_t)first;
	data[2] = (uint8_t)(third >> 8);
	data[3] = (uint8_t)third;
	data[4] = (uint8_t)(second >> 8);
	data[5] = (uint8_t)second;
	data[6] = (uint8_t)(fourth >> 8);
	data[7] = (uint8_t)fourth;
}

int
st_legacy_idea_decrypt_cbc(const void *key, size_t key_size,
	const void *iv, void *data, size_t size)
{
	uint16_t encryption[52];
	uint16_t decryption[52];
	uint8_t previous[8];
	uint8_t saved[8];
	uint8_t *bytes = data;

	if (key == NULL || iv == NULL || data == NULL || key_size != 16
	    || size % 8 != 0)
		return -1;
	idea_expand_key(key, encryption);
	idea_invert_key(encryption, decryption);
	memcpy(previous, iv, sizeof(previous));
	for (size_t offset = 0; offset < size; offset += 8) {
		memcpy(saved, bytes + offset, sizeof(saved));
		idea_decrypt_block(decryption, bytes + offset);
		for (size_t i = 0; i < sizeof(previous); i++)
			bytes[offset + i] ^= previous[i];
		memcpy(previous, saved, sizeof(previous));
	}
	xp_crypto_clear(encryption, sizeof(encryption));
	xp_crypto_clear(decryption, sizeof(decryption));
	xp_crypto_clear(previous, sizeof(previous));
	xp_crypto_clear(saved, sizeof(saved));
	return 0;
}
