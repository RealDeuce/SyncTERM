/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "st_md5.h"
#include <stdio.h>
#include <string.h>

static int
check(const void *bytes, size_t size, size_t chunk, const char *expected)
{
	static const char digits[] = "0123456789abcdef";
	struct st_md5 context;
	uint8_t digest[16];
	char text[33];

	st_md5_init(&context);
	for (size_t position = 0; position < size;) {
		size_t count = size - position < chunk ? size - position : chunk;
		st_md5_update(&context, (const uint8_t *)bytes + position, count);
		position += count;
	}
	st_md5_update(&context, NULL, 0);
	st_md5_finish(&context, digest);
	for (size_t i = 0; i < 16; ++i) {
		text[i * 2] = digits[digest[i] >> 4];
		text[i * 2 + 1] = digits[digest[i] & 15];
	}
	text[32] = 0;
	if (strcmp(text, expected) != 0) {
		fprintf(stderr, "MD5: expected %s, got %s (chunk %zu)\n", expected, text, chunk);
		return 1;
	}
	return 0;
}

int
main(void)
{
	static const struct {const char *text, *digest;} vectors[] = {
		{"", "d41d8cd98f00b204e9800998ecf8427e"},
		{"a", "0cc175b9c0f1b6a831c399e269772661"},
		{"abc", "900150983cd24fb0d6963f7d28e17f72"},
		{"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
		{"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
		{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"},
		{"12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a"},
	};
	for (size_t chunk = 1; chunk < 90; ++chunk)
		for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i)
			if (check(vectors[i].text, strlen(vectors[i].text), chunk, vectors[i].digest))
				return 1;
	return 0;
}
