/* Native entry point; the console main-thread dispatcher may wrap main. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_application.h"

int
main(int argc, char **argv)
{
	return st_application_run(argc, argv, NULL);
}
