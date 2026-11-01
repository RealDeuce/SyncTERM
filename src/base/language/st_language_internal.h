/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_LANGUAGE_INTERNAL_H_
#define ST_LANGUAGE_INTERNAL_H_

#include "st_language.h"

/* Invalid/unsupported identifiers are ignored. Allocation errors propagate. */
int st_language_add(struct st_language_preferences *result,
	const char *locale, size_t size);
int st_language_environment(struct st_language_preferences *result,
	const char *language, const char *all, const char *messages,
	const char *lang);
int st_language_platform(struct st_language_preferences *result);
int st_language_macos(struct st_language_preferences *result);

#endif
