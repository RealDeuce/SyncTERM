/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "value_convert.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #c); return EXIT_FAILURE; } } while (0)

static bool
bytes_equal(uint64_t value, const char *expected)
{
	char buffer[64];

	return value_format_byte_estimate(buffer, sizeof(buffer), value)
	    && strcmp(buffer, expected) == 0;
}

static bool
duration_equal(double value, const char *expected)
{
	char buffer[64];

	return value_format_duration_estimate(buffer, sizeof(buffer), value)
	    && strcmp(buffer, expected) == 0;
}

int
main(void)
{
	char buffer[64];
	static const char *const locales[] = {
		"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE.utf8", "fr_FR.utf8"
	};

	CHECK(bytes_equal(0, "0"));
	CHECK(bytes_equal(42, "42"));
	CHECK(bytes_equal(1023, "1023"));
	CHECK(bytes_equal(1024, "1.0K"));
	CHECK(bytes_equal(1280, "1.2K"));
	CHECK(bytes_equal(1536, "1.5K"));
	CHECK(bytes_equal(1792, "1.8K"));
	CHECK(bytes_equal(UINT64_C(1048575), "1024.0K"));
	CHECK(bytes_equal(UINT64_C(1048576), "1.0M"));
	CHECK(bytes_equal(UINT64_MAX, "16384.0P"));
	CHECK(!value_format_byte_estimate(buffer, 1, 0));
	CHECK(!value_format_byte_estimate(NULL, sizeof(buffer), 0));

	CHECK(duration_equal(-1.0, "0s"));
	CHECK(duration_equal(NAN, "0s"));
	CHECK(duration_equal(0.0, "0s"));
	CHECK(duration_equal(8.25, "8.2s"));
	CHECK(duration_equal(8.75, "8.8s"));
	CHECK(duration_equal(42.0, "42s"));
	CHECK(duration_equal(90.0, "1.5m"));
	CHECK(duration_equal(10800.0, "3.0h"));
	CHECK(duration_equal(86400.0, "1.0d"));
	CHECK(duration_equal(365.0 * 86400.0, "1.0y"));
	CHECK(duration_equal(INFINITY, "infy"));
	CHECK(!value_format_duration_estimate(buffer, 1, 0.0));
	CHECK(!value_format_duration_estimate(NULL, sizeof(buffer), 0.0));

	for (size_t index = 0; index < sizeof(locales) / sizeof(locales[0]); index++) {
		if (setlocale(LC_NUMERIC, locales[index]) != NULL) {
			CHECK(bytes_equal(1536, "1.5K"));
			CHECK(duration_equal(90.0, "1.5m"));
			break;
		}
	}
	(void)setlocale(LC_NUMERIC, "C");
	return EXIT_SUCCESS;
}
