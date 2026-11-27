/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "midi_file.h"
#include "st_cmf.h"
#include "st_opl2.h"

struct event {
	uint64_t frame;
	uint8_t  status, a, b;
};

struct voice {
	unsigned channel, note;
	uint64_t age;
	bool     key;
};

struct st_cmf {
	struct st_opl2  chip;
	audio_stream_t *stream;
	struct event   *events;
	size_t          count, next;
	uint64_t        duration, frame, age;
	uint8_t         patches[256][16];
	unsigned        patch_count;
	struct voice    voices[9];
	uint8_t         programs[16], volume[16], rhythm;
	int             bend[16];
};

static unsigned
word(const uint8_t *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static uint64_t
frame_at(uint64_t ns)
{
	return ns / 1000000000 * AUDIO_SAMPLE_RATE + ns % 1000000000 * AUDIO_SAMPLE_RATE / 1000000000;
}

static bool
load(struct st_cmf *p, FILE *file)
{
	uint8_t h[40];
	int64_t end = fs_stream_size(file);
	if (end < 37 || fs_stream_seek(file, 0) != 0 || fread(h, 1, 37, file) != 37 || memcmp(h, "CTMF", 4))
		return false;
	unsigned version = word(h + 4), bank = word(h + 6), music = word(h + 8), header_size = 37;
	if (version == 0x101) {
		if (fread(h + 37, 1, 3, file) != 3)
			return false;
		header_size    = 40;
		p->patch_count = word(h + 36);
	}
	else if (version == 0x100)
		p->patch_count = h[36];
	else
		return false;
	int64_t bank_end  = bank + p->patch_count * 16;
	int64_t track_end = bank > music ? bank : end;
	if (p->patch_count == 0 || p->patch_count > 256 || bank < header_size || music < header_size
	    || bank_end > end || (bank <= music && music < bank_end) || track_end <= music
	    || track_end - music > ST_MUSIC_STORAGE_LIMIT || fs_stream_seek(file, bank) != 0
	    || fread(p->patches, 16, p->patch_count, file) != p->patch_count)
		return false;
	struct st_midi_file *track = NULL;
	if (st_midi_track_open(&track, file, music, track_end, (uint16_t)word(h + 12)) != 0)
		return false;

	/* Decode once on the session thread. The real-time callback only visits
	 * complete, owned events and never seeks, allocates or reads a file. */
	struct st_midi_event e;
	size_t               capacity = 0;
	int                  status;
	while ((status = st_midi_file_next(track, &e)) == 0) {
		if (e.size < 2 || e.bytes[0] >= 0xf0)
			continue;
		if (p->count == capacity) {
			capacity = capacity ? capacity * 2 : 128;
			if (capacity > ST_MUSIC_STORAGE_LIMIT / sizeof(*p->events))
				break;
			struct event *events = realloc(p->events, capacity * sizeof(*events));
			if (events == NULL)
				break;
			p->events = events;
		}
		p->events[p->count++] =
		    (struct event) {frame_at(e.time_ns), e.bytes[0], e.bytes[1], e.size == 3 ? e.bytes[2] : 0};
	}
	if (status == 1)
		p->duration = frame_at(e.time_ns);
	st_midi_file_close(track);
	return status == 1 && p->duration > 0;
}

static const uint8_t operators[9]   = {0, 1, 2, 8, 9, 10, 16, 17, 18};
static const uint8_t drum_voices[5] = {6, 7, 8, 8, 7};
static const uint8_t drum_ops[5]    = {16, 20, 18, 21, 17};

static void
instrument(struct st_cmf *p, const uint8_t *patch, unsigned op, unsigned which, unsigned velocity)
{
	static const unsigned regs[] = {0x20, 0x40, 0x60, 0x80, 0xe0};
	for (unsigned i = 0; i < 5; ++i) {
		unsigned value = patch[2 * i + which];
		if (i == 1) {
			unsigned level = 63 - ((63 - (value & 63)) * velocity + 63) / 127;
			value          = (value & 0xc0) | level;
		}
		st_opl2_write(&p->chip, regs[i] + op, (uint8_t)value);
	}
}

static void
pitch(struct st_cmf *p, unsigned voice, unsigned note, int bend, bool key)
{
	double   hz    = 440.0 * exp2(((int)note - 69 + bend / 128.0) / 12.0);
	unsigned block = 0;
	double   fnum  = hz * 1048576.0 / ST_OPL2_NATIVE_RATE;
	while (fnum > 1023 && block < 7) {
		fnum *= .5;
		++block;
	}
	unsigned n = fnum >= 1023 ? 1023 : (unsigned)(fnum + .5);
	st_opl2_write(&p->chip, 0xa0 + voice, (uint8_t)n);
	st_opl2_write(&p->chip, 0xb0 + voice, (uint8_t)((key ? 0x20 : 0) | block << 2 | (n >> 8)));
}

static void
note(struct st_cmf *p, unsigned channel, unsigned key, unsigned velocity)
{
	const uint8_t *patch = p->patches[p->programs[channel] % p->patch_count];
	unsigned       level = velocity * p->volume[channel] / 127;
	if ((p->rhythm & 0x20) && channel >= 11) {
		unsigned drum = channel - 11, bit = 0x10 >> drum, voice = drum_voices[drum];
		p->rhythm &= ~bit;
		st_opl2_write(&p->chip, 0xbd, p->rhythm);
		if (velocity) {
			instrument(p, patch, drum_ops[drum], 0, drum == 0 && !(patch[10] & 1) ? 127 : level);
			if (!drum)
				instrument(p, patch, drum_ops[0] + 3, 1, level);
			st_opl2_write(&p->chip, 0xc0 + voice, patch[10]);
			pitch(p, voice, key, p->bend[channel], false);
			p->rhythm |= bit;
			st_opl2_write(&p->chip, 0xbd, p->rhythm);
		}
		return;
	}

	unsigned count = p->rhythm & 0x20 ? 6 : 9, selected = 0;
	for (unsigned i = 0; i < count; ++i) {
		struct voice *v = &p->voices[i];
		if (v->key && v->channel == channel && v->note == key) {
			selected = i;
			break;
		}
		if ((!v->key && p->voices[selected].key)
		    || (v->key == p->voices[selected].key && v->age < p->voices[selected].age))
			selected = i;
	}
	struct voice *v = &p->voices[selected];
	if (v->key && (velocity || (v->channel == channel && v->note == key))) {
		pitch(p, selected, v->note, p->bend[v->channel], false);
		v->key = false;
	}
	if (velocity) {
		unsigned op = operators[selected];
		instrument(p, patch, op, 0, patch[10] & 1 ? level : 127);
		instrument(p, patch, op + 3, 1, level);
		st_opl2_write(&p->chip, 0xc0 + selected, patch[10]);
		*v = (struct voice) {channel, key, ++p->age, true};
		pitch(p, selected, key, p->bend[channel], true);
	}
}

static void
dispatch(struct st_cmf *p, const struct event *e)
{
	unsigned channel = e->status & 15, a = e->a, b = e->b;
	switch (e->status >> 4) {
		case 8:
			note(p, channel, a, 0);
			return;
		case 9:
			note(p, channel, a, b);
			return;
		case 12:
			p->programs[channel] = a;
			return;
		case 14:
			p->bend[channel] = ((int)(b * 128 + a) - 8192) / 32;
			break;
		case 11:
			switch (a) {
				case 7:
					p->volume[channel] = b;
					return;
				case 103:
					for (unsigned i = 6; i < 9; ++i)
						if (p->voices[i].key) {
							pitch(p, i, p->voices[i].note, 0, false);
							p->voices[i].key = false;
						}
					p->rhythm = (p->rhythm & 0xc0) | (b ? 0x20 : 0);
					st_opl2_write(&p->chip, 0xbd, p->rhythm);
					return;
				case 104:
					p->bend[channel] = (int)b;
					break;
				case 105:
					p->bend[channel] = -(int)b;
					break;
				default:
					return;
			}
			break;
		default:
			return;
	}
	for (unsigned i = 0; i < 9; ++i)
		if (p->voices[i].key && p->voices[i].channel == channel)
			pitch(p, i, p->voices[i].note, p->bend[channel], true);
}

static void
rewind_song(struct st_cmf *p)
{
	st_opl2_init(&p->chip, AUDIO_SAMPLE_RATE);
	st_opl2_write(&p->chip, 1, 0x20);
	p->rhythm = 0xc0;
	st_opl2_write(&p->chip, 0xbd, p->rhythm);
	memset(p->programs, 0, sizeof(p->programs));
	memset(p->volume, 127, sizeof(p->volume));
	memset(p->voices, 0, sizeof(p->voices));
	memset(p->bend, 0, sizeof(p->bend));
	p->next = p->frame = p->age = 0;
}

static size_t
render(void *context, int32_t *stereo, size_t frames)
{
	struct st_cmf *p        = context;
	size_t         rendered = 0;
	while (rendered < frames) {
		if (p->frame >= p->duration)
			rewind_song(p);
		while (p->next < p->count && p->events[p->next].frame <= p->frame)
			dispatch(p, &p->events[p->next++]);
		uint64_t next = p->next < p->count ? p->events[p->next].frame : p->duration;
		int32_t  mono[256];
		size_t   n = frames - rendered;
		if (n > 256)
			n = 256;
		if (n > next - p->frame)
			n = (size_t)(next - p->frame);
		st_opl2_render(&p->chip, mono, n);
		for (size_t i = 0; i < n; ++i)
			stereo[2 * (rendered + i)] = stereo[2 * (rendered + i) + 1] = mono[i];
		rendered += n;
		p->frame += n;
	}
	return rendered;
}

void
st_cmf_destroy(struct st_cmf *p)
{
	if (p != NULL) {
		audio_stream_destroy(p->stream);
		free(p->events);
		free(p);
	}
}

struct st_cmf *
st_cmf_play(st_audio_runtime_t *audio, FILE *file)
{
	struct st_cmf *p = calloc(1, sizeof(*p));
	if (p == NULL)
		return NULL;
	audio_engine_t *engine;
	if (load(p, file) && st_audio_runtime_acquire(audio, &engine) == 0
	    && audio_stream_create(&p->stream, engine, 0, 0) == 0) {
		rewind_song(p);
		if (audio_stream_set_source32(p->stream, render, p) == 0
		    && audio_stream_source_active(p->stream, true) == 0)
			return p;
	}
	st_cmf_destroy(p);
	return NULL;
}
