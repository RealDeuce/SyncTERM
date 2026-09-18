/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "string_util.h"

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
	return EXIT_SUCCESS;
}
