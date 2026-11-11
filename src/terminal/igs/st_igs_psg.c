/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_igs_psg.h"

enum { FRAME_PARTS = 250000 };
static const uint8_t masks[16] = {
	255,15,255,15,255,15,31,255,31,31,31,255,255,15,255,255
};
/* YM2149 level data, as recorded in chapter 53 and generated/ym2149-mixer.json
 * (Hatari c453b85f811fc11bed10e19e5e48fe95377e5a23, sound.c). These
 * single-channel logarithmic amplitudes are summed here; the reference's
 * separate measured 3-channel nonlinear table is not this mixing policy. */
static const uint8_t fixed_levels[16] = {
	0,1,5,7,9,11,13,15,17,19,21,23,25,27,29,31
};
static const uint16_t amplitudes[32] = {
	0,369,438,521,619,735,874,1039,1234,1467,1744,2072,2463,2927,3479,4135,
	4914,5841,6942,8250,9806,11654,13851,16462,19565,23253,27636,32845,
	39037,46395,55141,65535
};

static void
period(struct st_igs_psg *p, unsigned generator)
{
	unsigned value;
	if (generator < 3)
		value = p->registers[2*generator] + 256u*p->registers[2*generator+1];
	else if (generator == 3)
		value = p->registers[6];
	else
		value = p->registers[11] + 256u*p->registers[12];
	if (value == 0) value = 1;
	/* Half-tone/envelope step: period*44100/250000 output frames;
	 * noise shift: period*44100/125000 frames. All fit uint32_t. */
	p->duration[generator] = (uint32_t)value*ST_IGS_PSG_RATE*(generator == 3 ? 2u : 1u);
}

void
st_igs_psg_init(struct st_igs_psg *p)
{
	*p = (struct st_igs_psg){.registers = {[7] = 0x3f}, .lfsr = 1};
	for (unsigned i = 0; i < 5; ++i) period(p,i);
}

void
st_igs_psg_write(struct st_igs_psg *p, unsigned reg, uint8_t value)
{
	if (reg >= 16) return;
	p->registers[reg] = value & masks[reg];
	if (reg < 6) period(p,reg/2);
	else if (reg == 6) period(p,3);
	else if (reg == 11 || reg == 12) period(p,4);
	else if (reg == 13) { p->elapsed[4] = 0; p->envelope = 0; }
}

static uint8_t
envelope_level(const struct st_igs_psg *p)
{
	unsigned position = p->envelope, shape = p->registers[13];
	unsigned step = position % 32;
	if (position < 32) return (uint8_t)((shape & 4) ? step : 31-step);
	switch (shape) {
	case 8: return (uint8_t)(31-step);
	case 10: return (uint8_t)(position < 64 ? step : 31-step);
	case 11: case 13: return 31;
	case 12: return (uint8_t)step;
	case 14: return (uint8_t)(position < 64 ? 31-step : step);
	default: return 0;
	}
}

void
st_igs_psg_levels(const struct st_igs_psg *p, uint8_t levels[3])
{
	uint8_t envelope = envelope_level(p);
	for (unsigned i = 0; i < 3; ++i) {
		bool gate = (p->tone[i] || (p->registers[7] & (1u << i)))
		    && (p->noise || (p->registers[7] & (8u << i)));
		unsigned gain = p->registers[8+i];
		levels[i] = !gate ? 0 : gain & 16 ? envelope : fixed_levels[gain];
	}
}

static void
edge(struct st_igs_psg *p, unsigned generator)
{
	if (generator < 3) p->tone[generator] = !p->tone[generator];
	else if (generator == 3) {
		p->noise = (p->lfsr & 1) != 0;
		p->lfsr = (p->lfsr >> 1) ^ (p->noise ? 0x12000u : 0);
	}
	else if (++p->envelope >= 96) p->envelope -= 64;
}

static uint32_t
amplitude(const struct st_igs_psg *p)
{
	uint8_t levels[3];
	st_igs_psg_levels(p,levels);
	return (uint32_t)amplitudes[levels[0]] + amplitudes[levels[1]] + amplitudes[levels[2]];
}

static int16_t
sample(struct st_igs_psg *p)
{
	uint64_t area = 0;
	uint32_t remaining = FRAME_PARTS;
	/* A shorter period can lie behind the carried phase. Resolve that
	 * edge at this local sample boundary, without restarting other phases
	 * or inventing sub-sample bus-write timestamps. */
	for (unsigned i = 0; i < 5; ++i)
		if (p->elapsed[i] >= p->duration[i]) { p->elapsed[i] = 0; edge(p,i); }
	while (remaining != 0) {
		uint32_t span = remaining;
		for (unsigned i = 0; i < 5; ++i) {
			uint32_t until = p->duration[i]-p->elapsed[i];
			if (until < span) span = until;
		}
		area += (uint64_t)amplitude(p)*span;
		remaining -= span;
		for (unsigned i = 0; i < 5; ++i) {
			p->elapsed[i] += span;
			if (p->elapsed[i] == p->duration[i]) { p->elapsed[i] = 0; edge(p,i); }
		}
	}
	/* Integrate waveform edges over one output frame before quantizing.
	 * The following mild low-pass/DC block is a local synthesis choice,
	 * not the reference's canonical 48kHz/analog filter profile. */
	int64_t input = (int64_t)(area*32767 / ((uint64_t)FRAME_PARTS*3*65535));
	p->low += (input*65536-p->low)*3/4;
	p->high = p->low-p->previous_low + p->high*32700/32768;
	p->previous_low = p->low;
	int64_t output = p->high/65536;
	if (output > INT16_MAX) output = INT16_MAX;
	else if (output < INT16_MIN) output = INT16_MIN;
	return (int16_t)output;
}

void
st_igs_psg_render(struct st_igs_psg *p, int16_t *stereo, size_t frame_count)
{
	for (size_t i = 0; i < frame_count; ++i) {
		int16_t value = sample(p);
		stereo[2*i] = value; stereo[2*i+1] = value;
	}
}
