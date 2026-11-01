/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_EMOJI_LOCALE_INTERNAL_H_
#define ST_EMOJI_LOCALE_INTERNAL_H_

#include <stddef.h>

struct st_emoji_locale_data {
	const char *tag;
	int parent;
	const unsigned char *packed;
	size_t packed_size;
	size_t size;
};
struct st_emoji_locale_alias {
	const char *tag;
	size_t locale;
};
extern const struct st_emoji_locale_data st_emoji_locales[];
extern const size_t st_emoji_locale_count;
extern const struct st_emoji_locale_alias st_emoji_locale_aliases[];
extern const size_t st_emoji_locale_alias_count;

/* Raw LZ4 block decoder for the checked-in CLDR assets. */
int st_emoji_unpack(unsigned char *output, size_t size,
	const unsigned char *input, size_t packed_size);

#endif
