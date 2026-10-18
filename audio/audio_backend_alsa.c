/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "dynamic_library.h"
#include "io_error.h"

#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int (*alsa_pcm_open_fn)(snd_pcm_t **, const char *,
	snd_pcm_stream_t, int);
typedef int (*alsa_pcm_close_fn)(snd_pcm_t *);
typedef int (*alsa_pcm_set_params_fn)(snd_pcm_t *, snd_pcm_format_t,
	snd_pcm_access_t, unsigned, unsigned, int, unsigned);
typedef snd_pcm_sframes_t (*alsa_pcm_writei_fn)(snd_pcm_t *, const void *,
	snd_pcm_uframes_t);
typedef int (*alsa_pcm_recover_fn)(snd_pcm_t *, int, int);
typedef int (*alsa_pcm_delay_fn)(snd_pcm_t *, snd_pcm_sframes_t *);

struct alsa_api {
	int (*pcm_open)(snd_pcm_t **, const char *, snd_pcm_stream_t, int);
	int (*pcm_close)(snd_pcm_t *);
	int (*pcm_set_params)(snd_pcm_t *, snd_pcm_format_t,
	    snd_pcm_access_t, unsigned, unsigned, int, unsigned);
	snd_pcm_sframes_t (*pcm_writei)(snd_pcm_t *, const void *,
	    snd_pcm_uframes_t);
	int (*pcm_recover)(snd_pcm_t *, int, int);
	int (*pcm_delay)(snd_pcm_t *, snd_pcm_sframes_t *);
};

struct alsa_state {
	dynamic_library_t *library;
	struct alsa_api api;
	snd_pcm_t *pcm;
};

#define LOAD_ALSA(api, library, member, name) do { \
	void *st_audio_symbol = dynamic_library_symbol((library), (name)); \
	if (st_audio_symbol == NULL) { \
		(void)dynamic_library_close((library)); \
		return IO_ERROR_NOT_SUPPORTED; \
	} \
	(api)->member = (alsa_ ## member ## _fn)st_audio_symbol; \
} while (0)

static int
load_alsa(dynamic_library_t **result, struct alsa_api *api)
{
	static const char *const names[] = { "asound" };
	dynamic_library_t *library = NULL;
	int status;

	status = dynamic_library_open(&library, names, 1, 2,
	    DYNAMIC_LIBRARY_LOCAL);
	if (status != 0)
		return status;
	LOAD_ALSA(api, library, pcm_open, "snd_pcm_open");
	LOAD_ALSA(api, library, pcm_close, "snd_pcm_close");
	LOAD_ALSA(api, library, pcm_set_params, "snd_pcm_set_params");
	LOAD_ALSA(api, library, pcm_writei, "snd_pcm_writei");
	LOAD_ALSA(api, library, pcm_recover, "snd_pcm_recover");
	LOAD_ALSA(api, library, pcm_delay, "snd_pcm_delay");
	*result = library;
	return 0;
}

static bool
alsa_available(void)
{
	struct alsa_api api;
	dynamic_library_t *library = NULL;
	bool result = load_alsa(&library, &api) == 0;

	return result;
}

static int
alsa_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct alsa_state *state = calloc(1, sizeof(*state));
	int status;

	(void)callbacks;
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = load_alsa(&state->library, &state->api);
	if (status != 0
	    || state->api.pcm_open(&state->pcm, "default",
	    SND_PCM_STREAM_PLAYBACK, 0) < 0
	    || state->api.pcm_set_params(state->pcm, SND_PCM_FORMAT_S16,
	    SND_PCM_ACCESS_RW_INTERLEAVED, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE,
	    1, 50000) < 0) {
		if (state->pcm != NULL)
			(void)state->api.pcm_close(state->pcm);
		free(state);
		return status != 0 ? status : IO_ERROR_IO;
	}
	*result = state;
	return 0;
}

static void
alsa_close(void *opaque)
{
	struct alsa_state *state = opaque;

	if (state == NULL)
		return;
	(void)state->api.pcm_close(state->pcm);
	free(state);
}

static int
alsa_write(void *opaque, const int16_t *frames, size_t frame_count)
{
	struct alsa_state *state = opaque;
	const int16_t *position = frames;

	while (frame_count > 0) {
		snd_pcm_sframes_t written = state->api.pcm_writei(state->pcm,
		    position, frame_count);

		if (written < 0) {
			if (state->api.pcm_recover(state->pcm, (int)written, 1) < 0)
				return IO_ERROR_IO;
			continue;
		}
		if (written == 0)
			return IO_ERROR_IO;
		position += (size_t)written * AUDIO_CHANNELS;
		frame_count -= (size_t)written;
	}
	return 0;
}

static uint32_t
alsa_latency_ms(void *opaque)
{
	struct alsa_state *state = opaque;
	snd_pcm_sframes_t frames;

	if (state->api.pcm_delay(state->pcm, &frames) < 0 || frames <= 0)
		return 0;
	return (uint32_t)((uint64_t)frames * 1000 / AUDIO_SAMPLE_RATE);
}

const struct audio_backend_descriptor audio_backend_alsa = {
	AUDIO_BACKEND_ALSA,
	alsa_available,
	alsa_open,
	alsa_close,
	alsa_write,
	alsa_latency_ms,
};
