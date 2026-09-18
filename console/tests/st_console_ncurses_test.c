/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include "io_error.h"

#if ST_CONSOLE_TEST_HAVE_PTY_H
	#include <pty.h>
#elif ST_CONSOLE_TEST_HAVE_UTIL_H
	#include <util.h>
#elif ST_CONSOLE_TEST_HAVE_LIBUTIL_H
	#include <libutil.h>
#endif

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned char terminal_output[65536];
static size_t terminal_output_size;

static void
save_output(const unsigned char *data, size_t size)
{
	if (size > sizeof(terminal_output) - terminal_output_size)
		size = sizeof(terminal_output) - terminal_output_size;
	memcpy(&terminal_output[terminal_output_size], data, size);
	terminal_output_size += size;
}

static bool
output_contains(const char *text)
{
	size_t length = strlen(text);

	if (length > terminal_output_size)
		return false;
	for (size_t i = 0; i <= terminal_output_size - length; ++i)
		if (memcmp(&terminal_output[i], text, length) == 0)
			return true;
	return false;
}

static bool
cursor_visible_at_end(void)
{
	bool visible = true;

	for (size_t i = 0; i + 5 < terminal_output_size; ++i) {
		if (memcmp(&terminal_output[i], "\033[?25h", 6) == 0)
			visible = true;
		else if (memcmp(&terminal_output[i], "\033[?25l", 6) == 0)
			visible = false;
	}
	return visible;
}

static void
deadline_after(struct timespec *deadline, time_t seconds)
{
	(void)timespec_get(deadline, TIME_UTC);
	deadline->tv_sec += seconds;
}

static void
child_result(int descriptor, unsigned char result)
{
	(void)write(descriptor, &result, 1);
}

static void
child_test(int report)
{
	struct st_console_config config = {
		.columns = 8,
		.rows = 3,
		.wrap = true,
	};
	st_console_t *console = NULL;
	st_console_t *peer = NULL;
	st_console_display_t *display = NULL;
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_NCURSES,
	};
	struct st_console_event event;
	const struct st_console_rect cells_rect = {0, 0, 3, 1};
	struct st_console_cell cells[3] = {
		{.codepoint = 'A', .foreground = 9, .background = 0},
		{.codepoint = 'B',
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 215, 175),
		    .background = 0},
		{.codepoint = 'C', .foreground = 7, .background = 0,
		    .flags = ST_CONSOLE_CELL_BOLD},
	};
	struct timespec deadline;
	uint32_t columns;
	uint32_t rows;
	uint32_t capabilities;
	int status;

	status = st_console_display_create(&display, &display_config);
	if (status == 0)
		status = st_console_create_on_display(&console, display, &config);
	if (status == IO_ERROR_NOT_FOUND || status == IO_ERROR_NOT_SUPPORTED) {
		child_result(report, 'S');
		_exit(77);
	}
	if (status != 0) {
		child_result(report, 'C');
		_exit(1);
	}
	if (st_console_size(console, &columns, &rows) != 0
	    || columns != 91 || rows != 37) {
		child_result(report, 'D');
		st_console_destroy(console);
		_exit(1);
	}
	if (st_console_capabilities(console, &capabilities) != 0
	    || (capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) == 0
	    || (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) == 0) {
		child_result(report, 'T');
		st_console_destroy(console);
		_exit(1);
	}
	config.cell_width = 8;
	config.cell_height = 16;
	if (st_console_create_on_display(&peer, display, &config) != 0
	    || st_console_capabilities(peer, &capabilities) != 0
	    || (capabilities & ST_CONSOLE_CAP_PIXELS) != 0
	    || st_console_cell_pixel_size(peer, &columns, &rows) != 0
	    || columns != 0 || rows != 0) {
		child_result(report, 'A');
		_exit(1);
	}
	st_console_destroy(peer);
	child_result(report, 'R');
	deadline_after(&deadline, 5);
	status = st_console_event_timedwait(console, &event, &deadline);
	if (status != 0 || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 13) {
		child_result(report, 'K');
		st_console_destroy(console);
		_exit(1);
	}
	child_result(report, 'E');
	{
		const char text[] = "REPAINT";
		const struct st_console_rect rectangle = {0, 1, 7, 1};
		struct st_console_cell marker[7] = {0};

		for (size_t i = 0; i < 7; ++i) {
			marker[i].codepoint = (uint32_t)text[i];
			marker[i].foreground = 7;
		}
		if (st_console_write_cells(console, &rectangle, marker, 7) != 0)
			_exit(1);
	}
	if (st_console_write_cells(console, &cells_rect, cells, 3) != 0
	    || st_console_present(console) != 0) {
		child_result(report, 'P');
		st_console_destroy(console);
		_exit(1);
	}
	child_result(report, 'O');
	deadline_after(&deadline, 5);
	for (;;) {
		status = st_console_event_timedwait(console, &event, &deadline);
		if (status != 0 || event.type == ST_CONSOLE_EVENT_RESIZE)
			break;
	}
	if (status != 0 || event.data.resize.columns != 101
	    || event.data.resize.rows != 43
	    || st_console_size(console, &columns, &rows) != 0
	    || columns != 91 || rows != 37) {
		child_result(report, 'W');
		st_console_destroy(console);
		_exit(1);
	}
	child_result(report, 'Z');
	/* No drawing calls: the backend must repaint the idle screen itself. */
	deadline_after(&deadline, 5);
	do {
		status = st_console_event_timedwait(console, &event, &deadline);
	} while (status == 0 && event.type != ST_CONSOLE_EVENT_KEY_CODE);
	if (status != 0 || event.data.key.code != 13)
		_exit(1);
	st_console_destroy(console);
	st_console_display_destroy(display);
	child_result(report, 'F');
	_exit(0);
}

