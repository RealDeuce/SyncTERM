/* Fixed-format audio playback and synthesis. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_AUDIO_H_
#define ST_AUDIO_H_

#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_SAMPLE_RATE 44100U
#define AUDIO_CHANNELS 2U
#define AUDIO_FRAME_SIZE (AUDIO_CHANNELS * sizeof(int16_t))

enum audio_backend {
	AUDIO_BACKEND_NONE = 0,
	AUDIO_BACKEND_WASAPI = 1U << 0,
	AUDIO_BACKEND_ALSA = 1U << 1,
	AUDIO_BACKEND_OSS = 1U << 2,
	AUDIO_BACKEND_SDL = 1U << 3,
	AUDIO_BACKEND_PORTAUDIO = 1U << 4,
	AUDIO_BACKEND_PULSE_AUDIO = 1U << 5,
	AUDIO_BACKEND_CORE_AUDIO = 1U << 6,
	AUDIO_BACKEND_PIPEWIRE = 1U << 7,
};

#define AUDIO_BACKENDS_DEFAULT \
	(AUDIO_BACKEND_WASAPI | AUDIO_BACKEND_ALSA | AUDIO_BACKEND_OSS \
	| AUDIO_BACKEND_PORTAUDIO | AUDIO_BACKEND_PULSE_AUDIO \
	| AUDIO_BACKEND_CORE_AUDIO | AUDIO_BACKEND_PIPEWIRE)

enum audio_wave_shape {
	AUDIO_WAVE_SINE,
	AUDIO_WAVE_SAWTOOTH,
	AUDIO_WAVE_SQUARE,
	AUDIO_WAVE_SINE_SAW,
	AUDIO_WAVE_SINE_HARM,
	AUDIO_WAVE_SINE_SAW_CHORD,
	AUDIO_WAVE_SINE_SAW_HARM,
};

struct audio_buffer_options {
	float volume_left_db;
	float volume_right_db;
	size_t fade_in_frames;
	size_t fade_out_frames;
	bool crossfade;
	bool loop;
};

#define AUDIO_BUFFER_OPTIONS_INIT { 0.0f, 0.0f, 0, 0, false, false }

typedef struct audio_engine audio_engine_t;
typedef struct audio_stream audio_stream_t;

/*
 * All PCM is native-endian signed 16-bit stereo at AUDIO_SAMPLE_RATE.
 * Counts passed to the API are frame counts, not sample or byte counts.
 * Volumes are decibels in the inclusive range -192 dB through +96 dB;
 * zero-initialized options therefore mean unity gain and no envelope.
 *
 * A buffer's fade applies at mix time. A loop repeats until another buffer is
 * appended; crossfade makes that successor overlap the prior tail for its
 * fade-in length. Queued buffer and completed-crossfade boundaries do not add
 * silence within a device pull. Appending never waits for playback.
 */

/*
 * Returns compiled candidates whose required runtime library is currently
 * loadable. A device can still be unavailable when an engine attempts to
 * open it.
 */
uint32_t audio_available_backends(void);
/* Returns compiled candidates without probing runtime-loaded libraries. */
uint32_t audio_compiled_backends(void);

/*
 * Result is unchanged on failure. Only one engine may exist per process.
 * Backends allowed by the mask are tried in this order: CoreAudio, WASAPI,
 * PortAudio, PipeWire, SDL, PulseAudio, ALSA, then OSS. SDL support is an
 * explicit build-time opt-in because its host must obey SDL's main-thread
 * requirements.
 * Engine destruction stops callbacks/workers, discards queued audio, and
 * destroys any streams the caller has not already destroyed.
 */
int audio_engine_create(audio_engine_t **result, uint32_t allowed_backends);
void audio_engine_destroy(audio_engine_t *engine);
enum audio_backend audio_engine_backend(const audio_engine_t *engine);

int audio_stream_create(audio_stream_t **result, audio_engine_t *engine,
	float volume_left_db, float volume_right_db);
void audio_stream_destroy(audio_stream_t *stream);

int audio_stream_append(audio_stream_t *stream, const int16_t *frames,
	size_t frame_count, const struct audio_buffer_options *options);
/* Ownership transfers only when this function succeeds. */
int audio_stream_append_take(audio_stream_t *stream, int16_t *frames,
	size_t frame_count, const struct audio_buffer_options *options);
int audio_stream_append_u8_mono_22050(audio_stream_t *stream,
	const uint8_t *samples, size_t sample_count,
	const struct audio_buffer_options *options);
int audio_stream_clear(audio_stream_t *stream);
/*
 * Drain waits for the buffers present when it is called, not later appends.
 * Clear cancels a concurrent drain while those buffers are still queued or
 * awaiting mixer acknowledgment. Once they have been submitted and
 * acknowledged, clear cannot retract them and does not cancel the final
 * backend-latency wait. An unterminated loop returns IO_ERROR_BUSY instead of
 * waiting forever.
 */
int audio_stream_drain(audio_stream_t *stream);
bool audio_stream_is_idle(audio_stream_t *stream);
int audio_stream_set_volume(audio_stream_t *stream,
	float volume_left_db, float volume_right_db);
int audio_stream_ramp_volume(audio_stream_t *stream,
	float volume_left_db, float volume_right_db, size_t frame_count);
int audio_stream_get_volume(audio_stream_t *stream,
	float *volume_left_db, float *volume_right_db);

/*
 * Copies the supplied frames and returns before playback completes. A
 * one-shot cannot use the loop option because it exposes no stream to stop.
 */
int audio_engine_play(audio_engine_t *engine, const int16_t *frames,
	size_t frame_count, const struct audio_buffer_options *options);
int audio_engine_play_u8_mono_22050(audio_engine_t *engine,
	const uint8_t *samples, size_t sample_count,
	const struct audio_buffer_options *options);

/* Nonnegative finite frequencies above Nyquist retain the reference
 * generator's aliased waveform. Unrepresentable phase ranges fail. */
int audio_generate_tone(int16_t *result, size_t frame_count,
	double frequency, enum audio_wave_shape shape, bool clean_edges);

/* Plays the historic SyncTERM beep synchronously, with speaker fallback. */
int audio_beep(audio_engine_t *engine, double frequency,
	uint32_t duration_ms);

/*
 * Stream operations are thread-safe. The caller must not race stream or
 * engine destruction with any operation using the object being destroyed.
 */

#endif
