/* Native Windows ConPTY Unicode/codepage round-trip fixture.
 * Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_connection.h"
#include "st_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s (Windows error=%lu)\n", __FILE__, __LINE__, #e, (unsigned long)GetLastError()); return 1; } } while (0)

static int
child(void)
{
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	wchar_t text[2];
	DWORD count, mode;
	CHECK(GetConsoleMode(input, &mode));
	CHECK(SetConsoleMode(input, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)));
	CHECK(WriteConsoleW(output, L"READY", 5, &count, NULL) && count == 5);
	for (DWORD used = 0; used < 2;) {
		CHECK(ReadConsoleW(input, text + used, 2 - used, &count, NULL) && count > 0);
		used += count;
	}
	CHECK(text[0] == 0x00e9 && text[1] == 0x2500);
	CHECK(WriteConsoleW(output, L"RESULT:\u00e9\u2500:DONE", 14, &count, NULL) && count == 14);
	return 0;
}

static int
receive_until(st_connection_t *connection, const char *wanted)
{
	char buffer[32768] = "";
	size_t used = 0;
	struct st_deadline deadline;
	int64_t remaining;
	CHECK(st_deadline_start(&deadline, 10000) == 0);
	while (strstr(buffer, wanted) == NULL) {
		CHECK(st_deadline_remaining_ms(&deadline, &remaining) == 0 && remaining > 0);
		CHECK(used < sizeof(buffer) - 1);
		ssize_t count = st_connection_receive(connection, buffer + used,
		    sizeof(buffer) - 1 - used, remaining);
		if (count <= 0)
			fprintf(stderr, "ConPTY receive=%lld buffer=[%s]\n", (long long)count, buffer);
		CHECK(count > 0);
		used += (size_t)count;
		buffer[used] = '\0';
	}
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--child") == 0)
		return child();
	for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
		struct st_profile profile;
		st_connection_t *connection = NULL;
		struct st_connection_error error = {0};
		st_profile_init(&profile);
		profile.connection_type = ST_CONNECTION_SHELL;
		profile.screen_mode = ST_SCREEN_MODE_80X25;
		profile.encoding = utf8 ? ST_PROFILE_ENCODING_UTF8 : ST_CODEPAGE_CP437;
		strcpy(profile.address, ".\\st_conpty_encoding_test.exe --child");
		int status = st_connection_connect_shell(&connection, &profile, NULL, NULL, NULL, &error);
		if (status == IO_ERROR_NOT_SUPPORTED) {
			fprintf(stderr, "ConPTY unavailable: %s\n", error.detail);
			return 77;
		}
		CHECK(status == 0);
		CHECK(receive_until(connection, "READY") == 0);
		const char *input = utf8 ? "\xc3\xa9\xe2\x94\x80" : "\x82\xc4";
		for (size_t i = 0; i < strlen(input); ++i)
			CHECK(st_connection_send(connection, input + i, 1, 5000) == 1);
		CHECK(receive_until(connection, utf8 ? "RESULT:\xc3\xa9\xe2\x94\x80:DONE" : "RESULT:\x82\xc4:DONE") == 0);
		CHECK(st_connection_close(connection) == 0);
		st_profile_dispose(&profile);
	}
	return 0;
}
