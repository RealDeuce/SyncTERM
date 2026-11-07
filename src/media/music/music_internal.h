/* Private MIDI adapter contract. All methods run on the music worker, except
 * initialization before its first request. Native callbacks only acknowledge
 * owned buffers. Backends never call the terminal or Wren. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_MUSIC_INTERNAL_H_
#define ST_MUSIC_INTERNAL_H_
#include "st_music.h"
#include "midi_file.h"
#include "io_error.h"

struct st_midi_sink {
	enum st_midi_backend kind;
	void *state;
	int (*send)(void *, const uint8_t *, size_t);
	int (*render)(void *, int16_t *, size_t);
	/* Source calibration is combined with channel gain by the wide mixer. */
	float pcm_gain_db;
	int (*gain)(void *, float, float);
	void (*close)(void *);
	/* Native send/drain operations consult this when awaiting completion. */
	bool (*cancelled)(void *);
	void *cancel_context;
	/* Decoder state also retains file CC7/CC10 beneath logical stream gain. */
	uint8_t status, message[3], volume[16], pan[16];
	uint8_t sent_volume[16], sent_pan[16], gain_sent[16];
	size_t used, needed;
	uint8_t *sysex;
	size_t sysex_size, sysex_capacity;
	float left_db, right_db;
	bool validate_only;
};
typedef int (*st_midi_emit_t)(void *, const uint8_t *, size_t);
int st_midi_decode(struct st_midi_sink *sink, const uint8_t *bytes,
	size_t size, st_midi_emit_t emit, void *context);
int st_midi_gain_controllers(struct st_midi_sink *sink, float left, float right,
	st_midi_emit_t emit, void *context);
int st_midi_sink_open(struct st_midi_sink *sink, enum st_midi_backend kind,
	bool (*cancelled)(void *), void *context);
void st_midi_sink_close(struct st_midi_sink *sink);
int st_midi_sink_start(struct st_midi_sink *sink);
int st_midi_sink_ansi(struct st_midi_sink *sink);
int st_midi_sink_calibrate(struct st_midi_sink *sink);
int st_midi_sink_send(struct st_midi_sink *sink, const uint8_t *data, size_t size);
int st_midi_sink_gain(struct st_midi_sink *sink, float left, float right);
void st_midi_sink_release(struct st_midi_sink *sink, bool immediate);

int st_midi_fluid_open(struct st_midi_sink *sink);
int st_midi_adl_open(struct st_midi_sink *sink);
int st_midi_alsa_open(struct st_midi_sink *sink);
int st_midi_winmm_open(struct st_midi_sink *sink);
int st_midi_apple_open(struct st_midi_sink *sink);

/* Test-only construction follows the audio component's injected-backend
 * pattern, without a user preference or physical MIDI device. */
int st_music_create_with_sink(st_music_t **result, st_audio_runtime_t *runtime,
	audio_stream_t **pcm_slot, const struct st_midi_sink *sink);
#endif
