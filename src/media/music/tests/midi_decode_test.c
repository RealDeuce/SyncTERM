/* Literal wire messages, independent of any native library implementation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct log { uint8_t bytes[4096]; size_t size, messages, modes; bool ansi, fail_mode; };
static int record(void *context, const uint8_t *b, size_t n)
{
	struct log *log = context;
	CHECK(n <= sizeof(log->bytes) - log->size);
	memcpy(log->bytes + log->size, b, n); log->size += n; ++log->messages; return 0;
}
static int gain(void *context, float left, float right) { (void)context; (void)left; (void)right; return 0; }
static int mode(void *context, bool ansi)
{
	struct log *log = context;
	++log->modes;
	if (log->fail_mode) return IO_ERROR_IO;
	log->ansi = ansi; return 0;
}
static void stream(void)
{
	/* Replacement statuses cancel partial channel/common/SysEx messages.
	 * Realtime is emitted immediately, even inside those messages. */
	const uint8_t input[] = {12, 0x90,60,0xf8,100,61,101, 0x90,62,
	    0x80,60,0, 0xc2,7,8, 0xf1,1,9, 0xf2,1,0xfb,2,
	    0xf3,4, 0xf6,5, 0xf0,0x7d,6,0xfe,7,0xf7,
	    0xf0,0x7d,1,0x90,63,90, 0x90,1,0xf0,0x7d,2,0xf7,
	    0xf4,1,0xf5,2,0xf7,3, 0xd0,4,5,0xff,6};
	const uint8_t expected[] = {0xf8,0x90,60,100,0x90,61,101,
	    0x80,60,0,0xc2,7,0xc2,8,0xf1,1,0xfb,0xf2,1,2,
	    0xf3,4,0xf6,0xfe,0xf0,0x7d,6,7,0xf7,
	    0x90,63,90,0xf0,0x7d,2,0xf7,0xd0,4,0xd0,5,0xff,0xd0,6};
	for (size_t first = 0; first <= sizeof(input); ++first) {
		for (size_t second = first; second <= sizeof(input); ++second) {
			struct st_midi_sink s = {.gain = gain}; struct log log = {0};
			CHECK(st_midi_decode_stream(&s, input, first, record, &log) == 0);
			CHECK(st_midi_decode_stream(&s, input + first, second - first, record, &log) == 0);
			CHECK(st_midi_decode_stream(&s, input + second, sizeof(input) - second, record, &log) == 0);
			CHECK(log.size == sizeof(expected) && memcmp(log.bytes, expected, sizeof(expected)) == 0);
			CHECK(s.status == 0xd0 && s.used == 0 && s.sysex_size == 0);
			free(s.sysex);
		}
	}
	struct st_midi_sink s = {.gain = gain}; struct log log = {0};
	for (size_t i = 0; i < sizeof(input); ++i)
		CHECK(st_midi_decode_stream(&s, input + i, 1, record, &log) == 0);
	CHECK(log.size == sizeof(expected) && memcmp(log.bytes, expected, sizeof(expected)) == 0);
	free(s.sysex);
	/* A raw SysEx may exceed the initial arena and contain realtime without
	 * putting those realtime bytes inside the assembled message. */
	memset(&s, 0, sizeof(s)); s.gain = gain; memset(&log, 0, sizeof(log));
	uint8_t large[602]; large[0] = 0xf0; large[1] = 0x7d;
	for (size_t i = 2; i < sizeof(large) - 1; ++i) large[i] = (uint8_t)(i & 127);
	large[sizeof(large) - 1] = 0xf7;
	CHECK(st_midi_decode_stream(&s, large, 300, record, &log) == 0);
	const uint8_t realtime = 0xfa;
	CHECK(st_midi_decode_stream(&s, &realtime, 1, record, &log) == 0);
	CHECK(log.size == 1 && log.bytes[0] == 0xfa);
	CHECK(st_midi_decode_stream(&s, large + 300, sizeof(large) - 300, record, &log) == 0);
	CHECK(log.messages == 2 && log.size == sizeof(large) + 1 && memcmp(log.bytes + 1, large, sizeof(large)) == 0);
	/* Check the bound before any out-of-arena write. */
	s.sysex_size = ST_MUSIC_STORAGE_LIMIT / 2;
	const uint8_t data = 1;
	CHECK(st_midi_decode_stream(&s, &data, 1, record, &log) == IO_ERROR_MESSAGE_TOO_LARGE);
	free(s.sysex);
}
int main(void)
{
	stream();
	const uint8_t data[] = {0x90,60,100,61,101,0xf8,0xf0,0x7e,0x7f,9,1,0xf7,0x80,60,0};
	const uint8_t expected[] = {0x90,60,100,0x90,61,101,0xf8,0xf0,0x7e,0x7f,9,1,0xf7,0x80,60,0};
	for (size_t split = 0; split <= sizeof(data); ++split) {
		struct st_midi_sink s = {.gain = gain}; struct log log = {0};
		CHECK(st_midi_decode(&s, data, split, record, &log) == 0);
		CHECK(st_midi_decode(&s, data + split, sizeof(data) - split, record, &log) == 0);
		CHECK(log.messages == 5 && log.size == sizeof(expected) && memcmp(log.bytes, expected, sizeof(expected)) == 0);
		free(s.sysex);
	}
	struct st_midi_sink s = {0}; struct log log = {0};
	memset(s.volume, 100, sizeof(s.volume)); memset(s.pan, 64, sizeof(s.pan));
	CHECK(st_midi_gain_controllers(&s, -6, -6, record, &log) == 0);
	CHECK(log.messages == 32 && log.bytes[1] == 7 && log.bytes[2] == 71);
	size_t count = log.messages;
	CHECK(st_midi_gain_controllers(&s, -6, -6, record, &log) == 0 && log.messages == count);
	const uint8_t volume[] = {0xb0,7,64};
	CHECK(st_midi_decode(&s, volume, sizeof(volume), record, &log) == 0);
	CHECK(log.bytes[log.size - 1] == 45 && s.volume[0] == 64);
	CHECK(st_midi_gain_controllers(&s, 0, 0, record, &log) == 0 && s.volume[0] == 64);
	/* CC121 preserves volume and pan per MIDI RP-015. */
	const uint8_t reset[] = {0xb0,121,0};
	CHECK(st_midi_decode(&s, reset, sizeof(reset), record, &log) == 0 && s.volume[0] == 64);
	CHECK(st_midi_gain_controllers(&s, -192, 0, record, &log) == 0);
	const uint8_t pan[] = {0xb0,10,0};
	CHECK(st_midi_decode(&s, pan, sizeof(pan), record, &log) == 0);
	CHECK(log.bytes[log.size - 2] == 7 && log.bytes[log.size - 1] == 0);
	const uint8_t vendor[] = {0xf0,0x7d,1,0xf7};
	count = log.size;
	CHECK(st_midi_decode(&s, vendor, sizeof(vendor), record, &log) == 0 && log.size == count + sizeof(vendor));
	const uint8_t gm[] = {0xf0,0x7e,0x7f,9,1,0xf7};
	CHECK(st_midi_decode(&s, gm, sizeof(gm), record, &log) == 0 && s.volume[0] == 100 && s.pan[0] == 64);
	const uint8_t invalid[] = {0x90,60,0x80,60,0};
	CHECK(st_midi_decode(&s, invalid, sizeof(invalid), record, &log) == IO_ERROR_INVALID_FORMAT);
	free(s.sysex);
	memset(&log, 0, sizeof(log));
	struct st_midi_sink release = {.state = &log, .send = record};
	st_midi_sink_release(&release, false);
	CHECK(log.messages == 64 && log.size == 192);
	const uint8_t controls[] = {64,66,69,123};
	for (unsigned ch = 0; ch < 16; ++ch) for (unsigned i = 0; i < 4; ++i) {
		size_t at = (ch * 4 + i) * 3;
		CHECK(log.bytes[at] == (0xb0 | ch) && log.bytes[at + 1] == controls[i] && log.bytes[at + 2] == 0);
	}
	memset(&log, 0, sizeof(log));
	release.set_ansi = mode;
	CHECK(st_midi_sink_ansi(&release) == 0);
	const uint8_t synth[] = {0xb0,0,0,0xb0,32,0,0xc0,81};
	CHECK(log.messages == 3 && log.size == sizeof(synth) && memcmp(log.bytes, synth, sizeof(synth)) == 0);
	CHECK(log.ansi && log.modes == 1);
	CHECK(st_midi_sink_start(&release) == 0 && !log.ansi && log.modes == 2);
	log.fail_mode = true;
	CHECK(st_midi_sink_ansi(&release) == IO_ERROR_IO && !log.ansi && log.modes == 3);
	CHECK(st_midi_sink_start(&release) == IO_ERROR_IO && !log.ansi && log.modes == 4);
	return 0;
}
