/* Declarations for string facilities missing from the target C library. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef SYNCTERM_STRING_COMPAT_H_
#define SYNCTERM_STRING_COMPAT_H_

#include "string_compat_config.h"

#include <stddef.h>
#include <string.h>

#if !STRING_COMPAT_HAVE_STRDUP || defined(STRING_COMPAT_FORCE_FALLBACKS)
char *syncterm_strdup(const char *string);
	#define strdup syncterm_strdup
#endif

#if !STRING_COMPAT_HAVE_STRNDUP || defined(STRING_COMPAT_FORCE_FALLBACKS)
char *syncterm_strndup(const char *string, size_t maximum);
	#define strndup syncterm_strndup
#endif

#if !STRING_COMPAT_HAVE_STRLCPY || defined(STRING_COMPAT_FORCE_FALLBACKS)
size_t syncterm_strlcpy(char *destination, const char *source, size_t size);
	#define strlcpy syncterm_strlcpy
#endif

#if !STRING_COMPAT_HAVE_STRLCAT || defined(STRING_COMPAT_FORCE_FALLBACKS)
size_t syncterm_strlcat(char *destination, const char *source, size_t size);
	#define strlcat syncterm_strlcat
#endif

#if !STRING_COMPAT_HAVE_STRTOK_R || defined(STRING_COMPAT_FORCE_FALLBACKS)
char *syncterm_strtok_r(char *string, const char *delimiters, char **state);
	#define strtok_r syncterm_strtok_r
#endif

#endif
