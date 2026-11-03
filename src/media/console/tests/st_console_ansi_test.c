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

#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pty_diagnostics.h"

static unsigned char terminal_output[65536];
static size_t terminal_output_size;

static const uint8_t text_font_bitmap[128] = {0};
static const struct st_console_font_mapping text_font_mapping[] = {
	{'A', 'A'}, {'B', 'B'}, {'C', 'C'}, {'D', 'D'}, {'E', 'E'},
	{'F', 'F'}, {'G', 'G'}, {'H', 'H'}, {'I', 'I'}, {'J', 'J'},
	{'K', 'K'}, {'L', 'L'}, {'M', 'M'}, {'N', 'N'}, {'O', 'O'},
	{'P', 'P'}, {'Q', 'Q'}, {'R', 'R'}, {'S', 'S'}, {'T', 'T'},
	{'U', 'U'}, {'V', 'V'}, {'W', 'W'}, {'X', 'X'}, {'Y', 'Y'},
	{'Z', 'Z'},
};
static const struct st_console_font text_font = {
	.bitmap = text_font_bitmap,
	.bitmap_size = sizeof(text_font_bitmap),
	.width = 8,
	.height = 1,
	.glyph_count = 128,
	.row_stride = 1,
	.glyph_stride = 1,
	.mapping = text_font_mapping,
	.mapping_count = sizeof(text_font_mapping) / sizeof(text_font_mapping[0]),
};

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
	bool found = output_contains(text);
	if (!found)
		fprintf(stderr, "ANSI timed out waiting for text [%s]\n", text);
	return found;
}

static bool
next_event(st_console_t *console, struct st_console_event *event)
{
	struct timespec deadline;

	deadline_after(&deadline);
	return st_console_event_timedwait(console, event, &deadline) == 0;
}

static bool
expect_raw_escape(st_console_t *console, const char *tail)
{
	struct st_console_event event;

	if (!next_event(console, &event)
	    || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 27)
		return false;
	for (; *tail != '\0'; ++tail) {
		if (!next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != (unsigned char)*tail
		    || event.data.text.modifiers != 0)
			return false;
	}
	return true;
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
	uint16_t font_identifier;

	if (st_console_create(&console, &config_display, &config) != 0
	    || st_console_size(console, &columns, &rows) != 0
	    || columns != 91 || rows != 37
	    || st_console_capabilities(console, &capabilities) != 0
	    || (capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) == 0
	    || (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) == 0) {
		report(descriptor, 'C');
		_exit(1);
	}
	if (st_console_register_static_font(console, &text_font,
	    &font_identifier) != 0 || font_identifier != 0) {
		report(descriptor, 'F');
		goto failed;
	}
	report(descriptor, 'R');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 13) {
		report(descriptor, 'K');
		goto failed;
	}
	report(descriptor, 'K');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 3
	    || event.data.key.modifiers != ST_CONSOLE_MODIFIER_CONTROL)
		goto failed;
	report(descriptor, 'c');
	static const uint16_t shortcuts[] = {6, 19, 4, 7, 17};
	for (size_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); ++i) {
		if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
		    || event.data.key.code != shortcuts[i]
		    || event.data.key.modifiers != ST_CONSOLE_MODIFIER_CONTROL)
			goto failed;
	}
	report(descriptor, 'H');
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
	report(descriptor, 'E');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != ST_CONSOLE_KEY_UP
	    || event.data.key.modifiers != ST_CONSOLE_MODIFIER_SHIFT)
		goto failed;
	report(descriptor, '3');
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != ST_CONSOLE_KEY_UP
	    || event.data.key.modifiers != 0)
		goto failed;
	report(descriptor, 'M');
	if (!expect_raw_escape(console, "[1;2;3;4A"))
		goto failed;
	report(descriptor, '4');
	if (!expect_raw_escape(console, "[999999999999999999999999999999A"))
		goto failed;
	report(descriptor, 'O');
	{
		const char text[] = "REPAINT";
		const struct st_console_rect rectangle = {0, 20, 7, 1};
		const struct st_console_rect top = {0, 1, 6, 1};
		struct st_console_cell marker[7] = {0};

		for (size_t i = 0; i < 7; ++i) {
			marker[i].glyph = (uint32_t)text[i];
			marker[i].foreground = 7;
		}
		if (st_console_write_cells(console, &rectangle, marker, 7) != 0)
			goto failed;
		for (size_t i = 0; i < 6; ++i)
			marker[i].glyph = (uint32_t)"SHRINK"[i];
		if (st_console_write_cells(console, &top, marker, 6) != 0
		    || st_console_present(console) != 0)
			goto failed;
	}
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

