/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_EMOJI_LOCALE_H_
#define ST_EMOJI_LOCALE_H_

#include <stddef.h>

typedef struct st_emoji_locale st_emoji_locale_t;

/*
 * Resolve ordered language tags against bundled CLDR locales and parents.
 * Missing fields fall through later preferences, then English. The context
 * owns its decoded data and search keys; no process locale/global is changed.
 * Functions returning int use IO_ERROR_* and leave output pointers NULL on
 * failure. Each context is used by one owner at a time.
 */
int st_emoji_locale_create(st_emoji_locale_t **result,
	const char *const *languages, size_t count);
void st_emoji_locale_destroy(st_emoji_locale_t *locale);
/* Static/context-owned name; NULL for an invalid emoji index. */
const char *st_emoji_localized_name(const st_emoji_locale_t *locale, size_t id);
/* Context-owned, pipe-separated keyword fields; NULL if absent/invalid. */
const char *st_emoji_localized_keywords(const st_emoji_locale_t *locale, size_t id);
const char *st_emoji_english_keywords(const st_emoji_locale_t *locale, size_t id);
/*
 * Match localized names/keywords, plus English names/keywords, with canonical
 * caseless Unicode matching at grapheme boundaries. An empty query returns
 * base representatives. The caller frees the allocated indexes with free().
 */
int st_emoji_search(const st_emoji_locale_t *locale, const void *query,
	size_t size, size_t **indexes, size_t *count);

#endif
