/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "dynamic_library.h"
#include "io_error.h"

#include <portaudio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef PaError (*pa_initialize_fn)(void);
typedef PaError (*pa_terminate_fn)(void);
typedef PaError (*pa_open_default_stream_fn)(PaStream **, int, int,
	PaSampleFormat, double, unsigned long, PaStreamCallback *, void *);
typedef PaError (*pa_start_stream_fn)(PaStream *);
typedef PaError (*pa_stop_stream_fn)(PaStream *);
typedef PaError (*pa_close_stream_fn)(PaStream *);
typedef const PaStreamInfo *(*pa_get_stream_info_fn)(PaStream *);

struct portaudio_api {
	PaError (*initialize)(void);
	PaError (*terminate)(void);
	PaError (*open_default_stream)(PaStream **, int, int,
	    PaSampleFormat, double, unsigned long, PaStreamCallback *, void *);
	PaError (*start_stream)(PaStream *);
	PaError (*stop_stream)(PaStream *);
	PaError (*close_stream)(PaStream *);
	const PaStreamInfo *(*get_stream_info)(PaStream *);
};

struct portaudio_state {
	dynamic_library_t *library;
	struct portaudio_api api;
	struct audio_backend_callbacks callbacks;
	PaStream *stream;
};

#define LOAD_PA(api, library, member, name) do { \
	void *st_audio_symbol = dynamic_library_symbol((library), (name)); \
	if (st_audio_symbol == NULL) { \
		(void)dynamic_library_close((library)); \
		return IO_ERROR_NOT_SUPPORTED; \
	} \
	(api)->member = (pa_ ## member ## _fn)st_audio_symbol; \
} while (0)

static int
load_portaudio(dynamic_library_t **result, struct portaudio_api *api)
{
	static const char *const names[] = {
		"portaudio", "libportaudio-2", "libportaudio"
	};
	dynamic_library_t *library = NULL;
	int status = dynamic_library_open(&library, names, 3, 2,
	    DYNAMIC_LIBRARY_LOCAL);

	if (status != 0)
		return status;
	LOAD_PA(api, library, initialize, "Pa_Initialize");
	LOAD_PA(api, library, terminate, "Pa_Terminate");
	LOAD_PA(api, library, open_default_stream, "Pa_OpenDefaultStream");
	LOAD_PA(api, library, start_stream, "Pa_StartStream");
	LOAD_PA(api, library, stop_stream, "Pa_StopStream");
	LOAD_PA(api, library, close_stream, "Pa_CloseStream");
	LOAD_PA(api, library, get_stream_info, "Pa_GetStreamInfo");
	*result = library;
	return 0;
}

static bool
portaudio_available(void)
{
	struct portaudio_api api;
	dynamic_library_t *library = NULL;
	bool result = load_portaudio(&library, &api) == 0;

	return result;
}

static int
portaudio_callback(const void *input, void *output,
    unsigned long frame_count, const PaStreamCallbackTimeInfo *time_info,
    PaStreamCallbackFlags flags, void *user_data)
{
	struct portaudio_state *state = user_data;

	(void)input;
	(void)time_info;
	(void)flags;
	(void)state->callbacks.pull(state->callbacks.context, output,
	    (size_t)frame_count);
	return paContinue;
}

static int
portaudio_open(void **result,
    const struct audio_backend_callbacks *callbacks)
{
	struct portaudio_state *state = calloc(1, sizeof(*state));
	int status;

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->callbacks = *callbacks;
	status = load_portaudio(&state->library, &state->api);
	if (status != 0 || state->api.initialize() != paNoError) {
		free(state);
		return status != 0 ? status : IO_ERROR_IO;
	}
	if (state->api.open_default_stream(&state->stream, 0,
	    AUDIO_CHANNELS, paInt16, AUDIO_SAMPLE_RATE, 512,
	    portaudio_callback, state) != paNoError
	    || state->api.start_stream(state->stream) != paNoError) {
		if (state->stream != NULL)
			(void)state->api.close_stream(state->stream);
		(void)state->api.terminate();
		free(state);
		return IO_ERROR_IO;
	}
	*result = state;
	return 0;
}

static void
portaudio_close(void *opaque)
{
	struct portaudio_state *state = opaque;

	if (state == NULL)
		return;
	(void)state->api.stop_stream(state->stream);
	(void)state->api.close_stream(state->stream);
	(void)state->api.terminate();
	free(state);
}

static uint32_t
portaudio_latency_ms(void *opaque)
{
	struct portaudio_state *state = opaque;
	const PaStreamInfo *info = state->api.get_stream_info(state->stream);

	if (info == NULL || info->outputLatency <= 0.0)
		return 0;
	return (uint32_t)(info->outputLatency * 1000.0 + 0.5);
}

const struct audio_backend_descriptor audio_backend_portaudio = {
	AUDIO_BACKEND_PORTAUDIO,
	portaudio_available,
	portaudio_open,
	portaudio_close,
	NULL,
	portaudio_latency_ms,
};
