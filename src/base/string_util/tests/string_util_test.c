/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "string_util.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", \
    __FILE__, __LINE__); return EXIT_FAILURE; } } while (0)

int
main(void)
{
	char text[] = " \t Value \r\n";
	size_t begin;
	size_t end;

	CHECK(ascii_is_space('\v') && !ascii_is_space(0xa0));
	CHECK(ascii_to_lower('Z') == 'z' && ascii_to_lower(0xc0) == 0xc0);
	CHECK(ascii_case_compare("Key", "kEY") == 0);
	CHECK(ascii_case_compare("a", "b") < 0);
	CHECK(ascii_case_ncompare("Section:One", "section:Two", 8) == 0);
	CHECK(ascii_case_starts_with("Section:One", "section:"));
	CHECK(!ascii_case_starts_with("Sec", "section:"));
	CHECK(!ascii_case_starts_with("\xc0", "\xe0"));
	CHECK(ascii_case_span_equal("TRUE trailing", 4, "true"));
	CHECK(!ascii_case_span_equal("TRUE trailing", 3, "true"));
	CHECK(strcmp(string_trim_ascii(text), "Value") == 0);
	string_ascii_trim_span("  abc ", 6, &begin, &end);
	CHECK(begin == 2 && end == 5);
	CHECK(string_span_is_printable_ascii("A~", 2));
	CHECK(!string_span_is_printable_ascii("\x80", 1));
	for (unsigned int byte = 0; byte < 256; ++byte) {
		bool space = byte == 32 || (byte >= 9 && byte <= 13);
		unsigned int lower = byte >= 65 && byte <= 90 ? byte + 32 : byte;
		char span[] = {' ', (char)byte, '\t'};
		CHECK(ascii_is_space((unsigned char)byte) == space);
		CHECK(ascii_is_printable((unsigned char)byte)
		    == (byte >= 32 && byte <= 126));
		CHECK(ascii_to_lower((unsigned char)byte) == lower);
		CHECK(ascii_to_upper((unsigned char)byte)
		    == (byte >= 97 && byte <= 122 ? byte - 32 : byte));
		CHECK(ascii_is_digit((unsigned char)byte) == (byte >= 48 && byte <= 57));
		string_ascii_trim_span(span, sizeof(span), &begin, &end);
		CHECK(begin == (space ? 3U : 1U));
		CHECK(end == (space ? 3U : 2U));
	}
	const char *const locales[] = {
		"C", "", "tr_TR.UTF-8", "tr_TR.ISO8859-9", "en_US.ISO8859-1",
	};
	for (size_t i = 0; i < sizeof(locales) / sizeof(locales[0]); ++i) {
		if (setlocale(LC_CTYPE, locales[i]) == NULL)
			continue;
		for (unsigned int byte = 0; byte < 256; ++byte) {
			CHECK(ascii_is_digit((unsigned char)byte) == (byte >= 48 && byte <= 57));
			CHECK(ascii_to_upper((unsigned char)byte)
			    == (byte >= 97 && byte <= 122 ? byte - 32 : byte));
		}
	}
	CHECK(setlocale(LC_CTYPE, "C") != NULL);
	string_ascii_trim_span("", 0, &begin, &end);
	CHECK(begin == 0 && end == 0);
	CHECK(ascii_case_starts_with("", ""));
	CHECK(!ascii_case_starts_with(NULL, ""));
	CHECK(!ascii_case_span_equal("a\0b", 3, "a"));
	CHECK(ascii_case_ncompare("a", "b", 0) == 0);
	CHECK(ascii_case_compare("\xff", "z") > 0);
	CHECK(string_trim_ascii(NULL) == NULL);
	return EXIT_SUCCESS;
}
