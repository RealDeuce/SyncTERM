/* Implementations of string facilities missing from the target C library. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "string_compat.h"

#if !STRING_COMPAT_HAVE_STRDUP || !STRING_COMPAT_HAVE_STRNDUP \
    || defined(STRING_COMPAT_FORCE_FALLBACKS)
static char *
string_compat_duplicate(const char *string, size_t maximum)
{
	size_t length = 0;
	char *copy;

	while (length < maximum && string[length] != '\0')
		length++;
	if (length == SIZE_MAX)
		return NULL;
	copy = malloc(length + 1);
	if (copy != NULL) {
		memcpy(copy, string, length);
		copy[length] = '\0';
	}
	return copy;
}
#endif

#if !STRING_COMPAT_HAVE_STRDUP || defined(STRING_COMPAT_FORCE_FALLBACKS)
char *
syncterm_strdup(const char *string)
{
	return string_compat_duplicate(string, SIZE_MAX);
}
#endif

#if !STRING_COMPAT_HAVE_STRNDUP || defined(STRING_COMPAT_FORCE_FALLBACKS)
char *
syncterm_strndup(const char *string, size_t maximum)
{
	return string_compat_duplicate(string, maximum);
}
#endif

#if !STRING_COMPAT_HAVE_STRLCPY || defined(STRING_COMPAT_FORCE_FALLBACKS)
size_t
syncterm_strlcpy(char *destination, const char *source, size_t size)
{
	size_t source_length = strlen(source);

	if (size != 0) {
		size_t copy_length = source_length < size - 1
		    ? source_length : size - 1;

		memcpy(destination, source, copy_length);
		destination[copy_length] = '\0';
	}
	return source_length;
}
#endif

#if !STRING_COMPAT_HAVE_STRLCAT || defined(STRING_COMPAT_FORCE_FALLBACKS)
size_t
syncterm_strlcat(char *destination, const char *source, size_t size)
{
	size_t destination_length = 0;
	size_t source_length = strlen(source);

	while (destination_length < size
	    && destination[destination_length] != '\0')
		destination_length++;
	if (destination_length == size)
		return size + source_length;

	size_t available = size - destination_length - 1;
	size_t copy_length = source_length < available
	    ? source_length : available;
	memcpy(destination + destination_length, source, copy_length);
	destination[destination_length + copy_length] = '\0';
	return destination_length + source_length;
}
#endif

/* Keep the translation unit valid when the target supplies every facility. */
typedef int string_compat_translation_unit_is_not_empty;
