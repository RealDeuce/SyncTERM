/* SkyPix raw sample ownership and owner-thread rate conversion. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_SKYPIX_AUDIO_H_
#define CTERM_SKYPIX_AUDIO_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { CTERM_SKYPIX_SAMPLE_MAX = 4 * 1024 * 1024 };
struct cterm_skypix_audio {
	uint8_t *samples;
	size_t count;
	uint16_t period;
	uint64_t phase, drain_until;
	unsigned repeats;
	bool playing, draining;
};
struct cterm_skypix;
int cterm_skypix_audio_load(struct cterm_skypix *, const void *, size_t);
/* Loaded sample, nonzero repeats, owner-thread conversion. */
size_t cterm_skypix_audio_convert(struct cterm_skypix_audio *, int16_t *, size_t);
void cterm_skypix_audio_play(struct cterm_skypix *, unsigned);
void cterm_skypix_audio_poll(struct cterm_skypix *, uint64_t);
void cterm_skypix_audio_cancel(struct cterm_skypix *);
void cterm_skypix_audio_reset(struct cterm_skypix *);
#endif
