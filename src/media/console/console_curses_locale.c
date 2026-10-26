/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_curses_locale.h"
#include "console_config.h"
#include "io_error.h"

#include <stdlib.h>

#if ST_CONSOLE_HAVE_THREAD_LOCALE
#include <locale.h>

struct st_curses_locale {
	locale_t value;
};
#endif

int
st_curses_locale_create(struct st_curses_locale **result)
{
	*result = NULL;
#if ST_CONSOLE_HAVE_THREAD_LOCALE
	struct st_curses_locale *locale = malloc(sizeof(*locale));

	if (locale == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	locale->value = newlocale(LC_CTYPE_MASK, "C", (locale_t)0);
	if (locale->value == (locale_t)0) {
		free(locale);
		return IO_ERROR_NOT_SUPPORTED;
	}
	*result = locale;
#endif
	return 0;
}

void
st_curses_locale_destroy(struct st_curses_locale *locale)
{
#if ST_CONSOLE_HAVE_THREAD_LOCALE
	if (locale != NULL) {
		freelocale(locale->value);
		free(locale);
	}
#else
	(void)locale;
#endif
}

int
st_curses_locale_call(struct st_curses_locale *locale,
	int (*call)(void *), void *argument)
{
#if ST_CONSOLE_HAVE_THREAD_LOCALE
	if (locale != NULL) {
		locale_t previous = uselocale(locale->value);
		int status;

		if (previous == (locale_t)0)
			return IO_ERROR_IO;
		status = call(argument);
		if (uselocale(previous) == (locale_t)0 && status == 0)
			status = IO_ERROR_IO;
		return status;
	}
#else
	(void)locale;
#endif
	return call(argument);
}
