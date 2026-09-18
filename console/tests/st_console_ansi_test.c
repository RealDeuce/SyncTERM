/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#if ST_CONSOLE_TEST_HAVE_PTY_H
	#include <pty.h>
#elif ST_CONSOLE_TEST_HAVE_UTIL_H
	#include <util.h>
#elif ST_CONSOLE_TEST_HAVE_LIBUTIL_H
	#include <libutil.h>
#endif

#include <poll.h>
#include <signal.h>
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

static void
deadline_after(struct timespec *deadline)
{
	(void)timespec_get(deadline, TIME_UTC);
	deadline->tv_sec += 5;
}

static bool
wait_text(int master, const char *text)
{
	for (unsigned int i = 0; i < 100 && !output_contains(text); ++i) {
		struct pollfd output = {.fd = master, .events = POLLIN};
		unsigned char bytes[4096];

		if (poll(&output, 1, 50) > 0 && (output.revents & POLLIN) != 0) {
			ssize_t count = read(master, bytes, sizeof(bytes));
			if (count > 0)
				save_output(bytes, (size_t)count);
		}
	}
	return output_contains(text);
}

static bool
next_event(st_console_t *console, struct st_console_event *event)
{
	struct timespec deadline;

	deadline_after(&deadline);
	return st_console_event_timedwait(console, event, &deadline) == 0;
}

static void
report(int descriptor, unsigned char value)
{
	(void)write(descriptor, &value, 1);
}

static void
child_test(int descriptor)
{
	const struct st_console_display_config config_display = {
		.backend = ST_CONSOLE_BACKEND_ANSI,
		.text_presentation = ST_CONSOLE_TEXT_UNICODE,
	};
	const struct st_console_config config = {
		.columns = 8,
		.rows = 3,
		.wrap = true,
	};
	st_console_t *console = NULL;
	struct st_console_event event;
	uint32_t columns;
	uint32_t rows;
	uint32_t capabilities;

	if (st_console_create(&console, &config_display, &config) != 0
	    || st_console_size(console, &columns, &rows) != 0
	    || columns != 91 || rows != 37
	    || st_console_capabilities(console, &capabilities) != 0
	    || (capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) == 0
	    || (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) == 0) {
		report(descriptor, 'C');
		_exit(1);
	}
	report(descriptor, 'R');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 13) {
		report(descriptor, 'K');
		goto failed;
	}
	report(descriptor, 'K');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != ST_CONSOLE_KEY_UP
	    || event.data.key.modifiers != ST_CONSOLE_MODIFIER_ALT) {
		report(descriptor, 'A');
		goto failed;
	}
	report(descriptor, 'A');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_TEXT
	    || event.data.text.codepoint != 'y'
	    || event.data.text.modifiers != ST_CONSOLE_MODIFIER_ALT) {
		report(descriptor, 'T');
		goto failed;
	}
	report(descriptor, 'T');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_TEXT
	    || event.data.text.codepoint != UINT32_C(0x00e9)
	    || event.data.text.modifiers != 0) {
		report(descriptor, 'U');
		goto failed;
	}
	report(descriptor, 'U');
	if (!next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_DOORWAY_KEY
	    || event.data.doorway_key.scan_code != 0x2e
	    || event.data.doorway_key.enhanced) {
		report(descriptor, 'D');
		goto failed;
	}
	report(descriptor, 'D');
	if (!next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_DOORWAY_KEY
	    || event.data.doorway_key.scan_code != 0x1c
	    || !event.data.doorway_key.enhanced) {
		report(descriptor, 'E');
		goto failed;
	}
	{
		const char text[] = "REPAINT";
		const struct st_console_rect rectangle = {0, 20, 7, 1};
		const struct st_console_rect top = {0, 1, 6, 1};
		struct st_console_cell marker[7] = {0};

		for (size_t i = 0; i < 7; ++i) {
			marker[i].codepoint = (uint32_t)text[i];
			marker[i].foreground = 7;
		}
		if (st_console_write_cells(console, &rectangle, marker, 7) != 0)
			goto failed;
		for (size_t i = 0; i < 6; ++i)
			marker[i].codepoint = (uint32_t)"SHRINK"[i];
		if (st_console_write_cells(console, &top, marker, 6) != 0
		    || st_console_present(console) != 0)
			goto failed;
	}
	report(descriptor, 'E');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_RESIZE
	    || event.data.resize.columns != 40 || event.data.resize.rows != 10)
		goto failed;
	report(descriptor, 'H');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_RESIZE
	    || event.data.resize.columns != 101 || event.data.resize.rows != 43
	    || st_console_size(console, &columns, &rows) != 0
	    || columns != 91 || rows != 37) {
		report(descriptor, 'Z');
		goto failed;
	}
	report(descriptor, 'Z');
	/* Stay idle until the parent observes a backend-driven repaint. */
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 13)
		goto failed;
	st_console_destroy(console);
	report(descriptor, 'X');
	_exit(0);

