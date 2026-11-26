/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_TONES_H_
#define ST_RIP3_TONES_H_

#include <stdbool.h>
#include <stdint.h>

struct st_rip3_builtin_reference;

struct st_rip3_tone {
	uint16_t frequency, milliseconds;
};

/* A finite, copyable sequence, even for the original driver's WORD sweeps. */
struct st_rip3_tones {
	struct st_rip3_tone notes[6];
	unsigned            count, index;
	uint16_t            repeats, frequency, stop;
	int32_t             step;
};

/* NOT_SUPPORTED means the reference belongs to another variable family. */
int  st_rip3_tones_parse(struct st_rip3_tones *, const struct st_rip3_builtin_reference *);
bool st_rip3_tones_next(struct st_rip3_tones *, struct st_rip3_tone *);

#endif
