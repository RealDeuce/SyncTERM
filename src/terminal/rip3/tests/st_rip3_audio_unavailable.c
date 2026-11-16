/* Exercise the optional decoder's absence in the same native configuration. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_audio_file.h"
#include "io_error.h"

bool
st_audio_file_available(void)
{
	return false;
}

int
st_audio_file_decode_stream(int16_t **frames, size_t *count, FILE *stream)
{
	(void)frames; (void)count; (void)stream;
	return IO_ERROR_NOT_SUPPORTED;
}
