/* Tests for strict binary hexadecimal conversion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_hex.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
		    __LINE__, #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	static const uint8_t known[] = {0x00, 0x01, 0xab, 0xcd, 0xef, 0xff};
	uint8_t all[256];
	uint8_t decoded[256];
	char encoded[sizeof(all) * 2 + 1];
	char unchanged[] = "unchanged";
	char empty[1] = {'x'};
	uint8_t byte = 0xa5;
	uint8_t digit = 0xff;

	CHECK(st_hex_digit_value('0', &digit) && digit == 0);
	CHECK(st_hex_digit_value('a', &digit) && digit == 10);
	CHECK(st_hex_digit_value('F', &digit) && digit == 15);
	CHECK(!st_hex_digit_value('g', &digit) && digit == 15);
	CHECK(!st_hex_digit_value('0', NULL));

	CHECK(st_hex_encode(encoded, sizeof(encoded), known, sizeof(known)));
	CHECK(strcmp(encoded, "0001abcdefff") == 0);
	memset(decoded, 0, sizeof(decoded));
	CHECK(st_hex_decode(decoded, sizeof(decoded), "0001ABCdefFF", 12));
	CHECK(memcmp(decoded, known, sizeof(known)) == 0);

	for (size_t i = 0; i < sizeof(all); ++i)
		all[i] = (uint8_t)i;
	CHECK(st_hex_encode(encoded, sizeof(encoded), all, sizeof(all)));
	CHECK(st_hex_decode(decoded, sizeof(decoded), encoded, strlen(encoded)));
	CHECK(memcmp(decoded, all, sizeof(all)) == 0);

	CHECK(st_hex_encode(empty, sizeof(empty), "", 0));
	CHECK(empty[0] == '\0');
	CHECK(st_hex_decode(decoded, sizeof(decoded), "", 0));

	CHECK(!st_hex_encode(unchanged, sizeof(unchanged), known, sizeof(known)));
	CHECK(strcmp(unchanged, "unchanged") == 0);
	CHECK(!st_hex_encode(unchanged, sizeof(unchanged), &byte, SIZE_MAX));
	CHECK(strcmp(unchanged, "unchanged") == 0);

	memset(decoded, 0xa5, sizeof(decoded));
	CHECK(!st_hex_decode(decoded, sizeof(decoded), "abc", 3));
	CHECK(decoded[0] == 0xa5);
	CHECK(!st_hex_decode(decoded, sizeof(decoded), "01xz", 4));
	CHECK(decoded[0] == 0xa5);
	CHECK(!st_hex_decode(decoded, 1, "0011", 4));
	CHECK(decoded[0] == 0xa5);
	CHECK(!st_hex_encode(NULL, 0, known, sizeof(known)));
	CHECK(!st_hex_encode(encoded, sizeof(encoded), NULL, sizeof(known)));
	CHECK(!st_hex_decode(NULL, 0, "00", 2));
	CHECK(!st_hex_decode(decoded, sizeof(decoded), NULL, 2));
	return 0;
}
