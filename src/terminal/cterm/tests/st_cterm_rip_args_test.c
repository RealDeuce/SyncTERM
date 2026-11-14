/* Independent RIP numeric spelling and WORD-boundary expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_rip_args.h"
#include <stdio.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)

int
st_cterm_rip_args_test_main(void)
{
	static const struct {
		const char *text;
		size_t width, used;
		int value;
	} good[] = {
		{"-A", 2, 2, -10}, {"-a", 2, 2, -10}, {"A-", 2, 2, 10},
		{"0-", 2, 2, 0}, {"-", 1, 1, 0}, {"-", 2, 1, 0},
		{"--", 2, 2, 0}, {"A-9", 3, 3, 10}, {"-A-", 3, 3, -10},
		{"1", 2, 1, 1}, {"1|X", 2, 1, 1}, {"0PA8", 4, 4, -32768},
		{"1EKF", 4, 4, -1}, {"1ekf", 4, 4, -1}, {"-001", 4, 4, -1},
		{"1EKG", 4, 4, 0}, {"1EKH", 4, 4, 1}, {"-1EKG", 5, 5, 0},
	};
	for (size_t i = 0; i < sizeof(good) / sizeof(*good); ++i) {
		const char *cursor = good[i].text;
		int value = 12345;
		CHECK(cterm_rip_read_number(&cursor, good[i].width, &value));
		CHECK(value == good[i].value && cursor == good[i].text + good[i].used);
	}
	const char *bad[] = {"", "|", "+1", " 1", "1?", "A-?", "\xff", "\x80", "1\x1b"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
		const char *cursor = bad[i];
		int value = 12345;
		CHECK(!cterm_rip_read_number(&cursor, 3, &value));
		CHECK(cursor == bad[i] && value == 12345);
	}
	const uint8_t widths[3] = {2, 2, 1};
	const char *cursor = "0-001";
	int fields[3] = {99, 99, 99};
	CHECK(!cterm_rip_read_number(&cursor, 0, &fields[0]));
	CHECK(!cterm_rip_read_number(&cursor, 6, &fields[0]));
	CHECK(strcmp(cursor, "0-001") == 0 && fields[0] == 99);
	CHECK(cterm_rip_read_fields(&cursor, widths, 3, fields) == 3);
	CHECK(fields[0] == 0 && fields[1] == 0 && fields[2] == 1 && *cursor == '\0');
	cursor = "001";
	CHECK(cterm_rip_read_fields(&cursor, widths, 3, fields) == 2);
	CHECK(fields[0] == 0 && fields[1] == 1 && *cursor == '\0');

	/* Every WORD has independently generated positive and negative spellings.
	 * Division-based encoding is separate from the decoder's accumulation. */
	const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	for (unsigned word = 0; word < 65536u; ++word) {
		char positive[5] = {0}, negative[6] = {'-', 0};
		unsigned magnitude = word;
		for (size_t j = 4; j > 0; --j) {
			positive[j - 1] = digits[magnitude % 36u];
			magnitude /= 36u;
		}
		memcpy(negative + 1, positive, sizeof(positive));
		int value, expected = word < 32768u ? (int)word : (int)((int32_t)word - 65536);
		cursor = positive;
		CHECK(cterm_rip_read_number(&cursor, 4, &value));
		CHECK(value == expected && *cursor == '\0');
		cursor = negative;
		CHECK(cterm_rip_read_number(&cursor, 5, &value));
		unsigned negated = (65536u - word) % 65536u;
		expected = negated < 32768u ? (int)negated : (int)((int32_t)negated - 65536);
		CHECK(value == expected && *cursor == '\0');
	}
	CHECK(cterm_rip_signed_byte(0) == 0 && cterm_rip_signed_byte(127) == 127);
	CHECK(cterm_rip_signed_byte(128) == -128 && cterm_rip_signed_byte(255) == -1);
	return 0;
}
