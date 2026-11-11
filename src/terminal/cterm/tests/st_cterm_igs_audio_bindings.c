/* Test-only observation/failure injection at existing component boundaries. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs_audio.h"
#include "st_audio_runtime.h"
#include "io_error.h"
#include <stdlib.h>

size_t st_igs_test_calls;
int st_igs_test_arguments[4];
const uint16_t *st_igs_test_patch;
unsigned st_igs_test_stops[3], st_igs_test_stop_count;
bool st_igs_test_allocation_failure, st_igs_test_open_failure;

static int
observe_on(struct st_igs_gist *g, const uint16_t *patch, int voice, int volume, int pitch, int priority)
{
	++st_igs_test_calls; st_igs_test_patch = patch;
	st_igs_test_arguments[0] = voice; st_igs_test_arguments[1] = volume;
	st_igs_test_arguments[2] = pitch; st_igs_test_arguments[3] = priority;
	return st_igs_gist_on(g,patch,voice,volume,pitch,priority);
}
static void
observe_stop(struct st_igs_gist *g, int voice)
{
	st_igs_test_stops[st_igs_test_stop_count%3] = (unsigned)voice; ++st_igs_test_stop_count;
	st_igs_gist_stop(g,voice);
}
static void *allocate(size_t n, size_t size)
{
	return st_igs_test_allocation_failure ? NULL : calloc(n,size);
}
static int acquire(st_audio_runtime_t *runtime, audio_engine_t **engine)
{
	return st_igs_test_open_failure ? IO_ERROR_IO : st_audio_runtime_acquire(runtime,engine);
}
#define st_igs_gist_on observe_on
#define st_igs_gist_stop observe_stop
#define calloc allocate
#define st_audio_runtime_acquire acquire
#include "../cterm_igs_audio.c"
