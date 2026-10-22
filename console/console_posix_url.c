/* Launch the POSIX desktop URL handler without waiting for the browser. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "console_internal.h"
#include "console_posix_url.h"
#include "io_error.h"

#include <errno.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int
st_console_posix_open_url(st_console_display_t *display, const char *url)
{
	(void)display;
	if (url == NULL || url[0] == '\0') return IO_ERROR_INVALID_ARGUMENT;
	/* The URI is a separate positional argument, never shell source. The
	 * short-lived shell owns/reparents the asynchronous desktop handler;
	 * SyncTERM reaps only that shell and never waits for a browser to close. */
	char *const arguments[] = {
		"sh", "-c", "command -v xdg-open >/dev/null 2>&1 || exit 127\n"
		"xdg-open \"$1\" </dev/null >/dev/null 2>&1 &",
		"syncterm-url", (char *)url, NULL,
	};
	pid_t child;
	int status;
	int result = posix_spawn(&child, "/bin/sh", NULL, NULL, arguments, environ);
	if (result != 0) return IO_ERROR_IO;
	pid_t waited;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited != child || !WIFEXITED(status)) return IO_ERROR_IO;
	if (WEXITSTATUS(status) == 127) return IO_ERROR_NOT_FOUND;
	return WEXITSTATUS(status) == 0 ? 0 : IO_ERROR_IO;
}
