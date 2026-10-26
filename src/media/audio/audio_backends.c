/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"
#include "audio_config.h"

#include <stddef.h>

/* Preference order intentionally preserves the established xpbeep policy. */
const struct audio_backend_descriptor *const audio_backends[] = {
#if ST_AUDIO_HAVE_CORE_AUDIO
	&audio_backend_core_audio,
#endif
#if ST_AUDIO_HAVE_WASAPI
	&audio_backend_wasapi,
#endif
#if ST_AUDIO_HAVE_PORTAUDIO
	&audio_backend_portaudio,
#endif
#if ST_AUDIO_HAVE_PIPEWIRE
	&audio_backend_pipewire,
#endif
#if ST_AUDIO_HAVE_SDL
	&audio_backend_sdl,
#endif
#if ST_AUDIO_HAVE_PULSE_AUDIO
	&audio_backend_pulse_audio,
#endif
#if ST_AUDIO_HAVE_ALSA
	&audio_backend_alsa,
#endif
#if ST_AUDIO_HAVE_OSS
	&audio_backend_oss,
#endif
#if !ST_AUDIO_HAVE_CORE_AUDIO && !ST_AUDIO_HAVE_WASAPI \
    && !ST_AUDIO_HAVE_PORTAUDIO && !ST_AUDIO_HAVE_PIPEWIRE \
    && !ST_AUDIO_HAVE_SDL && !ST_AUDIO_HAVE_PULSE_AUDIO \
    && !ST_AUDIO_HAVE_ALSA && !ST_AUDIO_HAVE_OSS
	NULL,
#endif
};

#if !ST_AUDIO_HAVE_CORE_AUDIO && !ST_AUDIO_HAVE_WASAPI \
    && !ST_AUDIO_HAVE_PORTAUDIO && !ST_AUDIO_HAVE_PIPEWIRE \
    && !ST_AUDIO_HAVE_SDL && !ST_AUDIO_HAVE_PULSE_AUDIO \
    && !ST_AUDIO_HAVE_ALSA && !ST_AUDIO_HAVE_OSS
const size_t audio_backend_count = 0;
#else
const size_t audio_backend_count =
	sizeof(audio_backends) / sizeof(audio_backends[0]);
#endif
