/* Authored N16/67 reset records and stateful YM3812 write-acceptance cases. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>

#include "io_error.h"
#include "st_big_music.h"
#include "st_opl2.h"
#define CHECK(x)                                                                \
	do {                                                                    \
		if (!(x)) {                                                     \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                \
		}                                                               \
	} while (0)
static const uint8_t offsets[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21};
/* All175 register/value pairs, transcribed from chapter16's slot/default
 * map and ordered tail, not generated from the implementation under test. */
static const uint16_t expected[] = {0xbd00, 0x0800, 0x404f, 0xc006, 0x60f1, 0x8053, 0x2001, 0xe000, 0xbd00, 0x0800,
    0x414f, 0xc106, 0x61f1, 0x8153, 0x2101, 0xe100, 0xbd00, 0x0800, 0x424f, 0xc206, 0x62f1, 0x8253, 0x2201, 0xe200,
    0xbd00, 0x0800, 0x4300, 0x63f2, 0x8374, 0x2311, 0xe300, 0xbd00, 0x0800, 0x4400, 0x64f2, 0x8474, 0x2411, 0xe400,
    0xbd00, 0x0800, 0x4500, 0x65f2, 0x8574, 0x2511, 0xe500, 0xbd00, 0x0800, 0x484f, 0xc306, 0x68f1, 0x8853, 0x2801,
    0xe800, 0xbd00, 0x0800, 0x494f, 0xc406, 0x69f1, 0x8953, 0x2901, 0xe900, 0xbd00, 0x0800, 0x4a4f, 0xc506, 0x6af1,
    0x8a53, 0x2a01, 0xea00, 0xbd00, 0x0800, 0x4b00, 0x6bf2, 0x8b74, 0x2b11, 0xeb00, 0xbd00, 0x0800, 0x4c00, 0x6cf2,
    0x8c74, 0x2c11, 0xec00, 0xbd00, 0x0800, 0x4d00, 0x6df2, 0x8d74, 0x2d11, 0xed00, 0xbd00, 0x0800, 0x504f, 0xc606,
    0x70f1, 0x9053, 0x3001, 0xf000, 0xbd00, 0x0800, 0x514f, 0xc706, 0x71f1, 0x9153, 0x3101, 0xf100, 0xbd00, 0x0800,
    0x524f, 0xc806, 0x72f1, 0x9253, 0x3201, 0xf200, 0xbd00, 0x0800, 0x5300, 0x73f2, 0x9374, 0x3311, 0xf300, 0xbd00,
    0x0800, 0x5400, 0x74f2, 0x9474, 0x3411, 0xf400, 0xbd00, 0x0800, 0x5500, 0x75f2, 0x9574, 0x3511, 0xf500, 0xbd00,
    0xbd00, 0x0800, 0xa000, 0xb000, 0xa100, 0xb100, 0xa200, 0xb200, 0xa300, 0xb300, 0xa400, 0xb400, 0xa500, 0xb500,
    0xa600, 0xb600, 0xa700, 0xb700, 0xa800, 0xb800, 0xe000, 0xe100, 0xe200, 0xe300, 0xe400, 0xe500, 0xe800, 0xe900,
    0xea00, 0xeb00, 0xec00, 0xed00, 0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0x0120};
_Static_assert(sizeof(expected) / sizeof(*expected) == ST_BIG_MUSIC_WARM_WRITES,
    "Complete authored warm sequence");

static void
full_sequence(void)
{
	unsigned count[256] = {0};
	for (unsigned i = 0; i < 175; ++i) {
		uint8_t reg, data;
		CHECK(st_big_music_warm_write(i, &reg, &data) == 0);
		++count[reg];
	}
	CHECK(count[0xbd] == 20 && count[8] == 19 && count[1] == 1);
	for (unsigned i = 0; i < 18; ++i) {
		CHECK(count[0x20 + offsets[i]] == 1 && count[0x40 + offsets[i]] == 1
		      && count[0x60 + offsets[i]] == 1 && count[0x80 + offsets[i]] == 1
		      && count[0xe0 + offsets[i]] == 2);
	}
	for (unsigned i = 0; i < 9; ++i)
		CHECK(count[0xa0 + i] == 1 && count[0xb0 + i] == 1 && count[0xc0 + i] == 1);
	for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); ++i) {
		uint8_t reg, data;
		CHECK(st_big_music_warm_write((unsigned)i, &reg, &data) == 0
		      && ((unsigned)reg << 8 | data) == expected[i]);
	}
	for (unsigned i = 0; i < 18; ++i) {
		uint8_t reg, data;
		CHECK(st_big_music_warm_write(156 + i, &reg, &data) == 0 && reg == 0xe0 + offsets[i] && data == 0);
	}
	uint8_t reg = 99, data = 88;
	CHECK(st_big_music_warm_write(175, &reg, &data) == IO_ERROR_OUT_OF_RANGE && reg == 99 && data == 88);
	CHECK(st_big_music_warm_write(0, NULL, &data) == IO_ERROR_INVALID_ARGUMENT && data == 88);
}

static void
retained_chip(void)
{
	for (unsigned enabled = 0; enabled < 2; ++enabled) {
		struct st_opl2 chip;
		CHECK(st_opl2_init(&chip, 44100) == 0);
		CHECK(st_opl2_write(&chip, 1, 0x20) == 0 && st_opl2_write(&chip, 0xe0, 3) == 0);
		CHECK(st_opl2_write(&chip, 0x20, 1) == 0 && st_opl2_write(&chip, 0x60, 0xf0) == 0
		      && st_opl2_write(&chip, 0xa0, 0x44) == 0 && st_opl2_write(&chip, 0xb0, 0x31) == 0);
		CHECK(st_opl2_write(&chip, 2, 0xfe) == 0 && st_opl2_write(&chip, 4, 0x21) == 0);
		int32_t pcm[32];
		CHECK(st_opl2_render(&chip, pcm, 32) == 0);
		uint64_t samples       = chip.chipSamples;
		uint32_t phase         = chip.op[0].phase;
		uint8_t  timer_control = chip.timerControl, status = chip.status;
		uint16_t timer_count = chip.timerCount[0];
		CHECK(st_opl2_write(&chip, 1, enabled ? 0x20 : 0) == 0 && st_opl2_write(&chip, 0xbd, 0xff) == 0
		      && st_opl2_write(&chip, 8, 0xc0) == 0);
		for (unsigned i = 0; i < 175; ++i) {
			uint8_t reg, data;
			CHECK(
			    st_big_music_warm_write(i, &reg, &data) == 0 && st_opl2_write(&chip, reg, data) == 0);
		}
		CHECK(chip.waveSelectEnable
		      && chip.op[0].waveform == (enabled ? 0 : 3)); /* WSE gates E0 writes, not rendering. */
		CHECK(chip.rhythmMode == 0 && chip.noteSelect == 0 && !chip.compositeSine);
		CHECK(chip.op[0].totalLevel == 15 && chip.op[3].totalLevel == 0 && chip.chan[0].fNumber == 0
		      && chip.chan[0].block == 0);
		CHECK(chip.chipSamples == samples
		      && chip.op[0].phase == phase); /* Warm writes are not a chip reinitialization. */
		CHECK(chip.timerControl == timer_control && chip.timerCount[0] == timer_count
		      && chip.status == status && chip.timer[0] == 0xfe);
	}
}

int
main(void)
{
	full_sequence();
	retained_chip();
	return 0;
}
