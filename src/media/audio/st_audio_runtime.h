/* Application-owned lazy audio-engine lifetime and backend policy. */

/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_AUDIO_RUNTIME_H_
#define ST_AUDIO_RUNTIME_H_

#include "audio.h"

#include <stdint.h>

typedef struct st_audio_runtime st_audio_runtime_t;

int st_audio_runtime_create(st_audio_runtime_t **result,
	uint32_t allowed_backends);
/* Frontend-owned playback; settings never open a physical audio device. */
int st_audio_runtime_create_pull(st_audio_runtime_t **result);
int st_audio_runtime_pull(st_audio_runtime_t *runtime, int16_t *frames,
	size_t frame_count);
void st_audio_runtime_stop(st_audio_runtime_t *runtime);
void st_audio_runtime_destroy(st_audio_runtime_t *runtime);
void st_audio_runtime_set_allowed(st_audio_runtime_t *runtime,
	uint32_t allowed_backends);
uint32_t st_audio_runtime_allowed(st_audio_runtime_t *runtime);
/* Independent MIDI policy; zero until the application installs its settings.
 * Stop/pull queries also let non-PCM music obey this runtime's lifetime. */
void st_audio_runtime_set_midi_allowed(st_audio_runtime_t *runtime, uint32_t allowed);
uint32_t st_audio_runtime_midi_allowed(st_audio_runtime_t *runtime);
bool st_audio_runtime_is_pull(st_audio_runtime_t *runtime);
bool st_audio_runtime_stopped(st_audio_runtime_t *runtime);
/* Returns a borrowed engine which remains valid until runtime destruction. */
int st_audio_runtime_acquire(st_audio_runtime_t *runtime,
	audio_engine_t **result);

#endif
