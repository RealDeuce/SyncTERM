/* Locale-independent US-ASCII string operations. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_STRING_UTIL_H_
#define SYNCTERM_STRING_UTIL_H_

#include <stdbool.h>
#include <stddef.h>

bool ascii_is_space(unsigned char character);
bool ascii_is_printable(unsigned char character);
bool ascii_is_digit(unsigned char character);
unsigned char ascii_to_lower(unsigned char character);
unsigned char ascii_to_upper(unsigned char character);
int ascii_case_compare(const char *left, const char *right);
int ascii_case_ncompare(const char *left, const char *right, size_t length);
bool ascii_case_starts_with(const char *text, const char *prefix);
bool ascii_case_span_equal(const char *text, size_t length,
	const char *value);

/* Returns the first non-space character after terminating trailing space. */
char *string_trim_ascii(char *string);
void string_ascii_trim_span(const char *string, size_t length,
    size_t *begin, size_t *end);
bool string_span_is_printable_ascii(const char *string, size_t length);

#endif
