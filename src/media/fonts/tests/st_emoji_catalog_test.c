/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_emoji_catalog.h"
#include "st_emoji_fonts.h"
#include "st_utf8.h"
#include "st_unicode_grapheme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)

static bool
covered(const struct st_console_font *font, const char *text)
{
	size_t size = strlen(text), consumed;
	uint32_t scalar;
	if (st_utf8_decode_one(&scalar, &consumed, text, size) == 0 && consumed == size)
		for (size_t i = 0; i < font->mapping_count; i++)
			if (font->mapping[i].codepoint == scalar) return true;
	for (size_t i = 0; i < font->cluster_mapping_count; i++)
		if (font->cluster_mapping[i].text_size == size
		    && memcmp(font->cluster_mapping[i].text, text, size) == 0) return true;
	return false;
}

int st_emoji_catalog_test_main(void)
{
	CHECK(st_emoji_entry_count > 3000);
	const struct st_console_font *fonts[] = {&st_emoji_8_font, &st_emoji_14_font, &st_emoji_16_font};
	for (size_t i = 0; i < st_emoji_entry_count; i++) {
		const struct st_emoji_entry *entry = &st_emoji_entries[i];
		CHECK(entry->name[0] != '\0' && entry->base < st_emoji_entry_count);
		CHECK(st_emoji_entries[entry->base].base == entry->base);
		CHECK(st_emoji_find(entry->text) == i);
		uint32_t scalars[64];
		size_t count = 0, offset = 0, size = strlen(entry->text);
		while (offset < size) {
			size_t used;
			CHECK(count < 64 && st_utf8_decode_one(&scalars[count], &used, entry->text + offset, size - offset) == 0);
			offset += used;
			count++;
		}
		CHECK(st_unicode_rgi_emoji_prefix(scalars, count) == count);
		for (size_t f = 0; f < 3; f++) CHECK(covered(fonts[f], entry->text));
	}
	CHECK(st_emoji_find("not emoji") == st_emoji_entry_count);
	return 0;
}
