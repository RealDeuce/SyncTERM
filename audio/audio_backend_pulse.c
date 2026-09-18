/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "dynamic_library.h"
#include "io_error.h"

#include <pulse/error.h>
#include <pulse/simple.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef pa_simple *(*pulse_simple_new_fn)(const char *, const char *,
	pa_stream_direction_t, const char *, const char *, const pa_sample_spec *,
	const pa_channel_map *, const pa_buffer_attr *, int *);
typedef void (*pulse_simple_free_fn)(pa_simple *);
typedef int (*pulse_simple_write_fn)(pa_simple *, const void *, size_t, int *);
typedef pa_usec_t (*pulse_simple_get_latency_fn)(pa_simple *, int *);

struct pulse_api {
	pa_simple *(*simple_new)(const char *, const char *, pa_stream_direction_t,
	    const char *, const char *, const pa_sample_spec *,
	    const pa_channel_map *, const pa_buffer_attr *, int *);
	void (*simple_free)(pa_simple *);
	int (*simple_write)(pa_simple *, const void *, size_t, int *);
	pa_usec_t (*simple_get_latency)(pa_simple *, int *);
};

struct pulse_state {
	dynamic_library_t *library;
	struct pulse_api api;
	pa_simple *stream;
};

#define LOAD_PULSE(api, library, member, name) do { \
	void *st_audio_symbol = dynamic_library_symbol((library), (name)); \
	if (st_audio_symbol == NULL) { \
		(void)dynamic_library_close((library)); \
		return IO_ERROR_NOT_SUPPORTED; \
	} \
	(api)->member = (pulse_ ## member ## _fn)st_audio_symbol; \
} while (0)

static int
load_pulse(dynamic_library_t **result, struct pulse_api *api)
{
	static const char *const names[] = {
		"pulse-simple", "libpulse-simple-0", "libpulse-simple"
	};
	dynamic_library_t *library = NULL;
	int status = dynamic_library_open(&library, names, 3, 0,
	    DYNAMIC_LIBRARY_LOCAL);

	if (status != 0)
		return status;
	LOAD_PULSE(api, library, simple_new, "pa_simple_new");
	LOAD_PULSE(api, library, simple_free, "pa_simple_free");
	LOAD_PULSE(api, library, simple_write, "pa_simple_write");
	LOAD_PULSE(api, library, simple_get_latency, "pa_simple_get_latency");
	*result = library;
	return 0;
}

static bool
pulse_available(void)
{
	struct pulse_api api;
	dynamic_library_t *library = NULL;
	bool result = load_pulse(&library, &api) == 0;

	(void)dynamic_library_close(library);
	return result;
}

static int
pulse_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct pulse_state *state = calloc(1, sizeof(*state));
	const pa_sample_spec specification = {
		PA_SAMPLE_S16NE, AUDIO_SAMPLE_RATE, AUDIO_CHANNELS
	};
	int error;
	int status;

	(void)callbacks;
	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = load_pulse(&state->library, &state->api);
	if (status == 0)
		state->stream = state->api.simple_new(NULL, "SyncTERM",
		    PA_STREAM_PLAYBACK, NULL, "audio", &specification, NULL,
		    NULL, &error);
	if (status != 0 || state->stream == NULL) {
		(void)dynamic_library_close(state->library);
		free(state);
		return status != 0 ? status : IO_ERROR_IO;
	}
	*result = state;
	return 0;
}

static void
pulse_close(void *opaque)
{
	struct pulse_state *state = opaque;

	if (state == NULL)
		return;
	state->api.simple_free(state->stream);
	(void)dynamic_library_close(state->library);
	free(state);
}

static int
pulse_write(void *opaque, const int16_t *frames, size_t frame_count)
{
	struct pulse_state *state = opaque;
	int error;

	return state->api.simple_write(state->stream, frames,
	    frame_count * AUDIO_FRAME_SIZE, &error) < 0 ? IO_ERROR_IO : 0;
}

static uint32_t
pulse_latency_ms(void *opaque)
{
	struct pulse_state *state = opaque;
	int error;
	pa_usec_t latency = state->api.simple_get_latency(state->stream, &error);

	return latency == (pa_usec_t)-1 ? 0 : (uint32_t)(latency / 1000);
}

const struct audio_backend_descriptor audio_backend_pulse_audio = {
	AUDIO_BACKEND_PULSE_AUDIO,
	pulse_available,
	pulse_open,
	pulse_close,
	pulse_write,
	pulse_latency_ms,
};