failed:
	st_console_destroy(console);
	_exit(1);
}

static int
wait_report(int descriptor, int master, unsigned char expected)
{
	struct pollfd descriptors[2] = {
		{.fd = descriptor, .events = POLLIN},
		{.fd = master, .events = POLLIN},
	};
	unsigned char buffer[4096];

	for (;;) {
		if (poll(descriptors, 2, 5000) <= 0)
			return -1;
		if ((descriptors[1].revents & POLLIN) != 0)
		{
			ssize_t count = read(master, buffer, sizeof(buffer));

			if (count > 0)
				save_output(buffer, (size_t)count);
		}
		if ((descriptors[0].revents & POLLIN) != 0) {
			unsigned char value = '?';

			if (read(descriptor, &value, 1) != 1 || value != expected) {
				fprintf(stderr, "ANSI child failed at stage %c\n", value);
				return -1;
			}
			return 0;
		}
	}
}

int
main(void)
{
	struct winsize size = {.ws_row = 37, .ws_col = 91};
	int reports[2];
	int master;
	pid_t child;
	int child_status;

	if (pipe(reports) != 0)
		return EXIT_FAILURE;
	child = forkpty(&master, NULL, NULL, &size);
	if (child < 0)
		return EXIT_FAILURE;
	if (child == 0) {
		(void)close(reports[0]);
		child_test(reports[1]);
	}
	(void)close(reports[1]);
	if (wait_report(reports[0], master, 'R') != 0
	    || !output_contains("\033[=255h")
	    || write(master, "\r", 1) != 1
	    || wait_report(reports[0], master, 'K') != 0
	    || write(master, "\033[1;3A", 6) != 6
	    || wait_report(reports[0], master, 'A') != 0
	    || write(master, "\033y", 2) != 2
	    || wait_report(reports[0], master, 'T') != 0
	    || write(master, "\303\251", 2) != 2
	    || wait_report(reports[0], master, 'U') != 0
	    || write(master, "\000\056", 2) != 2
	    || wait_report(reports[0], master, 'D') != 0
	    || write(master, "\000\340\034", 3) != 3
	    || wait_report(reports[0], master, 'E') != 0)
		goto failed;
	if (!wait_text(master, "REPAINT"))
		goto failed;
	terminal_output_size = 0;
	size.ws_row = 10;
	size.ws_col = 40;
	if (ioctl(master, TIOCSWINSZ, &size) != 0
	    || wait_report(reports[0], master, 'H') != 0
	    || !wait_text(master, "SHRINK") || output_contains("REPAINT"))
		goto failed;
	terminal_output_size = 0;
	size.ws_row = 43;
	size.ws_col = 101;
	if (ioctl(master, TIOCSWINSZ, &size) != 0
	    || wait_report(reports[0], master, 'Z') != 0
	    || !wait_text(master, "REPAINT")
	    || write(master, "\r", 1) != 1
	    || wait_report(reports[0], master, 'X') != 0)
		goto failed;
	(void)close(reports[0]);
	if (!output_contains("\033[r\033[0m\033[?25h\033[0 q\033[=255l")) {
		fprintf(stderr, "ANSI shutdown did not restore the cursor\n");
		goto failed_after_report;
	}
	if (waitpid(child, &child_status, 0) != child
	    || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
		return EXIT_FAILURE;
	(void)close(master);
	return EXIT_SUCCESS;

failed:
	(void)close(reports[0]);

failed_after_report:
	(void)kill(child, SIGKILL);
	(void)waitpid(child, &child_status, 0);
	(void)close(master);
	return EXIT_FAILURE;
}
