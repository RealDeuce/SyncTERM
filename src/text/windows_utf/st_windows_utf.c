/* Strict conversion between UTF-8 and Windows UTF-16 strings. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_windows_utf.h"

#include "io_error.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>

_Static_assert(sizeof(wchar_t) == 2, "Windows wchar_t must be UTF-16");

int
st_windows_utf8_to_utf16(wchar_t **result, const char *source)
{
	wchar_t *converted;
	int required;

	if (result == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source,
	    -1, NULL, 0);
	if (required == 0)
		return io_error_from_windows(GetLastError());
	if ((size_t)required > SIZE_MAX / sizeof(*converted))
		return IO_ERROR_OUT_OF_RANGE;
	converted = malloc((size_t)required * sizeof(*converted));
	if (converted == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1,
	    converted, required) == 0) {
		int status = io_error_from_windows(GetLastError());

		free(converted);
		return status;
	}
	*result = converted;
	return 0;
}

int
st_windows_utf16_to_utf8(char **result, const wchar_t *source)
{
	char *converted;
	int required;

	if (result == NULL || source == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source,
	    -1, NULL, 0, NULL, NULL);
	if (required == 0)
		return io_error_from_windows(GetLastError());
	converted = malloc((size_t)required);
	if (converted == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1,
	    converted, required, NULL, NULL) == 0) {
		int status = io_error_from_windows(GetLastError());

		free(converted);
		return status;
	}
	*result = converted;
	return 0;
}
