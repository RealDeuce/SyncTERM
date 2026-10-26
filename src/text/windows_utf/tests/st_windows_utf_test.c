/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_windows_utf.h"

#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d\n", \
    __FILE__, __LINE__); return EXIT_FAILURE; } } while (0)

int
main(void)
{
	static const char text[] = "A\xe2\x82\xac\xf0\x9f\x98\x80";
	static const char invalid[] = "\xc0\x80";
	static const wchar_t invalid_wide[] = {0xd800, 0};
	wchar_t *wide = NULL;
	char *roundtrip = NULL;
	wchar_t *wide_sentinel = (wchar_t *)(uintptr_t)1;
	char *text_sentinel = (char *)(uintptr_t)1;

	CHECK(st_windows_utf8_to_utf16(&wide, text) == 0);
	CHECK(wide != NULL);
	CHECK(wide[0] == L'A' && wide[1] == 0x20ac
	    && wide[2] == 0xd83d && wide[3] == 0xde00 && wide[4] == 0);
	CHECK(st_windows_utf16_to_utf8(&roundtrip, wide) == 0);
	CHECK(strcmp(roundtrip, text) == 0);
	free(roundtrip);
	free(wide);
	CHECK(st_windows_utf8_to_utf16(&wide_sentinel, invalid)
	    == IO_ERROR_INVALID_ENCODING);
	CHECK(wide_sentinel == (wchar_t *)(uintptr_t)1);
	CHECK(st_windows_utf16_to_utf8(&text_sentinel, invalid_wide)
	    == IO_ERROR_INVALID_ENCODING);
	CHECK(text_sentinel == (char *)(uintptr_t)1);
	const wchar_t lone_low[] = {0xdc00, 0};
	CHECK(st_windows_utf16_to_utf8(&text_sentinel, lone_low)
	    == IO_ERROR_INVALID_ENCODING);
	CHECK(text_sentinel == (char *)(uintptr_t)1);
	CHECK(st_windows_utf8_to_utf16(&wide_sentinel, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(wide_sentinel == (wchar_t *)(uintptr_t)1);
	CHECK(st_windows_utf16_to_utf8(NULL, L"") == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_windows_utf8_to_utf16(&wide, "") == 0 && wide[0] == 0);
	CHECK(st_windows_utf16_to_utf8(&roundtrip, L"") == 0 && roundtrip[0] == 0);
	free(wide);
	free(roundtrip);
	return EXIT_SUCCESS;
}
