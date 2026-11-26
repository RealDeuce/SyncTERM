/* Authored YM3812 control and PCM expectations; no other emulator test code. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_opl2.h"
#define CHECK(x)                                                                \
	do {                                                                    \
		if (!(x)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                \
		}                                                               \
	} while (0)

static void
write_reg(struct st_opl2 *p, unsigned reg, unsigned value)
{
	CHECK(st_opl2_write(p, reg, (uint8_t)value) == 0);
}

static void
tick(struct st_opl2 *p, size_t count)
{
	int32_t samples[64];
	while (count) {
		size_t n = count > 64 ? 64 : count;
		CHECK(st_opl2_render(p, samples, n) == 0);
		count -= n;
	}
}

static uint8_t
status(const struct st_opl2 *p)
{
	uint8_t value = 0;
	CHECK(st_opl2_status(p, &value) == 0);
	return value;
}

static void
controls(void)
{
	struct st_opl2 p;
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	CHECK(ST_OPL2_CHANNELS == 9 && ST_OPL2_OPERATORS == 18 && status(&p) == 0x06);
	/* Literal retained-wave sequence from BIG reference17, based on
	 * write acceptance in the YM3812 low-level observations. */
	write_reg(&p, 0xe0, 3);
	CHECK(p.op[0].waveform == 0);
	write_reg(&p, 1, 0x20);
	write_reg(&p, 0xe0, 3);
	write_reg(&p, 1, 0);
	write_reg(&p, 0xe0, 0);
	CHECK(p.op[0].waveform == 3);
	write_reg(&p, 1, 0x20);
	CHECK(p.op[0].waveform == 3);
	for (unsigned slot = 0; slot < 18; ++slot) {
		unsigned reg = 0xe0 + (slot / 6) * 8 + slot % 6;
		write_reg(&p, reg, 255);
		CHECK(p.op[slot].waveform == 3);
	}
	struct st_opl2 before;
	memcpy(&before, &p, sizeof(p));
	CHECK(st_opl2_write(&p, 0x104, 255) == IO_ERROR_OUT_OF_RANGE && memcmp(&p, &before, sizeof(p)) == 0);
	CHECK(st_opl2_write(&p, 0x105, 1) == IO_ERROR_OUT_OF_RANGE && memcmp(&p, &before, sizeof(p)) == 0);
	/* OPL3 speaker-enable bits do not mute an OPL2 channel. */
	write_reg(&p, 0xc0, 0);
	memcpy(&before, &p, sizeof(p));
	write_reg(&p, 0xc0, 0xf0);
	CHECK(memcmp(&p, &before, sizeof(p)) == 0);
	/* Invalid operator holes have no target. */
	write_reg(&p, 0xe6, 0);
	CHECK(memcmp(&p, &before, sizeof(p)) == 0);
	for (unsigned ch = 0; ch < 9; ++ch) {
		write_reg(&p, 0xa0 + ch, 0xff);
		write_reg(&p, 0xb0 + ch, 0x3f);
		CHECK(p.chan[ch].fNumber == 1023 && p.chan[ch].block == 7);
		CHECK(p.op[p.chan[ch].opSlot[0]].key == 1 && p.op[p.chan[ch].opSlot[1]].key == 1);
	}
	write_reg(&p, 0xbd, 0x3f);
	static const unsigned masks[6] = {16, 1, 4, 16, 8, 2};
	for (unsigned bit = 0; bit < 5; ++bit) {
		write_reg(&p, 0xbd, 0x20 + (1u << bit));
		for (unsigned i = 0; i < 6; ++i)
			CHECK(p.op[12 + i].key == (1u + ((masks[i] & (1u << bit)) ? 2u : 0u)));
	}
	write_reg(&p, 0xbd, 0);
	for (unsigned i = 0; i < 18; ++i)
		CHECK(p.op[i].key == 1);
}

