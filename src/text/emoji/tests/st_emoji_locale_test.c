/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_emoji_catalog.h"
#include "st_emoji_locale.h"
#include "st_emoji_locale_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
	fprintf(stderr, "line %d: %s\n", __LINE__, #test); return 1; \
} } while (0)

static bool
search_has(st_emoji_locale_t *locale, const char *query, const char *emoji)
{
	size_t *ids, count;
	bool found = false;

	if (st_emoji_search(locale, query, strlen(query), &ids, &count) != 0)
		return false;
	for (size_t i = 0; i < count; i++)
		if (strcmp(st_emoji_entries[ids[i]].text, emoji) == 0)
			found = true;
	free(ids);
	return found;
}

static int
test_names(void)
{
	static const struct {
		const char *language, *emoji, *name;
	} cases[] = {
		{"fr-CA", "😀", "visage avec large sourire"},
		{"fr-CA", "🕹️", "manette de jeu"},
		{"fr-FR", "💰", "sac plein d’argent"},
		{"de-DE-u-ca-gregory", "💰", "Geldsack"},
		{"ja-JP", "🐈", "ネコ"},
		{"zh-TW", "🐈", "貓"},
		{"zh-CN", "🐈", "猫"},
		{"sr-Latn-RS", "🐈", "mačka"},
		{"sr-Cyrl-RS", "🐈", "мачка"},
		{"sh", "🐈", "mačka"},
		{"zz-XX", "😀", "grinning face"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		st_emoji_locale_t *locale;
		size_t id = st_emoji_find(cases[i].emoji);

		CHECK(id < st_emoji_entry_count);
		CHECK(st_emoji_locale_create(&locale, &cases[i].language, 1) == 0);
		const char *name = st_emoji_localized_name(locale, id);
		if (strcmp(name, cases[i].name) != 0) {
			fprintf(stderr, "%s %s: expected %s, got %s\n",
			    cases[i].language, cases[i].emoji, cases[i].name, name);
			st_emoji_locale_destroy(locale);
			return 1;
		}
		st_emoji_locale_destroy(locale);
	}
	st_emoji_locale_t *locale;
	const char *preferences[] = {"zz", "sr-Arab-ZZ", "fr-CA", "fr-CA", "de"};
	CHECK(st_emoji_locale_create(&locale, preferences, 5) == 0);
	CHECK(strcmp(st_emoji_localized_name(locale, st_emoji_find("😀")),
	    "visage avec large sourire") == 0);
	CHECK(st_emoji_localized_name(locale, st_emoji_entry_count) == NULL);
	CHECK(st_emoji_localized_keywords(locale, st_emoji_entry_count) == NULL);
	CHECK(st_emoji_english_keywords(locale, st_emoji_entry_count) == NULL);
	CHECK(st_emoji_localized_keywords(NULL, 0) == NULL);
	CHECK(st_emoji_english_keywords(NULL, 0) == NULL);
	st_emoji_locale_destroy(locale);
	CHECK(st_emoji_locale_create(&locale, NULL, 0) == 0);
	CHECK(strcmp(st_emoji_localized_name(locale, st_emoji_find("😀")), "grinning face") == 0);
	st_emoji_locale_destroy(locale);
	CHECK(st_emoji_locale_create(&locale, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(locale == NULL);
	st_emoji_locale_destroy(NULL);
	return 0;
}

static int
test_search(void)
{
	st_emoji_locale_t *locale;
	const char *language = "fr-CA";
	size_t *ids, count;

	CHECK(st_emoji_locale_create(&locale, &language, 1) == 0);
	ids = &count;
	CHECK(st_emoji_search(locale, "a", 1, &ids, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ids == NULL);
	CHECK(st_emoji_search(locale, NULL, 1, &ids, &count) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ids == NULL && count == 0);
	CHECK(search_has(locale, "MANETTE", "🕹️"));
	CHECK(!search_has(locale, "MIAULER", "🐈"));
	CHECK(search_has(locale, "GRINNING FACE", "😀"));
	CHECK(strstr(st_emoji_localized_keywords(locale, st_emoji_find("😀")), "sourire") != NULL);
	CHECK(strstr(st_emoji_english_keywords(locale, st_emoji_find("😀")), "grin") != NULL);
	CHECK(strstr(st_emoji_english_keywords(locale, st_emoji_find("👙")), "pool") != NULL);
	CHECK(search_has(locale, "poo", "👙"));
	CHECK(search_has(locale, "MAN SHRUGGING: MEDIUM SKIN TONE", "🤷🏽‍♂️"));
	CHECK(search_has(locale, "person walking: facing right", "🚶‍➡️"));
	CHECK(search_has(locale, "HAUSSE LES ÉPAULES", "🤷🏽‍♂️"));
	CHECK(st_emoji_search(locale, "zzqq no emoji", 13, &ids, &count) == 0);
	CHECK(count == 0);
	free(ids);
	CHECK(st_emoji_search(locale, "\x1f", 1, &ids, &count) == 0);
	CHECK(count == 0);
	free(ids);
	CHECK(st_emoji_search(locale, "\0", 1, &ids, &count) == 0);
	CHECK(count == 0 && ids == NULL);
	CHECK(st_emoji_search(locale, "\xff", 1, &ids, &count) < 0);
	CHECK(ids == NULL);
	CHECK(st_emoji_search(locale, "", 0, &ids, &count) == 0);
	CHECK(count > 1000 && count < st_emoji_entry_count);
	for (size_t i = 0; i < count; i++)
		CHECK(st_emoji_entries[ids[i]].base == ids[i]);
	free(ids);
	st_emoji_locale_destroy(locale);
	// A regional keyword override does not union in replaced parent keywords.
	language = "fr";
	CHECK(st_emoji_locale_create(&locale, &language, 1) == 0);
	CHECK(search_has(locale, "E\xcc\x81MOTICO\xcc\x82NE", "😀"));
	CHECK(search_has(locale, "MIAULER", "🐈"));
	CHECK(st_emoji_search(locale, "\xcc\x81", 2, &ids, &count) == 0);
	CHECK(count == 0);
	free(ids);
	st_emoji_locale_destroy(locale);
	language = "ja";
	CHECK(st_emoji_locale_create(&locale, &language, 1) == 0);
	CHECK(search_has(locale, "猫", "🐈"));
	st_emoji_locale_destroy(locale);
	return 0;
}

static int
test_blocks(void)
{
	unsigned char output[256];
	static const unsigned char literal[] = {0x50, 'h', 'e', 'l', 'l', 'o'};
	static const unsigned char overlap[] = {0x11, 'a', 1, 0, 0x80,
	    'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
	static const struct { unsigned char data[5]; size_t size; } bad[] = {
		{{0xf0}, 1}, {{0xf0, 255}, 2}, {{0x20, 'a'}, 2},
		{{0x10, 'a', 0, 0}, 4}, {{0x10, 'a', 2, 0}, 4},
		{{0x10, 'a', 1}, 3}, {{0x1f, 'a', 1, 0, 255}, 5},
	};

	CHECK(st_emoji_unpack(output, 5, literal, sizeof(literal)) == 0);
	CHECK(memcmp(output, "hello", 5) == 0);
	CHECK(st_emoji_unpack(output, 14, overlap, sizeof(overlap)) == 0);
	CHECK(memcmp(output, "aaaaaabcdefghi", 14) == 0);
	CHECK(st_emoji_unpack(output, 4, literal, sizeof(literal)) == IO_ERROR_INVALID_FORMAT);
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(st_emoji_unpack(output, sizeof(output), bad[i].data, bad[i].size)
		    == IO_ERROR_INVALID_FORMAT);
	CHECK(st_emoji_locale_count > 150);
	for (size_t i = 0; i < st_emoji_locale_count; i++) {
		const struct st_emoji_locale_data *data = &st_emoji_locales[i];
		unsigned char *decoded = malloc(data->size == 0 ? 1 : data->size);
		size_t position = 0;

		CHECK(decoded != NULL);
		CHECK(st_emoji_unpack(decoded, data->size, data->packed, data->packed_size) == 0);
		while (position < data->size) {
			CHECK(data->size - position >= 2);
			size_t id = decoded[position] | ((size_t)decoded[position + 1] << 8);
			CHECK(id < st_emoji_entry_count);
			position += 2;
			for (size_t field = 0; field < 2; field++) {
				unsigned char *end = memchr(decoded + position, 0, data->size - position);
				CHECK(end != NULL);
				position = (size_t)(end - decoded) + 1;
			}
		}
		free(decoded);
	}
	return 0;
}

int
st_emoji_locale_test_main(void)
{
	CHECK(test_blocks() == 0);
	CHECK(test_names() == 0);
	CHECK(test_search() == 0);
	return 0;
}
