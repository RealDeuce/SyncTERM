/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_language_internal.h"
#include "io_error.h"

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

static int
add_wide_language(struct st_language_preferences *result,
	const WCHAR *language, size_t size)
{
	char tag[LOCALE_NAME_MAX_LENGTH];

	/* Language names are ASCII identifiers, not localized display names. */
	if (size >= sizeof(tag))
		return 0;
	for (size_t i = 0; i < size; i++) {
		if (language[i] > 127)
			return 0;
		tag[i] = (char)language[i];
	}
	return st_language_add(result, tag, size);
}

int
st_language_platform(struct st_language_preferences *result)
{
	ULONG count = 0;
	ULONG size = 0;
	int status = 0;

	if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &size)
	    && size > 0) {
		WCHAR *languages;
		size_t capacity = size;

		if (capacity > SIZE_MAX / sizeof(*languages))
			return IO_ERROR_OUT_OF_MEMORY;
		languages = malloc(capacity * sizeof(*languages));
		if (languages == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count,
		    languages, &size) && size <= capacity) {
			size_t start = 0;

			/* Bound the MULTI_SZ traversal even if preferences change. */
			while (start < size && languages[start] != 0) {
				size_t end = start;

				while (end < size && languages[end] != 0)
					end++;
				if (end == size)
					break;
				status = add_wide_language(result, languages + start,
				    end - start);
				if (status != 0)
					break;
				start = end + 1;
			}
		}
		free(languages);
	}
	if (status == 0 && result->count == 0) {
		WCHAR locale[LOCALE_NAME_MAX_LENGTH];
		int size_used = LCIDToLocaleName(
		    MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale,
		    LOCALE_NAME_MAX_LENGTH, 0);

		if (size_used > 1)
			status = add_wide_language(result, locale,
			    (size_t)size_used - 1);
	}
	return status;
}