static void
timers_and_csm(void)
{
	struct st_opl2 p;
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	write_reg(&p, 2, 255);
	write_reg(&p, 3, 255);
	write_reg(&p, 4, 0x81);
	tick(&p, 32);
	CHECK(status(&p) == 6); /* Reset does not start a timer. */
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	write_reg(&p, 2, 255);
	write_reg(&p, 3, 255);
	write_reg(&p, 4, 3);
	tick(&p, 4);
	CHECK(status(&p) == 6);
	tick(&p, 1);
	CHECK(status(&p) == 0xc6);
	tick(&p, 12);
	CHECK(status(&p) == 0xe6);
	write_reg(&p, 4, 0x80);
	CHECK(status(&p) == 6 && p.timerControl == 3);
	tick(&p, 4);
	CHECK(status(&p) == 0xc6);
	write_reg(&p, 4, 0x43);
	CHECK(status(&p) == 6);
	tick(&p, 16);
	CHECK(status(&p) == 0xa6);
	write_reg(&p, 4, 0x63);
	CHECK(status(&p) == 6);
	tick(&p, 16);
	CHECK(status(&p) == 6);
	/* Timer1 CSM is independent of status masking. Native-sample timing:
	 * overflow schedules one key pulse; the next sample applies it, then
	 * drops only that key owner, preserving melodic and rhythm keys. */
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	write_reg(&p, 8, 0x80);
	write_reg(&p, 2, 255);
	write_reg(&p, 4, 0x41);
	tick(&p, 5);
	CHECK(p.csmPending && status(&p) == 6);
	tick(&p, 1);
	CHECK(!p.csmPending);
	for (unsigned i = 0; i < 18; ++i)
		CHECK(p.op[i].key == 0 && p.op[i].envelopeStage == ST_OPL2_ENV_ATTACK);
	tick(&p, 1);
	for (unsigned i = 0; i < 18; ++i)
		CHECK(p.op[i].envelopeStage == ST_OPL2_ENV_RELEASE);
	write_reg(&p, 0xb0, 0x20);
	write_reg(&p, 0xbd, 0x21);
	tick(&p, 32);
	CHECK(p.op[0].key == 1 && p.op[3].key == 1 && p.op[13].key == 2);
}

static void
csm_pcm(void)
{
	struct st_opl2 p;
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	write_reg(&p, 0x20, 1);
	write_reg(&p, 0x23, 1);
	write_reg(&p, 0x40, 63);
	write_reg(&p, 0x60, 0xf0);
	write_reg(&p, 0x63, 0xf0);
	write_reg(&p, 0x83, 0x0f);
	write_reg(&p, 0xa0, 0x44);
	write_reg(&p, 0xb0, 0x12); /* Frequency set, melodic key OFF. */
	write_reg(&p, 2, 255);
	write_reg(&p, 4, 0x41);
	struct st_opl2 q;
	memcpy(&q, &p, sizeof(p));
	write_reg(&p, 8, 0x80);
	int32_t on[64], off[64];
	CHECK(st_opl2_render(&p, on, 64) == 0 && st_opl2_render(&q, off, 64) == 0);
	bool sounded = false;
	/* With full attenuation, inherited one's-complement arithmetic gives
	 * either0 or-1 as phase changes; that quantization floor is not a note. */
	for (unsigned i = 0; i < 64; ++i) {
		CHECK(off[i] == 0 || off[i] == -1);
		if (on[i] > 1 || on[i] < -1)
			sounded = true;
	}
	CHECK(sounded && status(&p) == 6 && status(&q) == 6);
}

