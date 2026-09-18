/* Main-thread-aware application entry point. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_config.h"

int st_console_application_main(int argc, char **argv);
int st_console_sdl_run_application(int argc, char **argv,
	int (*application_main)(int, char **));
int st_console_quartz_run_application(int argc, char **argv,
	int (*application_main)(int, char **));

int
main(int argc, char **argv)
{
#if ST_CONSOLE_HAVE_QUARTZ
	return st_console_quartz_run_application(argc, argv,
	    st_console_application_main);
#else
	return st_console_sdl_run_application(argc, argv,
	    st_console_application_main);
#endif
}