enum redirected_case {
	REDIRECTED_TEXT,
	REDIRECTED_FILE,
	REDIRECTED_ESCAPE,
	REDIRECTED_UTF8,
	REDIRECTED_MALFORMED,
	REDIRECTED_STOP,
};

static void
redirected_child(int descriptor, enum redirected_case test)
{
	const struct st_console_display_config display_config = {
		.backend = ST_CONSOLE_BACKEND_ANSI,
		.text_presentation = ST_CONSOLE_TEXT_UNICODE,
	};
	const struct st_console_config console_config = {
		.columns = 8,
		.rows = 3,
		.wrap = true,
	};
	st_console_t *console = NULL;
	struct st_console_event event = {0};
	const char *stage = "input";

	if (st_console_create(&console, &display_config, &console_config) != 0) {
		report(descriptor, 'C');
		_exit(1);
	}
	report(descriptor, 'R');
	if (test == REDIRECTED_STOP) {
		st_console_destroy(console);
		report(descriptor, 'X');
		_exit(0);
	}
	if (test == REDIRECTED_TEXT || test == REDIRECTED_FILE) {
		if (!next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != 'x')
			goto failed;
	}
	else if (test == REDIRECTED_ESCAPE) {
		if (!next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_KEY_CODE
		    || event.data.key.code != 27
		    || !next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != '[')
			goto failed;
	}
	else if (test == REDIRECTED_UTF8) {
		if (!next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != UINT32_C(0xfffd))
			goto failed;
	}
	else if (test == REDIRECTED_MALFORMED) {
		for (size_t i = 0; i < 3; ++i) {
			if (!next_event(console, &event)
			    || event.type != ST_CONSOLE_EVENT_TEXT
			    || event.data.text.codepoint != UINT32_C(0xfffd))
				goto failed;
		}
		if (!next_event(console, &event)
		    || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != 'B')
			goto failed;
	}
	stage = "close";
	if (!next_event(console, &event) || event.type != ST_CONSOLE_EVENT_CLOSE)
		goto failed;
	stage = "queue after close";
	if (st_console_event_try(console, &event) != IO_ERROR_WOULD_BLOCK)
		goto failed;
	st_console_destroy(console);
	report(descriptor, 'X');
	_exit(0);

failed:
	fprintf(stderr, "ANSI redirected case=%d stage=%s last-event=%d"
	    " text=U+%04x key=%u\n", test, stage, event.type,
	    event.type == ST_CONSOLE_EVENT_TEXT ? (unsigned)event.data.text.codepoint : 0,
	    event.type == ST_CONSOLE_EVENT_KEY_CODE ? (unsigned)event.data.key.code : 0);
	st_console_destroy(console);
	report(descriptor, 'F');
	_exit(1);
}

static int
wait_byte(int descriptor, unsigned char expected)
{
	struct pollfd poll_descriptor = {.fd = descriptor, .events = POLLIN};
	unsigned char value = '?';

	if (poll(&poll_descriptor, 1, 5000) <= 0
	    || read(descriptor, &value, 1) != 1 || value != expected) {
		fprintf(stderr, "ANSI redirected report expected=%c actual=%c"
		    " revents=%x errno=%d\n", expected, value,
		    (unsigned short)poll_descriptor.revents, errno);
		return -1;
	}
	return 0;
}

