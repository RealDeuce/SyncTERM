/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_console.h"

#include "io_error.h"
#include "st_time.h"

#if ST_CONSOLE_TEST_HAVE_PTY_H
	#include <pty.h>
#elif ST_CONSOLE_TEST_HAVE_UTIL_H
	#include <util.h>
#elif ST_CONSOLE_TEST_HAVE_LIBUTIL_H
	#include <libutil.h>
#endif

#include <errno.h>
#include <locale.h>
#include <limits.h>
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
#include <wchar.h>

#include "pty_diagnostics.h"

static unsigned char terminal_output[65536];
static size_t terminal_output_size;

static bool
select_utf8_locale(void)
{
	static const char *const names[] = {"C.UTF-8", "en_US.UTF-8", "UTF-8"};

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		char text[MB_LEN_MAX];
		mbstate_t state = {0};

		if (setlocale(LC_CTYPE, names[i]) != NULL
		    && wcrtomb(text, (wchar_t)UINT32_C(0x00e9), &state) == 2
		    && memcmp(text, "\xc3\xa9", 2) == 0)
			return true;
	}
	return false;
}

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
wait_for_output(int descriptor, const char *text, size_t read_size, int timeout_ms)
{
	unsigned char bytes[4096];
	struct st_deadline deadline;

	if (read_size == 0 || st_deadline_start(&deadline, timeout_ms) != 0)
		return false;
	if (read_size > sizeof(bytes))
		read_size = sizeof(bytes);
	/* Per-character flushes can make hundreds of reads immediately ready. */
	while (!output_contains(text)) {
		struct pollfd output = {.fd = descriptor, .events = POLLIN};
		int64_t remaining;
		int status;
		ssize_t count;

		if (st_deadline_remaining_ms(&deadline, &remaining) != 0 || remaining == 0)
			return false;
		status = poll(&output, 1, remaining > 50 ? 50 : (int)remaining);
		if (status == 0 || (status < 0 && errno == EINTR))
			continue;
		if (status < 0 || (output.revents & POLLIN) == 0)
			return false;
		count = read(descriptor, bytes, read_size);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return false;
		save_output(bytes, (size_t)count);
	}
	return true;
}