static int
wait_report(int report, int master, unsigned char expected)
{
	struct pollfd descriptors[2] = {
		{.fd = report, .events = POLLIN},
		{.fd = master, .events = POLLIN},
	};
	unsigned char buffer[4096];

	for (;;) {
		int status = poll(descriptors, 2, 5000);

		if (status <= 0)
			return -1;
		if ((descriptors[1].revents & POLLIN) != 0) {
			ssize_t count = read(master, buffer, sizeof(buffer));

			if (count > 0)
				save_output(buffer, (size_t)count);
		}
		if ((descriptors[0].revents & POLLIN) != 0) {
			unsigned char result;

			if (read(report, &result, 1) != 1)
				return -1;
			if (result == 'S')
				return 77;
			if (result != expected) {
				fprintf(stderr, "ncurses child failed at stage %c\n", result);
				return -1;
			}
			for (;;) {
				struct pollfd output = {.fd = master, .events = POLLIN};
				ssize_t count;

				if (poll(&output, 1, 0) <= 0
				    || (output.revents & POLLIN) == 0)
					break;
				count = read(master, buffer, sizeof(buffer));
				if (count <= 0)
					break;
				save_output(buffer, (size_t)count);
			}
			return 0;
		}
		if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
			return -1;
	}
}

static bool
wait_child(pid_t child, int master, int *child_status)
{
	struct pollfd descriptor = {.fd = master, .events = POLLIN};
	unsigned char buffer[4096];

	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		pid_t result = waitpid(child, child_status, WNOHANG);

		if (result == child)
			return true;
		if (result < 0)
			return false;
		if (poll(&descriptor, 1, 50) > 0
		    && (descriptor.revents & POLLIN) != 0) {
			if (read(master, buffer, sizeof(buffer)) == 0)
				return waitpid(child, child_status, 0) == child;
		}
		if ((descriptor.revents & POLLHUP) != 0)
			return waitpid(child, child_status, 0) == child;
	}
	return false;
}

int
main(void)
{
	struct winsize size = {.ws_row = 37, .ws_col = 91};
	const char *colorterm = getenv("COLORTERM");
	bool direct_colour = colorterm != NULL
	    && (strcmp(colorterm, "truecolor") == 0
	    || strcmp(colorterm, "24bit") == 0);
	int report[2];
	int master;
	pid_t child;
	int child_status;
	int status;

	if (pipe(report) != 0)
		return EXIT_FAILURE;
	child = forkpty(&master, NULL, NULL, &size);
	if (child < 0)
		return EXIT_FAILURE;
	if (child == 0) {
		(void)close(report[0]);
		child_test(report[1]);
	}
	(void)close(report[1]);
	status = wait_report(report[0], master, 'R');
	if (status == 77) {
		(void)waitpid(child, &child_status, 0);
		return 77;
	}
	if (status != 0 || write(master, "\r", 1) != 1
	    || wait_report(report[0], master, 'E') != 0)
		goto failed;
	if (wait_report(report[0], master, 'O') != 0)
		goto failed;
	if ((!direct_colour && (!output_contains("\033[38;5;12m")
	    || !output_contains("\033[38;5;43m")))
	    || (direct_colour
	    && (!output_contains("\033[38;5;12m")
	    || !output_contains("\033[38;2;0;215;175m")))
	    || !output_contains("\033[1m")) {
		fprintf(stderr, "ncurses emitted the wrong colour representation\n");
		goto failed;
	}
	size.ws_row = 43;
	size.ws_col = 101;
	terminal_output_size = 0;
	if (ioctl(master, TIOCSWINSZ, &size) != 0 || kill(child, SIGWINCH) != 0
	    || wait_report(report[0], master, 'Z') != 0)
		goto failed;
	for (unsigned int attempt = 0; attempt < 100
	    && !output_contains("REPAINT"); ++attempt) {
		struct pollfd output = {.fd = master, .events = POLLIN};
		unsigned char bytes[4096];

		if (poll(&output, 1, 50) > 0 && (output.revents & POLLIN) != 0) {
			ssize_t count = read(master, bytes, sizeof(bytes));
			if (count > 0)
				save_output(bytes, (size_t)count);
		}
	}
	if (!output_contains("REPAINT") || write(master, "\r", 1) != 1
	    || wait_report(report[0], master, 'F') != 0)
		goto failed;
	(void)close(report[0]);
	if (!wait_child(child, master, &child_status)
	    || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
		goto failed_after_report;
	if (!cursor_visible_at_end()) {
		fprintf(stderr, "ncurses left the terminal cursor hidden\n");
		goto failed_after_report;
	}
	(void)close(master);
	return EXIT_SUCCESS;

failed:
	(void)close(report[0]);
failed_after_report:
	(void)kill(child, SIGKILL);
	(void)waitpid(child, &child_status, 0);
	(void)close(master);
	return EXIT_FAILURE;
}
