/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "value_convert.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #c); return EXIT_FAILURE; } } while (0)

int
main(void)
{
	char buffer[64];
	int64_t signed_value;
	uint64_t unsigned_value;
	bool boolean;
	double value;
	double round_trip;
	time_t timestamp;

	/* Mathematical values, not arbitrary floating-point object bit patterns. */
#if FLT_RADIX == 2 && DBL_MANT_DIG == 53
	CHECK(value_parse_double(
	    "1.00000000000000011102230246251565404236316680908203125", &value));
	CHECK(value == 1.0);
	CHECK(value_parse_double(
	    "1.00000000000000011102230246251565404236316680908203126", &value));
	CHECK(value == nextafter(1.0, 2.0));
	CHECK(value_parse_double("9007199254740993", &value));
	CHECK(value == 9007199254740992.0);
	CHECK(value_parse_double("9007199254740995", &value));
	CHECK(value == 9007199254740996.0);
#endif
	CHECK(value_parse_double("-0.000e99999", &value));
	CHECK(value == 0.0 && signbit(value));
	{
		static const char *const invalid[] = {
		    "", ".", "+", "1e", "1e+", "1.2.3", "nan", "inf", "0x1p0"
		};
		for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
			value = 42.0;
			CHECK(!value_parse_double(invalid[i], &value));
			CHECK(value == 42.0);
		}
	}
	memcpy(buffer, "keep", 5);
	CHECK(!value_format_double(buffer, 2, 1.25));
	CHECK(strcmp(buffer, "keep") == 0);
	CHECK(value_parse_int64(" -0x8000000000000000 ", &signed_value));
	CHECK(signed_value == INT64_MIN);
	CHECK(!value_parse_int64("12x", &signed_value));
	CHECK(value_parse_uint64("18446744073709551615", &unsigned_value));
	CHECK(unsigned_value == UINT64_MAX);
	CHECK(!value_parse_uint64("18446744073709551616", &unsigned_value));
	CHECK(value_parse_bool(" YeS ", &boolean) && boolean);
	CHECK(!value_parse_bool("2", &boolean));
	CHECK(value_parse_double(" -1.25e2 ", &value) && value == -125.0);
	CHECK(!value_parse_double("1.2x", &value));
	CHECK(value_format_double(buffer, sizeof(buffer), 1.2345678901234567));
	CHECK(strchr(buffer, ',') == NULL);
	CHECK(value_parse_double(buffer, &round_trip));
	CHECK(fabs(round_trip - 1.2345678901234567)
	    <= 1.2345678901234567e-15);
	CHECK(value_format_double(buffer, sizeof(buffer),
	    nextafter(0.0, 1.0)));
	CHECK(value_parse_double(buffer, &round_trip));
	CHECK(round_trip == nextafter(0.0, 1.0));
	CHECK(value_format_double(buffer, sizeof(buffer), DBL_MIN));
	CHECK(value_parse_double(buffer, &round_trip));
	CHECK(round_trip == DBL_MIN);
	CHECK(value_format_double(buffer, sizeof(buffer), DBL_MAX));
	CHECK(value_parse_double(buffer, &round_trip));
	CHECK(round_trip == DBL_MAX);
	CHECK(!value_parse_double("1e5000", &round_trip));
	CHECK(value_parse_duration("2 h", &value) && value == 7200.0);
	CHECK(value_format_duration(buffer, sizeof(buffer), 7200.0));
	CHECK(strcmp(buffer, "2h") == 0);
	CHECK(value_format_int64(buffer, sizeof(buffer), INT64_MIN));
	CHECK(strcmp(buffer, "-9223372036854775808") == 0);
	CHECK(value_parse_datetime("Never", &timestamp) && timestamp == 0);
	CHECK(value_parse_datetime("20200102T030405", &timestamp));
	CHECK(value_format_datetime(buffer, sizeof(buffer), timestamp, true));
	CHECK(strcmp(buffer, "Jan  2 2020 03:04:05") == 0);
	{
		static const char *const locales[] = {
			"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE.utf8", "fr_FR.utf8"
		};

		for (size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
			if (setlocale(LC_NUMERIC, locales[i]) != NULL) {
				CHECK(value_parse_double("1.5", &value) && value == 1.5);
				CHECK(value_format_double(buffer, sizeof(buffer), 1.5));
				CHECK(strcmp(buffer, "1.5") == 0);
				break;
			}
		}
		(void)setlocale(LC_NUMERIC, "C");
	}
	{
		time_t compact;
		time_t named;

		CHECK(value_parse_datetime("20200102", &compact));
		CHECK(value_parse_datetime("Jan  2 2020", &named));
		if (compact != named)
			fprintf(stderr, "compact date interpreted differently from named date\n");
		CHECK(compact == named);
	}
	return EXIT_SUCCESS;
}
