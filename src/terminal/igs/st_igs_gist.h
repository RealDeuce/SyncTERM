/* GIST's three software voices; callers supply local 1/200-second updates. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_IGS_GIST_H
#define ST_IGS_GIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ST_IGS_GIST_PATCHES = 20, ST_IGS_GIST_WORDS = 56, ST_IGS_GIST_VOICES = 3 };
struct st_igs_gist_voice {
	uint16_t parameters[ST_IGS_GIST_WORDS];
	int16_t control, priority;
	/* Amplitude, amplitude modulation, tone, tone modulation, noise, noise modulation. */
	uint32_t accumulator[6];
};
struct st_igs_gist {
	struct st_igs_gist_voice voice[ST_IGS_GIST_VOICES];
	uint8_t registers[16];
	/* Synchronous, ordered unmasked write bytes; the mirror masks separately.
	 * The observer must not reenter or mutate the engine. */
	void (*write)(void *, unsigned reg, uint8_t value);
	void *context;
};
extern const uint16_t st_igs_gist_patches[ST_IGS_GIST_PATCHES][ST_IGS_GIST_WORDS];
void st_igs_gist_init(struct st_igs_gist *);
/* Returns accepted voice 0..2, or -1 on priority rejection. Signed-word
 * arguments retain native folding/automatic-allocation rules. */
int st_igs_gist_on(struct st_igs_gist *, const uint16_t patch[ST_IGS_GIST_WORDS],
    int voice, int volume, int pitch, int priority);
void st_igs_gist_stop(struct st_igs_gist *, int voice);
void st_igs_gist_release(struct st_igs_gist *, int voice);
bool st_igs_gist_active(const struct st_igs_gist *);
/* One software envelope update in C,B,A order. No clock or timer is owned here. */
void st_igs_gist_update(struct st_igs_gist *);
#endif
