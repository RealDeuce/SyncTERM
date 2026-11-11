/* Session music: ANSI notes, MIDI files/live input, and logical channel 0. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_MUSIC_H_
#define ST_MUSIC_H_

#include "st_audio_runtime.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum st_midi_backend {
	ST_MIDI_ALSA = 1U << 0,
	ST_MIDI_FLUIDSYNTH = 1U << 1,
	ST_MIDI_ADLMIDI = 1U << 2,
	ST_MIDI_WINMM = 1U << 3,
	ST_MIDI_APPLE = 1U << 4,
};
enum st_midi_linkage { ST_MIDI_NOT_BUILT, ST_MIDI_RUNTIME, ST_MIDI_STATIC, ST_MIDI_SYSTEM, ST_MIDI_SHARED };
uint32_t st_midi_compiled_backends(void);
enum st_midi_linkage st_midi_backend_linkage(enum st_midi_backend backend);

struct st_ansi_music_state {
	int tempo, octave, length, shape;
	bool foreground;
};
struct st_music_note {
	/* ANSI note 0..83 (MIDI 36..119), or -1 for a rest. */
	int pitch;
	size_t sound_frames, pause_frames;
};
void st_ansi_music_init(struct st_ansi_music_state *state);
/* Input is a completed, validated capture. Result is malloc-owned.
 * State is committed only on success; timing retains integer-ms rounding. */
int st_ansi_music_parse(const char *text, bool prefix_modes,
	struct st_ansi_music_state *state, struct st_music_note **notes,
	size_t *count, bool *foreground);
double st_music_note_frequency(int pitch);

typedef struct st_music st_music_t;
/* Queue/control calls are thread-safe. The owner must finish all callers
 * before destroying the player, as with the borrowed audio runtime. */
/* runtime and the PCM slot are borrowed through destruction. Only the player
 * accesses the slot while running. The player owns any stream in that slot;
 * tests may install a silent stream before the first operation. */
int st_music_create(st_music_t **result, st_audio_runtime_t *runtime,
	audio_stream_t **pcm_slot);
void st_music_destroy(st_music_t *music);
bool st_music_available(st_music_t *music);
/* Queue takes ownership only on success. FILE must be seekable and opened
 * through the caller's existing file authority. Queue never stores a path. */
int st_music_queue_file(st_music_t *music, FILE *file);
int st_music_queue_notes(st_music_t *music, struct st_music_note *notes,
	size_t count);
/* Copies live MIDI wire bytes into the same bounded worker FIFO. Timing is
 * supplied by the caller; running status/partial messages persist across
 * writes. No start/reset/release messages are added at write boundaries.
 * Held voices keep sounding (and software sinks keep rendering) until wire
 * input stops them, a file/ANSI request takes over, or flush/destroy/error.
 * Busy remains true during this live session. Wait acknowledges earlier
 * writes at the destination/PCM position, without waiting for held voices.
 * A zero-length write is a no-op and does not select/open a destination. */
int st_music_queue_midi(st_music_t *music, const uint8_t *bytes, size_t count);
/* Wait covers requests accepted before the call, not later requests.
 * Cancellation wakes waiters. Idle includes native output and PCM drain. */
int st_music_wait(st_music_t *music);
bool st_music_busy(st_music_t *music);
int st_music_flush(st_music_t *music, size_t fade_frames);
int st_music_volume(st_music_t *music, float left_db, float right_db,
	size_t ramp_frames);
int st_music_get_volume(st_music_t *music, float *left_db, float *right_db);
/* Borrowed test/diagnostic view. Do not race it with destruction or reset. */
audio_stream_t *st_music_pcm_stream(st_music_t *music);

#endif
