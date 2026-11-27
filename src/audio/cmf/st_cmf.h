/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CMF_H_
#define ST_CMF_H_
#include <stdio.h>

#include "st_audio_runtime.h"

struct st_cmf;
/* Borrows the file only during load. Plays a looping CMF on the session mixer. */
struct st_cmf *st_cmf_play(st_audio_runtime_t *, FILE *);
void           st_cmf_destroy(struct st_cmf *);
#endif