static int
test_fragmented_output(void)
{
	char frame[135];

	memset(frame, '?', sizeof(frame));
	memcpy(frame + 128, "REPAINT", 7);
	/* Exercise fragmented success, EOF before the marker, and an idle writer. */
	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		int descriptors[2];
		size_t size = scenario == 0 ? sizeof(frame) : scenario == 1 ? 128 : 0;

		terminal_output_size = 0;
		if (pipe(descriptors) != 0)
			return EXIT_FAILURE;
		if (size != 0 && write(descriptors[1], frame, size) != (ssize_t)size) {
			(void)close(descriptors[0]);
			(void)close(descriptors[1]);
			return EXIT_FAILURE;
		}
		if (scenario != 2)
			(void)close(descriptors[1]);
		bool found = wait_for_output(descriptors[0], "REPAINT", 1,
		    scenario == 2 ? 20 : 5000);
		(void)close(descriptors[0]);
		if (scenario == 2)
			(void)close(descriptors[1]);
		if (found != (scenario == 0) || terminal_output_size != size
		    || memcmp(terminal_output, frame, size) != 0) {
			fprintf(stderr, "fragmented repaint capture case %u: found=%d bytes=%zu\n",
			    scenario, found, terminal_output_size);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static bool
output_contains_byte(unsigned char byte)
{
	for (size_t i = 0; i < terminal_output_size; ++i)
		if (terminal_output[i] == byte)
			return true;
	return false;
}

static bool
output_selects_colour(unsigned index)
{
	char sequence[40];

	(void)snprintf(sequence, sizeof(sequence), "\033[38;5;%um", index);
	if (output_contains(sequence))
		return true;
	if (index < 16) {
		(void)snprintf(sequence, sizeof(sequence), "\033[%um",
		    index < 8 ? 30 + index : 90 + index - 8);
		if (output_contains(sequence))
			return true;
	}
	return false;
}

static bool
output_programs_colour(unsigned red, unsigned green, unsigned blue)
{
	for (size_t i = 0; i + 4 < terminal_output_size; ++i) {
		char command[80];
		unsigned index, r, g, b;
		size_t size = terminal_output_size - i - 4;

		if (memcmp(terminal_output + i, "\033]4;", 4) != 0)
			continue;
		if (size >= sizeof(command))
			size = sizeof(command) - 1;
		memcpy(command, terminal_output + i + 4, size);
		command[size] = '\0';
		/* xterm initc converts ncurses' 0..1000 channels back to 0..255;
		 * allow its one-unit rounding, but still require the programmed
		 * slot to be selected for output. */
		if (sscanf(command, "%u;rgb:%2x/%2x/%2x", &index, &r, &g, &b) == 4
		    && abs((int)r - (int)red) <= 1
		    && abs((int)g - (int)green) <= 1
		    && abs((int)b - (int)blue) <= 1
		    && output_selects_colour(index))
			return true;
	}
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
child_test(int report, bool cp437)
{
	size_t caller_mb_max = (size_t)MB_CUR_MAX;
	static const uint8_t bitmap[128] = {0};
	static const uint8_t wide_bitmap[2] = {0};
	static const char joystick[] = "\xf0\x9f\x95\xb9\xef\xb8\x8f";
	static const struct st_console_font_cluster_mapping clusters[] = {
		{joystick, sizeof(joystick) - 1, 128},
	};
	static const struct st_console_font_mapping mapping[] = {
		{'?', '?'},
		{'A', 'A'}, {'B', 'B'}, {'C', 'C'}, {'D', 'D'}, {'E', 'E'},
		{'F', 'F'}, {'G', 'G'}, {'H', 'H'}, {'I', 'I'}, {'J', 'J'},
		{'K', 'K'}, {'L', 'L'}, {'M', 'M'}, {'N', 'N'}, {'O', 'O'},
		{'P', 'P'}, {'Q', 'Q'}, {'R', 'R'}, {'S', 'S'}, {'T', 'T'},
		{'U', 'U'}, {'V', 'V'}, {'W', 'W'}, {'X', 'X'}, {'Y', 'Y'},
		{'Z', 'Z'},
		{UINT32_C(0x00e9), 127},
		{UINT32_C(0x2502), 126},
	};
	static const struct st_console_font font = {
		.bitmap = bitmap,
		.bitmap_size = sizeof(bitmap),
		.width = 8,
		.height = 1,
		.glyph_count = 128,
		.row_stride = 1,
		.glyph_stride = 1,
		.wide_bitmap = wide_bitmap,
		.wide_bitmap_size = sizeof(wide_bitmap),
		.wide_glyph_count = 1,
		.wide_row_stride = 2,
		.wide_glyph_stride = 2,
		.mapping = mapping,
		.mapping_count = sizeof(mapping) / sizeof(mapping[0]),
		.cluster_mapping = clusters,
		.cluster_mapping_count = 1,
		.fallback_glyph = '?',
	};
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
		.text_presentation = cp437 ? ST_CONSOLE_TEXT_CP437
		    : ST_CONSOLE_TEXT_UNICODE,
	};
	struct st_console_event event;
	const struct st_console_rect cells_rect = {0, 0, 7, 1};
	struct st_console_cell cells[7] = {
		{.glyph = 'A', .foreground = 9, .background = 0},
		{.glyph = 'B',
		    .foreground = ST_CONSOLE_COLOR_RGB_VALUE(0, 215, 175),
		    .background = 0},
		{.glyph = 'C', .foreground = 7, .background = 0,
		    .flags = ST_CONSOLE_CELL_BOLD},
		{.glyph = 127, .foreground = 7, .background = 0},
		{.glyph = 126, .foreground = 7, .background = 0},
		{.glyph = 128, .foreground = 7, .background = 0},
		{.glyph = 128, .foreground = 7, .background = 0,
		    .flags = ST_CONSOLE_CELL_CONTINUATION},
	};
	struct timespec deadline;
	uint32_t columns;
	uint32_t rows;
	uint32_t capabilities;
	const char *colorterm = getenv("COLORTERM");
	bool expect_direct = colorterm != NULL
	    && (strcmp(colorterm, "truecolor") == 0
	    || strcmp(colorterm, "24bit") == 0);
	int status;
	uint16_t font_identifier;

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
	if ((size_t)MB_CUR_MAX != caller_mb_max) {
		child_result(report, 'L');
		_exit(1);
	}
	if (st_console_register_static_font(console, &font, &font_identifier) != 0
	    || font_identifier != 0) {
		child_result(report, 'G');
		st_console_destroy(console);
		_exit(1);
	}
	for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); ++i)
		cells[i].font = font_identifier;
	if (st_console_size(console, &columns, &rows) != 0
	    || columns != 91 || rows != 37) {
		child_result(report, 'D');
		st_console_destroy(console);
		_exit(1);
	}
	if (st_console_capabilities(console, &capabilities) != 0
	    || (capabilities & ST_CONSOLE_CAP_TEXT_TERMINAL) == 0
	    || (capabilities & ST_CONSOLE_CAP_LOCAL_CONTROL_KEYS) == 0
	    || (expect_direct
	    && ((capabilities & ST_CONSOLE_CAP_PALETTE_CHANGE) == 0
	    || (capabilities & ST_CONSOLE_CAP_EXTENDED_PALETTE) == 0))) {
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
	if (cp437) {
		deadline_after(&deadline, 5);
		status = st_console_event_timedwait(console, &event, &deadline);
		if (status != 0 || event.type != ST_CONSOLE_EVENT_TEXT
		    || event.data.text.codepoint != UINT32_C(0x00e9)) {
			child_result(report, 'B');
			_exit(1);
		}
	}
	child_result(report, 'E');
	static const uint16_t shortcuts[] = {6, 19, 4, 7, 17};
	for (size_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); ++i) {
		deadline_after(&deadline, 5);
		status = st_console_event_timedwait(console, &event, &deadline);
		if (status != 0 || event.type != ST_CONSOLE_EVENT_KEY_CODE
		    || event.data.key.code != shortcuts[i]
		    || event.data.key.modifiers != ST_CONSOLE_MODIFIER_CONTROL) {
			child_result(report, 'H');
			st_console_destroy(console);
			_exit(1);
		}
	}
	child_result(report, 'h');
	/* Escape must complete without another byte arriving to disambiguate it.
	 * Nonblocking keypad reads do not guarantee that on every curses. */
	deadline_after(&deadline, 5);
	status = st_console_event_timedwait(console, &event, &deadline);
	if (status != 0 || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 27 || event.data.key.modifiers != 0) {
		child_result(report, 'X');
		_exit(1);
	}
	child_result(report, 'e');
	deadline_after(&deadline, 5);
	status = st_console_event_timedwait(console, &event, &deadline);
	if (status != 0 || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != ST_CONSOLE_KEY_UP) {
		child_result(report, 'U');
		_exit(1);
	}
	child_result(report, 'u');
	{
		const char text[] = "REPAINT";
		const struct st_console_rect rectangle = {0, 1, 7, 1};
		struct st_console_cell marker[7] = {0};

		for (size_t i = 0; i < 7; ++i) {
			marker[i].glyph = (uint32_t)text[i];
			marker[i].foreground = 7;
		}
		if (st_console_write_cells(console, &rectangle, marker, 7) != 0)
			_exit(1);
	}
	if (st_console_write_cells(console, &cells_rect, cells, 7) != 0) {
		child_result(report, 'P');
		st_console_destroy(console);
		_exit(1);
	}
	if (st_console_present(console) != 0) {
		child_result(report, 'Q');
		st_console_destroy(console);
		_exit(1);
	}
	if ((size_t)MB_CUR_MAX != caller_mb_max) {
		child_result(report, 'L');
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
	if (status != 0 || event.data.key.code != 27) {
		child_result(report, 'X');
		_exit(1);
	}
	child_result(report, 'e');
	deadline_after(&deadline, 5);
	status = st_console_event_timedwait(console, &event, &deadline);
	if (status != 0 || event.type != ST_CONSOLE_EVENT_KEY_CODE
	    || event.data.key.code != 13) {
		child_result(report, 'I');
		_exit(1);
	}
	st_console_destroy(console);
	st_console_display_destroy(display);
	if ((size_t)MB_CUR_MAX != caller_mb_max) {
		child_result(report, 'L');
		_exit(1);
	}
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

		if (status < 0 && errno == EINTR)
			continue;
		if (status <= 0) {
			fprintf(stderr, "ncurses waiting for stage %c: poll=%d errno=%d\n",
			    expected, status, status < 0 ? errno : 0);
			return -1;
		}
		if ((descriptors[1].revents & POLLIN) != 0) {
			ssize_t count = read(master, buffer, sizeof(buffer));

			if (count > 0)
				save_output(buffer, (size_t)count);
		}
		if ((descriptors[0].revents & POLLIN) != 0) {
			unsigned char result;

			if (read(report, &result, 1) != 1) {
				fprintf(stderr, "ncurses report ended before stage %c\n", expected);
				return -1;
			}
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
		if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			fprintf(stderr, "ncurses report closed before stage %c: revents=%x\n",
			    expected, (unsigned)descriptors[0].revents);
			return -1;
		}
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
main(int argc, char **argv)
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
	bool cp437;

	(void)setlocale(LC_CTYPE, "");

	if (argc == 2 && strcmp(argv[1], "--fragmented-output") == 0)
		return test_fragmented_output();
	if (argc == 1)
		cp437 = false;
	else if (argc == 2 && strcmp(argv[1], "--cp437") == 0)
		cp437 = true;
	else if (argc == 2 && strcmp(argv[1], "--cp437-c-locale") == 0)
		cp437 = true;
	else
		return EXIT_FAILURE;
	if ((argc == 2 && strcmp(argv[1], "--cp437-c-locale") == 0)
	    || (cp437 && !ST_CONSOLE_TEST_HAVE_THREAD_LOCALE)) {
		(void)setlocale(LC_CTYPE, "C");
	}
	else if (!select_utf8_locale()) {
		fprintf(stderr, "ncurses PTY test requires an installed UTF-8 locale\n");
		return 77;
	}

	if (pipe(report) != 0)
		return EXIT_FAILURE;
	child = forkpty(&master, NULL, NULL, &size);
	if (child < 0)
		return EXIT_FAILURE;
	if (child == 0) {
		(void)close(report[0]);
		child_test(report[1], cp437);
	}
	(void)close(report[1]);
	status = wait_report(report[0], master, 'R');
	if (status == 77) {
		(void)waitpid(child, &child_status, 0);
		return 77;
	}
	if (status != 0 || write(master, cp437 ? "\r\x82" : "\r", cp437 ? 2 : 1)
	    != (cp437 ? 2 : 1)
	    || wait_report(report[0], master, 'E') != 0
	    || write(master, "\006\023\004\007\021", 5) != 5
	    || wait_report(report[0], master, 'h') != 0)
		goto failed;
	if (write(master, "\033", 1) != 1
	    || wait_report(report[0], master, 'e') != 0
	    || write(master, "\033OA", 3) != 3
	    || wait_report(report[0], master, 'u') != 0)
		goto failed;
	if (wait_report(report[0], master, 'O') != 0)
		goto failed;
	if ((!direct_colour
	    && ((!output_selects_colour(203) && !output_programs_colour(255, 84, 84))
	    || (!output_selects_colour(43) && !output_programs_colour(0, 215, 175))))
	    || (direct_colour
	    && (!output_contains("\033[38;2;255;84;84m")
	    || !output_contains("\033[38;2;0;215;175m")))
	    || (!output_contains("\033[1m") && !output_contains("\033[0;1m"))
	    || (cp437 && (!output_contains_byte(0x82)
	    || !output_contains_byte(0xb3)
	    || output_contains("\xc2\x82") || output_contains("\xc2\xb3")))) {
		fprintf(stderr, "ncurses emitted the wrong cell representation\n");
		goto failed;
	}
	if (!cp437 && (direct_colour || ST_CONSOLE_TEST_HAVE_WIDE)
	    && !output_contains("\xf0\x9f\x95\xb9\xef\xb8\x8f")) {
		fprintf(stderr, "ncurses lost the joystick emoji presentation selector\n");
		goto failed;
	}
	size.ws_row = 43;
	size.ws_col = 101;
	terminal_output_size = 0;
	if (ioctl(master, TIOCSWINSZ, &size) != 0 || kill(child, SIGWINCH) != 0
	    || wait_report(report[0], master, 'Z') != 0)
		goto failed;
	if (!wait_for_output(master, "REPAINT", 4096, 5000)
	    || write(master, "\033", 1) != 1
	    || wait_report(report[0], master, 'e') != 0
	    || write(master, "\r", 1) != 1
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
	dump_terminal_output(terminal_output, terminal_output_size);
	/* Preserve a crash's exit status before killing a still-running child. */
	{
		pid_t result = waitpid(child, &child_status, WNOHANG);

		if (result == child) {
			if (WIFSIGNALED(child_status))
				fprintf(stderr, "ncurses child died from signal %d\n", WTERMSIG(child_status));
			else if (WIFEXITED(child_status))
				fprintf(stderr, "ncurses child exited with status %d\n", WEXITSTATUS(child_status));
		} else if (result == 0) {
			(void)kill(child, SIGKILL);
			(void)waitpid(child, &child_status, 0);
		}
	}
	(void)close(master);
	return EXIT_FAILURE;
}
