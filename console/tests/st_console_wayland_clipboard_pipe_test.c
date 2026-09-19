/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* A receiver can close its clipboard pipe before the send callback runs. */
#include "../console_wayland.c"
#include <signal.h>
#include <sys/wait.h>

int
main(void)
{
	pid_t child = fork();
	int status;

	if (child < 0)
		return EXIT_FAILURE;
	if (child == 0) {
		char text[] = "clipboard";
		struct state state = {.clipboard = text};
		int descriptors[2];

		if (signal(SIGPIPE, SIG_DFL) == SIG_ERR || pipe(descriptors) != 0)
			_exit(2);
		(void)close(descriptors[0]);
		source_send(&state, NULL, "text/plain;charset=utf-8", descriptors[1]);
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child)
		return EXIT_FAILURE;
	if (WIFSIGNALED(status))
		fprintf(stderr, "clipboard callback terminated child with signal %d\n",
		    WTERMSIG(status));
	return WIFEXITED(status) && WEXITSTATUS(status) == 0
	    ? EXIT_SUCCESS : EXIT_FAILURE;
}
