/* Verify exact desktop-handler arguments and asynchronous browser lifetime. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */
#include "console_posix_url.h"
#include "io_error.h"
#include "st_time.h"
#include "string_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); return 1; } } while (0)
int
main(int argc, char **argv)
{
	st_console_t *console = NULL;
	CHECK(argc == 1 || (argc == 2 && strcmp(argv[1], "--x11") == 0));
	if (argc == 2) {
		const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_X11};
		const struct st_console_config geometry = {
			.columns = 40, .rows = 10, .cell_width = 8, .cell_height = 16,
			.pixel_width = 320, .pixel_height = 160,
		};
		CHECK(st_console_create(&console, &display, &geometry) == 0);
		uint32_t capabilities;
		CHECK(st_console_capabilities(console, &capabilities) == 0);
		CHECK(capabilities & ST_CONSOLE_CAP_OPEN_URL);
	}
	const char *directory = ST_URL_TEST_DIRECTORY;
	const char *uri = "https://example.invalid/a?q='\"`$()&other=\xc3\xa9%20x";
	const char *old_path = getenv("PATH");
	char *saved_path = old_path == NULL ? NULL : strdup(old_path);
	char launcher[4096], record[4096], path[8192], bytes[1024] = {0};
	FILE *stream;
	uint64_t start, end;
	CHECK(snprintf(launcher, sizeof(launcher), "%s/xdg-open", directory) > 0);
	CHECK(snprintf(record, sizeof(record), "%s/uri.txt", directory) > 0);
	CHECK(snprintf(path, sizeof(path), "%s:%s", directory, old_path == NULL ? "" : old_path) > 0);
	(void)unlink(record);
	stream = fopen(launcher, "wb");
	CHECK(stream != NULL);
	CHECK(fputs("#!/bin/sh\nprintf '%s' \"$1\" > \"$ST_URL_TEST_RECORD\"\nsleep 2\n", stream) >= 0);
	CHECK(fclose(stream) == 0 && chmod(launcher, 0700) == 0);
	CHECK(setenv("PATH", path, 1) == 0);
	CHECK(setenv("ST_URL_TEST_RECORD", record, 1) == 0);
	CHECK(st_time_monotonic_ms(&start) == 0);
	CHECK((console == NULL ? st_console_posix_open_url(NULL, uri)
	    : st_console_open_url(console, uri)) == 0);
	CHECK(st_time_monotonic_ms(&end) == 0 && end - start < 1500);
	stream = NULL;
	for (unsigned i = 0; i < 200; ++i) {
		stream = fopen(record, "rb");
		if (stream != NULL) {
			size_t count = fread(bytes, 1, sizeof(bytes) - 1, stream);
			CHECK(fclose(stream) == 0);
			stream = NULL;
			if (count == strlen(uri)) { bytes[count] = '\0'; break; }
		}
		const struct timespec delay = {0, 10000000};
		(void)nanosleep(&delay, NULL);
	}
	CHECK(strcmp(bytes, uri) == 0);
	CHECK(setenv("PATH", directory, 1) == 0);
	CHECK(unlink(launcher) == 0);
	CHECK((console == NULL ? st_console_posix_open_url(NULL, uri)
	    : st_console_open_url(console, uri)) == IO_ERROR_NOT_FOUND);
	CHECK(st_console_posix_open_url(NULL, "") == IO_ERROR_INVALID_ARGUMENT);
	if (saved_path != NULL) CHECK(setenv("PATH", saved_path, 1) == 0);
	else CHECK(unsetenv("PATH") == 0);
	free(saved_path);
	CHECK(unsetenv("ST_URL_TEST_RECORD") == 0);
	CHECK(unlink(record) == 0);
	st_console_destroy(console);
	return 0;
}
