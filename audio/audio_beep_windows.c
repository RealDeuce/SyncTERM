/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "io_error.h"

#include <windows.h>

int
audio_platform_beep(double frequency, uint32_t duration_ms)
{
	if (frequency < 37.0 || frequency > 32767.0)
		return IO_ERROR_NOT_SUPPORTED;
	return Beep((DWORD)(frequency + 0.5), (DWORD)duration_ms)
	    ? 0 : IO_ERROR_IO;
}
