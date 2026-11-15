/* Independent protocol alphabets/examples, wide fields, and failure atomicity. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_number.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; \
} } while (0)

int
main(void)
{
	static const struct {
		unsigned radix;
		const char *text;
		uint64_t value;
	} cases[] = {
		{36, "0", 0}, {36, "0A", 10}, {36, "0E", 14}, {36, "1S", 64},
		{36, "6Y", 250},
		{36, "1M", 58}, {36, "HR", 639},
		{36, "QO", 960}, {36, "ZK", 1280}, {36, "ZZ", 1295},
		{36, "ZZZZZ", 60466175}, {36, "3W5E11264SGSF", UINT64_MAX},
		{64, "0A", 10}, {64, "0E", 14}, {64, "10", 64}, {64, "F0", 960},
		{64, "K0", 1280}, {64, "KF", 1295}, {64, "&&", 4095},
		{64, "3w", 250}, {64, "R9a", 111204}, {64, "0#", 62},
		{64, "0&", 63}, {64, "&&&&", 16777215},
		{64, "&&&&&", 1073741823}, {64, "F&&&&&&&&&&", UINT64_MAX},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
		uint64_t value = 99;
		char output[32];
		size_t length = strlen(cases[i].text);

		memset(output, '!', sizeof(output));
		CHECK(st_rip3_number_decode(&value, cases[i].text, length,
		    cases[i].radix) == 0 && value == cases[i].value);
		CHECK(st_rip3_number_encode(output, sizeof(output), value,
		    length, cases[i].radix) == 0);
		CHECK(memcmp(output, cases[i].text, length) == 0);
		CHECK(output[length] == '!');
	}
	{
		/* Every digit is checked independently, including case distinction. */
		static const char digits[] =
		    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
		uint64_t value;
		for (size_t i = 0; i < sizeof(digits) - 1; ++i) {
			CHECK(st_rip3_number_decode(&value, digits + i, 1, 64) == 0);
			CHECK(value == i);
			if (i < 62) {
				CHECK(st_rip3_number_decode(&value, digits + i, 1, 36) == 0);
				CHECK(value == (i < 36 ? i : i - 26));
			}
		}
	}
	{
		static const char *const bad[] = {
		    "-1", "+1", " 1", "1 ", "1|", "1$", "\xff", "\t0",
		};
		for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
			for (unsigned radix = 36; radix <= 64; radix += 28) {
				uint64_t value = 765;
				CHECK(st_rip3_number_decode(&value, bad[i], strlen(bad[i]),
				    radix) == IO_ERROR_INVALID_FORMAT);
				CHECK(value == 765);
			}
		}
	}
	{
		uint64_t value = 765;
		char output[8] = "intact!";
		const char nul[] = {'1', 0, '2'};

		CHECK(st_rip3_number_decode(&value, nul, sizeof(nul), 64) == IO_ERROR_INVALID_FORMAT);
		CHECK(st_rip3_number_decode(&value, "#", 1, 36) == IO_ERROR_INVALID_FORMAT);
		CHECK(st_rip3_number_decode(&value, "&", 1, 36) == IO_ERROR_INVALID_FORMAT);
		CHECK(st_rip3_number_decode(&value, "G0000000000", 11, 64) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_number_decode(&value, "3W5E11264SGSG", 13, 36) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_number_decode(&value, "", 0, 36) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_number_decode(&value, NULL, 1, 36) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_number_decode(NULL, "0", 1, 36) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_number_decode(&value, "10", 2, 10) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(value == 765);
		CHECK(st_rip3_number_encode(output, sizeof(output), 1296, 2, 36) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_number_encode(output, sizeof(output), 4095, 2, 36) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_number_encode(output, 1, 0, 2, 36) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_number_encode(output, sizeof(output), 0, 0, 36) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_number_encode(output, sizeof(output), 0, 2, 10) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(memcmp(output, "intact!", sizeof(output)) == 0);
		CHECK(st_rip3_number_encode(output, sizeof(output), 250, 5, 64) == 0);
		CHECK(memcmp(output, "0003w", 5) == 0 && output[5] == 't');
		CHECK(st_rip3_number_decode(&value, "00000000000000000000001", 23, 36) == 0 && value == 1);
	}
	return 0;
}
