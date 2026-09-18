/* Locale-independent US-ASCII string operations. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "string_util.h"

#include <string.h>

bool
ascii_is_space(unsigned char character)
{
	return character == '\t' || character == '\n' || character == '\v'
	    || character == '\f' || character == '\r' || character == ' ';
}

bool
ascii_is_printable(unsigned char character)
{
	return character >= 0x20 && character <= 0x7e;
}

unsigned char
ascii_to_lower(unsigned char character)
{
	if (character >= 'A' && character <= 'Z')
		return (unsigned char)(character + ('a' - 'A'));
	return character;
}

int
ascii_case_compare(const char *left, const char *right)
{
	while (*left != '\0' && *right != '\0') {
		unsigned char l = ascii_to_lower((unsigned char)*left++);
		unsigned char r = ascii_to_lower((unsigned char)*right++);

		if (l != r)
			return (l > r) - (l < r);
	}
	return ((unsigned char)*left > (unsigned char)*right)
	    - ((unsigned char)*left < (unsigned char)*right);
}

int
ascii_case_ncompare(const char *left, const char *right, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		unsigned char l = ascii_to_lower((unsigned char)left[i]);
		unsigned char r = ascii_to_lower((unsigned char)right[i]);

		if (l != r)
			return (l > r) - (l < r);
		if (l == '\0')
			return 0;
	}
	return 0;
}

bool
ascii_case_starts_with(const char *text, const char *prefix)
{
	if (text == NULL || prefix == NULL)
		return false;
	while (*prefix != '\0') {
		if (*text == '\0'
		    || ascii_to_lower((unsigned char)*text)
		    != ascii_to_lower((unsigned char)*prefix))
			return false;
		text++;
		prefix++;
	}
	return true;
}

bool
ascii_case_span_equal(const char *text, size_t length, const char *value)
{
	return text != NULL && value != NULL && strlen(value) == length
	    && ascii_case_ncompare(text, value, length) == 0;
}

void
string_ascii_trim_span(const char *string, size_t length, size_t *begin,
    size_t *end)
{
	size_t first = 0;
	size_t last = length;

	while (first < last && ascii_is_space((unsigned char)string[first]))
		first++;
	while (last > first && ascii_is_space((unsigned char)string[last - 1]))
		last--;
	if (begin != NULL)
		*begin = first;
	if (end != NULL)
		*end = last;
}

char *
string_trim_ascii(char *string)
{
	size_t begin;
	size_t end;

	if (string == NULL)
		return NULL;
	string_ascii_trim_span(string, strlen(string), &begin, &end);
	string[end] = '\0';
	return string + begin;
}

bool
string_span_is_printable_ascii(const char *string, size_t length)
{
	if (string == NULL)
		return false;
	for (size_t i = 0; i < length; i++) {
		if (!ascii_is_printable((unsigned char)string[i]))
			return false;
	}
	return true;
}
