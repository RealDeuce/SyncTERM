/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "console_ansi_system.h"

#include "io_error.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

struct st_console_ansi_system {
	struct termios original;
	bool restore_input;
	bool regular_input;
};

static bool
descriptor_size(int descriptor, uint32_t *columns, uint32_t *rows)
{
	struct winsize size;

	if (ioctl(descriptor, TIOCGWINSZ, &size) != 0
	    || size.ws_col == 0 || size.ws_row == 0)
		return false;
	*columns = size.ws_col;
	*rows = size.ws_row;
	return true;
}

int
st_console_ansi_system_open(struct st_console_ansi_system **result,
	bool *input_available, bool utf8)
{
	struct st_console_ansi_system *system;
	struct termios raw;
	struct stat input_status;

	(void)utf8;

	if (result == NULL || input_available == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	system = calloc(1, sizeof(*system));
	if (system == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*input_available = false;
	system->regular_input = fstat(STDIN_FILENO, &input_status) == 0
	    && S_ISREG(input_status.st_mode);
	if (isatty(STDIN_FILENO)) {
		if (tcgetattr(STDIN_FILENO, &system->original) != 0) {
			free(system);
			return io_error_from_errno(errno);
		}
		raw = system->original;
		raw.c_iflag &= (tcflag_t)~(IGNBRK | BRKINT | PARMRK | ISTRIP
		    | INLCR | IGNCR | ICRNL | IXON);
		raw.c_oflag &= (tcflag_t)~OPOST;
		raw.c_cflag &= (tcflag_t)~(CSIZE | PARENB);
		raw.c_cflag |= CS8;
		raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
			free(system);
			return io_error_from_errno(errno);
		}
		system->restore_input = true;
	}
	if (fcntl(STDIN_FILENO, F_GETFL) >= 0)
		*input_available = true;
	*result = system;
	return 0;
}

int
st_console_ansi_system_read(struct st_console_ansi_system *system,
	void *buffer, size_t size, size_t *received, unsigned int timeout_ms)
{
	struct pollfd descriptor = {.fd = STDIN_FILENO, .events = POLLIN};
	int timeout = timeout_ms > (unsigned int)INT_MAX
	    ? INT_MAX : (int)timeout_ms;
	int status;
	ssize_t count;

	if (system == NULL || buffer == NULL || size == 0 || received == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*received = 0;
	/* A regular file cannot block. Some poll implementations stop reporting
	 * readability at EOF, which would otherwise hide the terminal close. */
	status = system->regular_input ? 1 : poll(&descriptor, 1, timeout);
	if (status == 0)
		return 0;
	if (status < 0)
		return errno == EINTR ? 0 : io_error_from_errno(errno);
	if (!system->regular_input && (descriptor.revents & POLLIN) == 0) {
		if ((descriptor.revents & POLLHUP) != 0)
			return IO_ERROR_NOT_CONNECTED;
		return (descriptor.revents & (POLLERR | POLLNVAL)) != 0
		    ? IO_ERROR_IO : 0;
	}
	count = read(STDIN_FILENO, buffer, size);
	if (count < 0)
		return errno == EINTR || errno == EAGAIN ? 0
		    : io_error_from_errno(errno);
	if (count == 0)
		return IO_ERROR_NOT_CONNECTED;
	*received = (size_t)count;
	return 0;
}

void
st_console_ansi_system_stop(struct st_console_ansi_system *system)
{
	(void)system;
}

bool
st_console_ansi_system_size(struct st_console_ansi_system *system,
	uint32_t *columns, uint32_t *rows)
{
	(void)system;
	return descriptor_size(STDOUT_FILENO, columns, rows)
	    || descriptor_size(STDIN_FILENO, columns, rows);
}

void
st_console_ansi_system_close(struct st_console_ansi_system *system)
{
	if (system == NULL)
		return;
	if (system->restore_input)
		(void)tcsetattr(STDIN_FILENO, TCSANOW, &system->original);
	free(system);
}