static void
polyphonic_headroom(void)
{
	struct st_opl2 p;
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	for (unsigned ch = 0; ch < 9; ++ch) {
		write_reg(&p, 0xc0 + ch, 1);
		write_reg(&p, 0xb0 + ch, 0x20);
	}
	for (unsigned slot = 0; slot < 18; ++slot) {
		p.op[slot].phase         = 256u << 9;
		p.op[slot].envelope      = 0;
		p.op[slot].envelopeStage = ST_OPL2_ENV_SUSTAIN;
		p.op[slot].sustainHold   = 1;
	}
	int32_t samples[3];
	CHECK(st_opl2_render(&p, samples, 3) == 0);
	CHECK(samples[2] == 18 * 4084); /* Unclipped sum before source/mixer gain. */
	for (unsigned slot = 0; slot < 18; ++slot)
		p.op[slot].phase = 768u << 9;
	CHECK(st_opl2_render(&p, samples, 3) == 0 && samples[2] == -18 * 4085);
}

static void
waves_and_partitions(void)
{
	struct st_opl2 p;
	CHECK(st_opl2_init(&p, ST_OPL2_NATIVE_RATE) == 0);
	write_reg(&p, 1, 0x20);
	write_reg(&p, 0xb0, 0x20);
	/* Constant carrier phase, no modulation; known ROM extrema give
	 * +(1024+1018)*2=4084, negative one's complement=-4085.
	 * The disabled half/quarter waves produce exact silence. */
	p.op[3].envelope                 = 0;
	p.op[3].envelopeStage            = ST_OPL2_ENV_SUSTAIN;
	p.op[3].sustainHold              = 1;
	static const unsigned phases[]   = {256, 768, 768, 768, 256};
	static const unsigned waves[]    = {0, 0, 1, 2, 3};
	static const int      expected[] = {4084, -4085, 0, 4084, 0};
	for (unsigned i = 0; i < 5; ++i) {
		p.op[3].phase = phases[i] << 9;
		write_reg(&p, 0xe3, waves[i]);
		int32_t samples[3];
		CHECK(st_opl2_render(&p, samples, 3) == 0);
		CHECK(samples[2] == expected[i]);
	}
	p.op[3].phase = 768u << 9;
	write_reg(&p, 0xe3, 2);
	write_reg(&p, 1, 0);
	write_reg(&p, 0xe3, 0);
	int32_t retained[3];
	CHECK(st_opl2_render(&p, retained, 3) == 0 && retained[2] == 4084);
	write_reg(&p, 1, 0x20);
	CHECK(st_opl2_render(&p, retained, 3) == 0 && retained[2] == 4084);
	CHECK(st_opl2_init(&p, 44100) == 0);
	write_reg(&p, 0xa0, 0x44);
	write_reg(&p, 0xb0, 0x32);
	write_reg(&p, 0x20, 0x21);
	write_reg(&p, 0x23, 0x21);
	write_reg(&p, 0x60, 0xf2);
	write_reg(&p, 0x63, 0xf2);
	write_reg(&p, 0x80, 0x43);
	write_reg(&p, 0x83, 0x43);
	struct st_opl2 q;
	memcpy(&q, &p, sizeof(p));
	int32_t whole[1024], split[1024];
	CHECK(st_opl2_render(&p, whole, 1024) == 0);
	for (unsigned i = 0; i < 1024; ++i)
		CHECK(st_opl2_render(&q, split + i, 1) == 0);
	CHECK(memcmp(whole, split, sizeof(whole)) == 0 && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_opl2_init(&p, 44100) == 0);
	CHECK(st_opl2_render(&p, whole, 1024) == 0);
	for (unsigned i = 0; i < 1024; ++i)
		CHECK(whole[i] == 0);
	memcpy(&q, &p, sizeof(p));
	CHECK(st_opl2_write(&p, 256, 0) == IO_ERROR_OUT_OF_RANGE && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_opl2_init(&p, 0) == IO_ERROR_INVALID_ARGUMENT && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_opl2_render(&p, NULL, 1) == IO_ERROR_INVALID_ARGUMENT && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_opl2_render(&p, NULL, 0) == 0 && memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(st_opl2_init(NULL, 44100) == IO_ERROR_INVALID_ARGUMENT);
}

int
main(void)
{
	controls();
	timers_and_csm();
	csm_pcm();
	polyphonic_headroom();
	waves_and_partitions();
	return 0;
}
