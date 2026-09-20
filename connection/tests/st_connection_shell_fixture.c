/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_connection_config.h"

#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;

static void
handle_interrupt(int signal_number)
{
	(void)signal_number;
	interrupted = 1;
}

static int
write_all(const char *text)
{
	size_t remaining = strlen(text);

	while (remaining > 0) {
		ssize_t count = write(STDOUT_FILENO, text, remaining);

		if (count <= 0)
			return 1;
		text += count;
		remaining -= (size_t)count;
	}
	return 0;
}

int
main(void)
{
	struct termios attributes;
	char input[32];
	ssize_t count;

	if (tcgetattr(STDIN_FILENO, &attributes) != 0)
		return 1;
	if ((attributes.c_iflag & ICRNL) == 0
	    || (attributes.c_iflag & (IXON | IXOFF)) != 0
	    || (attributes.c_oflag & OPOST) == 0
	    || (attributes.c_lflag & (ECHO | ICANON | ISIG))
	    != (ECHO | ICANON | ISIG)
	    || attributes.c_cc[VERASE] != '\b')
		return 2;
#if ST_CONNECTION_HAVE_IXANY
	if ((attributes.c_iflag & IXANY) != 0)
		return 2;
#endif
#if ST_CONNECTION_HAVE_VERASE2
	if (attributes.c_cc[VERASE2] != '\b')
		return 2;
#endif
#if ST_CONNECTION_HAVE_B115200
	if (cfgetispeed(&attributes) != B115200
	    || cfgetospeed(&attributes) != B115200)
		return 2;
#endif
	if (signal(SIGINT, handle_interrupt) == SIG_ERR)
		return 3;
	if (write_all("flags-ok\nready\n") != 0)
		return 4;
	count = read(STDIN_FILENO, input, sizeof(input));
	if (count <= 0 || input[count - 1] != '\n')
		return 5;
	if (write_all("line-ok\nsignal-ready\n") != 0)
		return 6;
	for (unsigned int elapsed = 0; !interrupted && elapsed < 5000;
	    elapsed += 10)
		(void)poll(NULL, 0, 10);
	if (!interrupted)
		return 7;
	return write_all("signal-ok\n");
}
