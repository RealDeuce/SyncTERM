/* Preferred UI languages, independent of terminal encodings and setlocale(). */
/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_LANGUAGE_H_
#define ST_LANGUAGE_H_

#include <stddef.h>

struct st_language_preferences {
	size_t count;
	char **tags;
};

/*
 * Return ordered, unique language tags (for example, "fr-CA", "zh-Hant").
 * Windows/macOS use native UI preferences; other POSIX hosts use LANGUAGE
 * and LC_ALL/LC_MESSAGES/LANG. An undetermined language defaults to "en".
 * This does not change the process locale. Environment changes must not race
 * discovery. The caller owns the result and releases it with the function
 * below before reusing it. On error, the result is empty; returns IO_ERROR_*.
 */
int st_language_discover(struct st_language_preferences *result);
void st_language_free(struct st_language_preferences *preferences);

#endif
