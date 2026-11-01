/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_language_internal.h"
#include "io_error.h"
#include "string_compat.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Reject overlong environment values rather than truncate a language tag. */
#define LANGUAGE_TAG_SIZE 256

static bool
ascii_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool
ascii_digit(char c)
{
	return c >= '0' && c <= '9';
}

static char
ascii_lower(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static char
ascii_upper(char c)
{
	return c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
}

static bool
neutral_locale(const char *text, size_t size)
{
	return (size >= 1 && text[0] == 'C'
	        && (size == 1 || text[1] == '.' || text[1] == '@'))
	    || (size >= 5 && memcmp(text, "POSIX", 5) == 0
	        && (size == 5 || text[5] == '.' || text[5] == '@'));
}

/*
 * Normalize the identifiers supplied by OS preferences and POSIX locales.
 * This is structural validation, not an IANA registry or CLDR alias lookup.
 * Encoding suffixes do not describe a language. Known locale modifiers that
 * affect script/variant are retained; other modifiers (such as euro) aren't.
 */
static bool
language_tag(char tag[LANGUAGE_TAG_SIZE], const char *locale, size_t size)
{
	size_t end = 0;
	size_t modifier = size;
	size_t language_size = 0;
	size_t script_offset = 0;
	bool extensions = false;
	bool private_use = false;
	const char *script = NULL;
	const char *variant = NULL;

	if (neutral_locale(locale, size)) {
		memcpy(tag, "en", 3);
		return true;
	}
	while (end < size && locale[end] != '.' && locale[end] != '@')
		end++;
	for (size_t i = end; i < size; i++) {
		if (locale[i] == '@') {
			modifier = i + 1;
			break;
		}
	}
	if (end == 0 || end >= LANGUAGE_TAG_SIZE)
		return false;
	for (size_t start = 0; start < end;) {
		size_t stop = start;
		bool alpha = true;
		bool digits = true;

		while (stop < end && locale[stop] != '-' && locale[stop] != '_') {
			alpha = alpha && ascii_alpha(locale[stop]);
			digits = digits && ascii_digit(locale[stop]);
			if (!ascii_alpha(locale[stop]) && !ascii_digit(locale[stop]))
				return false;
			tag[stop] = ascii_lower(locale[stop]);
			stop++;
		}
		size_t length = stop - start;
		if (length == 0 || length > 8)
			return false;
		if (start == 0) {
			if (!alpha || length < 2)
				return false;
			language_size = length;
		} else if (length == 1 && !private_use) {
			/* Extensions/private-use subtags must retain lowercase. */
			extensions = true;
			private_use = tag[start] == 'x';
			if (stop == end)
				return false;
		} else if (!extensions && alpha && length == 4) {
			tag[start] = ascii_upper(tag[start]);
			script_offset = start;
		} else if (!extensions && ((alpha && length == 2)
		    || (digits && length == 3))) {
			for (size_t i = start; i < stop; i++)
				tag[i] = ascii_upper(tag[i]);
		}
		if (stop < end) {
			tag[stop] = '-';
			if (stop + 1 == end)
				return false;
		}
		start = stop + 1;
	}
	tag[end] = 0;
	if (modifier < size) {
		const char *name = locale + modifier;
		size_t length = size - modifier;

		if (length == 5 && memcmp(name, "latin", 5) == 0)
			script = "Latn";
		else if (length == 8 && memcmp(name, "cyrillic", 8) == 0)
			script = "Cyrl";
		else if (length == 8 && memcmp(name, "valencia", 8) == 0)
			variant = "valencia";
	}
	if (script != NULL && script_offset == 0 && !extensions) {
		if (end + 5 >= LANGUAGE_TAG_SIZE)
			return false;
		memmove(tag + language_size + 5, tag + language_size,
		    end - language_size + 1);
		tag[language_size] = '-';
		memcpy(tag + language_size + 1, script, 4);
		end += 5;
	}
	if (variant != NULL && !extensions) {
		if (end + 9 >= LANGUAGE_TAG_SIZE)
			return false;
		tag[end] = '-';
		memcpy(tag + end + 1, variant, 9);
	}
	return true;
}

int
st_language_add(struct st_language_preferences *result,
	const char *locale, size_t size)
{
	char tag[LANGUAGE_TAG_SIZE];
	char *copy;
	char **tags;

	if (!language_tag(tag, locale, size))
		return 0;
	for (size_t i = 0; i < result->count; i++) {
		if (strcmp(tag, result->tags[i]) == 0)
			return 0;
	}
	if (result->count >= SIZE_MAX / sizeof(*tags))
		return IO_ERROR_OUT_OF_MEMORY;
	copy = strdup(tag);
	if (copy == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	tags = realloc(result->tags, (result->count + 1) * sizeof(*tags));
	if (tags == NULL) {
		free(copy);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	result->tags = tags;
	result->tags[result->count++] = copy;
	return 0;
}

int
st_language_environment(struct st_language_preferences *result,
	const char *language, const char *all, const char *messages,
	const char *lang)
{
	const char *locale = all;
	int status;

	if (locale == NULL || locale[0] == 0)
		locale = messages;
	if (locale == NULL || locale[0] == 0)
		locale = lang;
	if (locale == NULL || locale[0] == 0)
		locale = "C";
	/* LANGUAGE is a message preference, and is ignored in the C locale. */
	if (!neutral_locale(locale, strlen(locale)) && language != NULL) {
		const char *start = language;

		while (*start != 0) {
			const char *stop = strchr(start, ':');
			size_t length = stop == NULL ? strlen(start)
			    : (size_t)(stop - start);

			status = st_language_add(result, start, length);
			if (status != 0)
				return status;
			if (stop == NULL || neutral_locale(start, length))
				break;
			start = stop + 1;
		}
	}
	if (result->count != 0)
		return 0;
	return st_language_add(result, locale, strlen(locale));
}

void
st_language_free(struct st_language_preferences *preferences)
{
	if (preferences == NULL)
		return;
	for (size_t i = 0; i < preferences->count; i++)
		free(preferences->tags[i]);
	free(preferences->tags);
	preferences->tags = NULL;
	preferences->count = 0;
}

int
st_language_discover(struct st_language_preferences *result)
{
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	result->tags = NULL;
	result->count = 0;
	status = st_language_platform(result);
	if (status == 0 && result->count == 0)
		status = st_language_add(result, "en", 2);
	if (status != 0)
		st_language_free(result);
	return status;
}
