/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_windows_launcher.h"
#include "st_application.h"
#include "st_console_test.h"

static int
console_application(int argc, char **argv)
{
	const struct st_application_host host = {.console_mode = true};
	return st_application_run(argc, argv, &host);
}

static int
graphical_application(int argc, char **argv)
{
	return st_application_run(argc, argv, NULL);
}

#ifdef ST_APP_USE_SDL_DISPATCH
int st_console_sdl_run_application(int argc, char **argv,
	int (*application_main)(int, char **));
#endif

int
st_application_main(int argc, char **argv, int console_mode)
{
	int (*application)(int, char **) = console_mode != 0
	    ? console_application : graphical_application;
#ifdef ST_APP_USE_SDL_DISPATCH
#if ST_CONSOLE_TEST
	if (st_console_test_requested(argc, argv)) return application(argc, argv);
#endif
	return st_console_sdl_run_application(argc, argv, application);
#else
	return application(argc, argv);
#endif
}
