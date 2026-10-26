/* Both thin Windows launchers call the same application/libretro DLL. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_windows_launcher.h"
#include "st_windows_utf.h"
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

static int
launch(void)
{
	int argc = 0, result = 1;
	wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
	char **argv = NULL;
	if (wide == NULL || argc <= 0) goto finished;
	argv = calloc((size_t)argc + 1, sizeof(*argv));
	if (argv == NULL) goto finished;
	for (int i = 0; i < argc; ++i)
		if (st_windows_utf16_to_utf8(&argv[i], wide[i]) != 0) goto finished;
#ifdef ST_LAUNCHER_GUI
	result = st_application_main(argc, argv, 0);
#else
	result = st_application_main(argc, argv, 1);
#endif
finished:
	if (argv != NULL) for (int i = 0; i < argc; ++i) free(argv[i]);
	free(argv);
	if (wide != NULL) LocalFree(wide);
	return result;
}

#ifdef ST_LAUNCHER_GUI
int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
	(void)instance; (void)previous; (void)command; (void)show;
	return launch();
}
#else
int
main(void)
{
	return launch();
}
#endif