static bool
run_redirected_case(enum redirected_case test, const void *data, size_t size)
{
	int input_read;
	int input_write;
	int reports[2];
	pid_t child;
	int status;

	if (test == REDIRECTED_FILE) {
		char path[96];

		if (snprintf(path, sizeof(path), "/tmp/st-console-ansi-%ld",
		    (long)getpid()) < 0)
			return false;
		input_read = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
		if (input_read < 0)
			return false;
		(void)unlink(path);
		input_write = -1;
		if (write(input_read, data, size) != (ssize_t)size
		    || lseek(input_read, 0, SEEK_SET) != 0) {
			(void)close(input_read);
			return false;
		}
	}
	else {
		int input[2];

		if (pipe(input) != 0)
			return false;
		input_read = input[0];
		input_write = input[1];
	}
	if (pipe(reports) != 0) {
		(void)close(input_read);
		if (input_write >= 0)
			(void)close(input_write);
		return false;
	}
	child = fork();
	if (child < 0) {
		(void)close(input_read);
		if (input_write >= 0)
			(void)close(input_write);
		(void)close(reports[0]);
		(void)close(reports[1]);
		return false;
	}
	if (child == 0) {
		int null_descriptor = open("/dev/null", O_WRONLY);

		if (input_write >= 0)
			(void)close(input_write);
		(void)close(reports[0]);
		if (null_descriptor < 0
		    || dup2(input_read, STDIN_FILENO) != STDIN_FILENO
		    || dup2(null_descriptor, STDOUT_FILENO) != STDOUT_FILENO)
			_exit(1);
		(void)close(input_read);
		(void)close(null_descriptor);
		redirected_child(reports[1], test);
	}
	(void)close(input_read);
	(void)close(reports[1]);
	if (wait_byte(reports[0], 'R') != 0)
		goto failed;
	if (test != REDIRECTED_FILE && size != 0
	    && write(input_write, data, size) != (ssize_t)size)
		goto failed;
	if (test != REDIRECTED_STOP && input_write >= 0) {
		(void)close(input_write);
		input_write = -1;
	}
	if (wait_byte(reports[0], 'X') != 0)
		goto failed;
	if (input_write >= 0)
		(void)close(input_write);
	(void)close(reports[0]);
	return waitpid(child, &status, 0) == child && WIFEXITED(status)
	    && WEXITSTATUS(status) == 0;

failed:
	fprintf(stderr, "ANSI redirected case=%d failed\n", test);
	if (input_write >= 0)
		(void)close(input_write);
	(void)close(reports[0]);
	(void)kill(child, SIGKILL);
	(void)waitpid(child, &status, 0);
	return false;
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
		int status = poll(descriptors, 2, 5000);
		if (status <= 0) {
			fprintf(stderr, "ANSI report expected=%c poll=%d errno=%d"
			    " report-events=%x PTY-events=%x\n", expected,
			    status, status < 0 ? errno : 0, (unsigned short)descriptors[0].revents,
			    (unsigned short)descriptors[1].revents);
			return -1;
		}
		if ((descriptors[1].revents & POLLIN) != 0)
		{
			ssize_t count = read(master, buffer, sizeof(buffer));

			if (count > 0)
				save_output(buffer, (size_t)count);
		}
		if ((descriptors[0].revents & POLLIN) != 0) {
			unsigned char value = '?';

			if (read(descriptor, &value, 1) != 1 || value != expected) {
				fprintf(stderr, "ANSI child report expected=%c actual=%c\n", expected, value);
				return -1;
			}
			return 0;
		}
		if ((descriptors[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
			fprintf(stderr, "ANSI child report closed before stage %c\n", expected);
			return -1;
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
	    || write(master, "\003", 1) != 1
	    || wait_report(reports[0], master, 'c') != 0
	    || write(master, "\006\023\004\007\021", 5) != 5
	    || wait_report(reports[0], master, 'H') != 0
	    || write(master, "\033[1;3A", 6) != 6
	    || wait_report(reports[0], master, 'A') != 0
	    || write(master, "\033y", 2) != 2
	    || wait_report(reports[0], master, 'T') != 0
	    || write(master, "\303\251", 2) != 2
	    || wait_report(reports[0], master, 'U') != 0
	    || write(master, "\000\056", 2) != 2
	    || wait_report(reports[0], master, 'D') != 0
	    || write(master, "\000\340\034", 3) != 3
	    || wait_report(reports[0], master, 'E') != 0
	    || write(master, "\033[1;2;3A", sizeof("\033[1;2;3A") - 1)
	    != (ssize_t)(sizeof("\033[1;2;3A") - 1)
	    || wait_report(reports[0], master, '3') != 0
	    || write(master, "\033[1;;A", sizeof("\033[1;;A") - 1)
	    != (ssize_t)(sizeof("\033[1;;A") - 1)
	    || wait_report(reports[0], master, 'M') != 0
	    || write(master, "\033[1;2;3;4A", sizeof("\033[1;2;3;4A") - 1)
	    != (ssize_t)(sizeof("\033[1;2;3;4A") - 1)
	    || wait_report(reports[0], master, '4') != 0
	    || write(master, "\033[999999999999999999999999999999A",
	    sizeof("\033[999999999999999999999999999999A") - 1)
	    != (ssize_t)(sizeof("\033[999999999999999999999999999999A")
	    - 1)
	    || wait_report(reports[0], master, 'O') != 0)
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
	if (!run_redirected_case(REDIRECTED_TEXT, "x", 1)
	    || !run_redirected_case(REDIRECTED_FILE, "x", 1)
	    || !run_redirected_case(REDIRECTED_ESCAPE, "\033[", 2)
	    || !run_redirected_case(REDIRECTED_UTF8, "\303", 1)
	    || !run_redirected_case(REDIRECTED_MALFORMED,
	    "\340\200\200B", 4)
	    || !run_redirected_case(REDIRECTED_STOP, NULL, 0))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;

failed:
	(void)close(reports[0]);

failed_after_report:
	dump_terminal_output(terminal_output, terminal_output_size);
	(void)kill(child, SIGKILL);
	(void)waitpid(child, &child_status, 0);
	(void)close(master);
	return EXIT_FAILURE;
}
