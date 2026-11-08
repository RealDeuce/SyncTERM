/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdlib.h>
#include "cterm.h"

bool st_skypix_test_allocation_failure;
bool st_skypix_test_open_failure;
static audio_stream_t *sample_stream(struct cterminal *t)
{
	return st_skypix_test_open_failure ? NULL : cterm_fx_stream(t);
}
static void *sample_malloc(size_t size)
{
	return st_skypix_test_allocation_failure ? NULL : malloc(size);
}
#define malloc sample_malloc
#define cterm_fx_stream sample_stream
#include "../cterm_skypix_audio.c"
