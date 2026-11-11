/* PSG waveform state in local audio-frame coordinates. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_IGS_PSG_H
#define ST_IGS_PSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ST_IGS_PSG_RATE = 44100, ST_IGS_PSG_CHANNELS = 2 };
struct st_igs_psg {
	uint8_t registers[16];
	bool tone[3], noise;
	uint32_t lfsr;
	unsigned envelope;
	/* Tone A/B/C, noise, envelope: elapsed/duration in 1/250000 of
	 * one local output frame. This represents the period conversions
	 * exactly; no foreign clock, tick loop or absolute timeline exists. */
	uint32_t elapsed[5], duration[5];
	/* Carried local sample-filter histories in Q16 amplitude units. */
	int64_t low, previous_low, high;
};

/* Silent host state. Render advances generators even while gated/muted. */
void st_igs_psg_init(struct st_igs_psg *);
/* Ordinary writes preserve phases; every R13 write retriggers only the
 * shared envelope. Invalid registers are ignored. GPIO is storage only. */
void st_igs_psg_write(struct st_igs_psg *, unsigned reg, uint8_t value);
/* Current gated 5-bit DAC indices in A/B/C order. */
void st_igs_psg_levels(const struct st_igs_psg *, uint8_t levels[3]);
/* Initialized, caller-owned state; output has room for 2*frame_count signed
 * 16-bit samples. No allocation, device access, timer or thread ownership. */
void st_igs_psg_render(struct st_igs_psg *, int16_t *stereo, size_t frame_count);
/* Advance unheard local frames without PCM or filter work. Generator state
 * matches render; discard filter history so re-enable cannot replay a tail. */
void st_igs_psg_skip(struct st_igs_psg *, uint64_t frame_count);

#endif
