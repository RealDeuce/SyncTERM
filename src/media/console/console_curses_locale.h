/* Private locale scope for curses' explicitly byte-encoded terminal mode. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_CURSES_LOCALE_H_
#define ST_CONSOLE_CURSES_LOCALE_H_

struct st_curses_locale;

int st_curses_locale_create(struct st_curses_locale **result);
void st_curses_locale_destroy(struct st_curses_locale *locale);
int st_curses_locale_call(struct st_curses_locale *locale,
	int (*call)(void *), void *argument);

#endif
