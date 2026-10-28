/* Native entry point; the console main-thread dispatcher may wrap main. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_application.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	/* POSIX executable policy; libretro hosts own their signal dispositions. */
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		fputs("SyncTERM: cannot ignore SIGPIPE\n", stderr);
		return EXIT_FAILURE;
	}
	return st_application_run(argc, argv, NULL);
}
