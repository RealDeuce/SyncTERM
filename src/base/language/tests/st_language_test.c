/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_language_internal.h"
#include "io_error.h"

#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
	fprintf(stderr, "line %d: %s\n", __LINE__, #test); return 1; \
} } while (0)

static bool
matches(const struct st_language_preferences *list, const char *expected)
{
	const char *start = expected;
	size_t index = 0;

	while (*start != 0) {
		const char *end = strchr(start, ':');
		size_t length = end == NULL ? strlen(start) : (size_t)(end - start);

		if (index >= list->count || strlen(list->tags[index]) != length
		    || memcmp(list->tags[index], start, length) != 0)
			return false;
		index++;
		if (end == NULL)
			break;
		start = end + 1;
	}
	return index == list->count;
}

static int
test_identifiers(void)
{
	static const struct {
		const char *locale;
		const char *expected;
	} cases[] = {
		{"en_US.UTF-8", "en-US"}, {"FR-ca", "fr-CA"},
		{"zh_hANT_tw", "zh-Hant-TW"}, {"es_419", "es-419"},
		{"sr_RS.UTF-8@latin", "sr-Latn-RS"},
		{"uz_UZ@cyrillic", "uz-Cyrl-UZ"},
		{"sr_Latn_RS@latin", "sr-Latn-RS"},
		{"ca_ES@valencia", "ca-ES-valencia"},
		{"de_DE.ISO-8859-15@euro", "de-DE"},
		{"en-US-u-ca-gregory", "en-US-u-ca-gregory"},
		{"de-DE-x-a", "de-DE-x-a"},
		{"C", "en"}, {"C.UTF-8", "en"}, {"POSIX", "en"},
		{"", ""}, {"en-", ""}, {"-en", ""}, {"en__US", ""},
		{"en-US-u", ""}, {"garbage/locale", ""}, {"e_US", ""},
		{"123_US", ""}, {"en_US;exec", ""},
		{"languagewaytoolong_US", ""}, {"en_\xc3\xa9", ""},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct st_language_preferences list = {0};

		CHECK(st_language_add(&list, cases[i].locale,
		    strlen(cases[i].locale)) == 0);
		if (!matches(&list, cases[i].expected)) {
			fprintf(stderr, "identifier: %s\n", cases[i].locale);
			st_language_free(&list);
			return 1;
		}
		st_language_free(&list);
	}
	struct st_language_preferences list = {0};
	char overlong[1024];

	memset(overlong, 'a', sizeof(overlong));
	CHECK(st_language_add(&list, overlong, sizeof(overlong)) == 0);
	CHECK(list.count == 0);
	CHECK(st_language_add(&list, "fr_CA", 5) == 0);
	CHECK(st_language_add(&list, "FR-ca.UTF-8", 11) == 0);
	CHECK(st_language_add(&list, "de", 2) == 0);
	CHECK(matches(&list, "fr-CA:de"));
	st_language_free(&list);
	CHECK(list.count == 0 && list.tags == NULL);
	st_language_free(&list);
	st_language_free(NULL);
	return 0;
}

static int
test_environment(void)
{
	static const struct {
		const char *language, *all, *messages, *lang, *expected;
	} cases[] = {
		{NULL, NULL, NULL, "ja_JP.UTF-8", "ja-JP"},
		{NULL, "fr_CA.UTF-8", "de_DE", "en_US", "fr-CA"},
		{NULL, "", "de_DE", "en_US", "de-DE"},
		{NULL, "", "", "en_GB", "en-GB"},
		{"fr_CA:de:fr-CA", NULL, NULL, "en_US", "fr-CA:de"},
		{"::fr::de:", NULL, NULL, "en_US", "fr:de"},
		{"bad/name:ja", NULL, NULL, "en_US", "ja"},
		{"bad/name::", NULL, NULL, "en_GB", "en-GB"},
		{"fr", "C", "de_DE", "en_US", "en"},
		{"fr", NULL, "POSIX", "en_US", "en"},
		{"fr", NULL, NULL, "C.UTF-8", "en"},
		{"fr", NULL, NULL, NULL, "en"},
		{"fr:C:de", NULL, NULL, "en_US", "fr:en"},
		{"", NULL, NULL, "it_IT", "it-IT"},
		{NULL, NULL, NULL, NULL, "en"},
		{NULL, NULL, NULL, "", "en"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct st_language_preferences list = {0};

		CHECK(st_language_environment(&list, cases[i].language,
		    cases[i].all, cases[i].messages, cases[i].lang) == 0);
		CHECK(matches(&list, cases[i].expected));
		st_language_free(&list);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct st_language_preferences list;
	const char *locale = setlocale(LC_ALL, NULL);
	char *saved_locale;

	CHECK(locale != NULL);
	saved_locale = malloc(strlen(locale) + 1);
	CHECK(saved_locale != NULL);
	memcpy(saved_locale, locale, strlen(locale) + 1);
	CHECK(st_language_discover(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_language_discover(&list) == 0);
	CHECK(list.count > 0);
	CHECK(strcmp(setlocale(LC_ALL, NULL), saved_locale) == 0);
	free(saved_locale);
	if (argc > 1) {
		CHECK(strcmp(argv[1], "--expect") == 0);
		CHECK(list.count == (size_t)argc - 2);
		for (size_t i = 0; i < list.count; i++)
			CHECK(strcmp(list.tags[i], argv[i + 2]) == 0);
	}
	for (size_t i = 0; i < list.count; i++) {
		printf("%s\n", list.tags[i]);
		for (size_t j = 0; j < i; j++)
			CHECK(strcmp(list.tags[i], list.tags[j]) != 0);
	}
	st_language_free(&list);
	CHECK(test_identifiers() == 0);
	CHECK(test_environment() == 0);
	return 0;
}
