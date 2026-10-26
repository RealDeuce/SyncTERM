/* Private contract between the audio engine and playback backends. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_AUDIO_INTERNAL_H_
#define ST_AUDIO_INTERNAL_H_

#include "audio.h"

#include <stddef.h>
#include <stdint.h>

struct audio_backend_callbacks {
	void *context;
	size_t (*pull)(void *context, int16_t *frames, size_t frame_count);
	void (*failed)(void *context);
};

struct audio_backend_descriptor {
	enum audio_backend backend;
	bool (*available)(void);
	int (*open)(void **result,
	    const struct audio_backend_callbacks *callbacks);
	void (*close)(void *state);
	/* Non-NULL only for a blocking backend pumped by the engine worker. */
	int (*write)(void *state, const int16_t *frames, size_t frame_count);
	uint32_t (*latency_ms)(void *state);
};

int audio_engine_create_with_backends(audio_engine_t **result,
	uint32_t allowed_backends,
	const struct audio_backend_descriptor *const backends[],
	size_t backend_count);

#if defined(ST_AUDIO_TESTING)
size_t audio_engine_stream_count_for_test(audio_engine_t *engine);
#endif

extern const struct audio_backend_descriptor audio_backend_alsa;
extern const struct audio_backend_descriptor audio_backend_core_audio;
extern const struct audio_backend_descriptor audio_backend_oss;
extern const struct audio_backend_descriptor audio_backend_pipewire;
extern const struct audio_backend_descriptor audio_backend_portaudio;
extern const struct audio_backend_descriptor audio_backend_pulse_audio;
extern const struct audio_backend_descriptor audio_backend_sdl;
extern const struct audio_backend_descriptor audio_backend_wasapi;

int audio_platform_beep(double frequency, uint32_t duration_ms);

#endif
