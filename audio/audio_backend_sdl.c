/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "dynamic_library.h"
#include "io_error.h"

#include <SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef Uint32 (*audio_sdl_was_init_fn)(Uint32);
typedef int (*audio_sdl_init_subsystem_fn)(Uint32);
typedef void (*audio_sdl_quit_subsystem_fn)(Uint32);
typedef SDL_AudioDeviceID (*audio_sdl_open_audio_device_fn)(const char *, int,
	const SDL_AudioSpec *, SDL_AudioSpec *, int);
typedef void (*audio_sdl_close_audio_device_fn)(SDL_AudioDeviceID);
typedef void (*audio_sdl_pause_audio_device_fn)(SDL_AudioDeviceID, int);

struct sdl_api {
	Uint32 (*was_init)(Uint32);
	int (*init_subsystem)(Uint32);
	void (*quit_subsystem)(Uint32);
	SDL_AudioDeviceID (*open_audio_device)(const char *, int,
	    const SDL_AudioSpec *, SDL_AudioSpec *, int);
	void (*close_audio_device)(SDL_AudioDeviceID);
	void (*pause_audio_device)(SDL_AudioDeviceID, int);
};

struct sdl_state {
	dynamic_library_t *library;
	struct sdl_api api;
	struct audio_backend_callbacks callbacks;
	SDL_AudioDeviceID device;
	bool owns_audio_subsystem;
	uint32_t latency;
};

#define LOAD_SDL(api, library, member, name) do { \
	void *st_audio_symbol = dynamic_library_symbol((library), (name)); \
	if (st_audio_symbol == NULL) { \
		(void)dynamic_library_close((library)); \
		return IO_ERROR_NOT_SUPPORTED; \
	} \
	(api)->member = (audio_sdl_ ## member ## _fn)st_audio_symbol; \
} while (0)

static int
load_sdl(dynamic_library_t **result, struct sdl_api *api)
{
	static const char *const names[] = { "SDL2", "SDL2-2.0" };
	dynamic_library_t *library = NULL;
	int status = dynamic_library_open(&library, names, 2, 0,
	    DYNAMIC_LIBRARY_LOCAL);

	if (status != 0)
		return status;
	LOAD_SDL(api, library, was_init, "SDL_WasInit");
	LOAD_SDL(api, library, init_subsystem, "SDL_InitSubSystem");
	LOAD_SDL(api, library, quit_subsystem, "SDL_QuitSubSystem");
	LOAD_SDL(api, library, open_audio_device, "SDL_OpenAudioDevice");
	LOAD_SDL(api, library, close_audio_device, "SDL_CloseAudioDevice");
	LOAD_SDL(api, library, pause_audio_device, "SDL_PauseAudioDevice");
	*result = library;
	return 0;
}

static bool
sdl_available(void)
{
	struct sdl_api api;
	dynamic_library_t *library = NULL;
	bool result = load_sdl(&library, &api) == 0;

	(void)dynamic_library_close(library);
	return result;
}

static void SDLCALL
sdl_callback(void *user_data, Uint8 *bytes, int byte_count)
{
	struct sdl_state *state = user_data;

	if (byte_count <= 0)
		return;
	(void)state->callbacks.pull(state->callbacks.context,
	    (int16_t *)(void *)bytes, (size_t)byte_count / AUDIO_FRAME_SIZE);
}

static int
sdl_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct sdl_state *state = calloc(1, sizeof(*state));
	SDL_AudioSpec requested;
	SDL_AudioSpec obtained;
	int status;

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->callbacks = *callbacks;
	status = load_sdl(&state->library, &state->api);
	if (status != 0)
		goto fail;
	if ((state->api.was_init(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
		if (state->api.init_subsystem(SDL_INIT_AUDIO) != 0) {
			status = IO_ERROR_IO;
			goto fail;
		}
		state->owns_audio_subsystem = true;
	}
	memset(&requested, 0, sizeof(requested));
	memset(&obtained, 0, sizeof(obtained));
	requested.freq = AUDIO_SAMPLE_RATE;
	requested.format = AUDIO_S16SYS;
	requested.channels = AUDIO_CHANNELS;
	requested.samples = 512;
	requested.callback = sdl_callback;
	requested.userdata = state;
	state->device = state->api.open_audio_device(NULL, 0, &requested,
	    &obtained, 0);
	if (state->device == 0) {
		status = IO_ERROR_IO;
		goto fail_subsystem;
	}
	state->latency = (uint32_t)((uint64_t)obtained.samples * 1000
	    / AUDIO_SAMPLE_RATE);
	state->api.pause_audio_device(state->device, 0);
	*result = state;
	return 0;

fail_subsystem:
	if (state->owns_audio_subsystem)
		state->api.quit_subsystem(SDL_INIT_AUDIO);
fail:
	(void)dynamic_library_close(state->library);
	free(state);
	return status;
}

static void
sdl_close(void *opaque)
{
	struct sdl_state *state = opaque;

	if (state == NULL)
		return;
	state->api.close_audio_device(state->device);
	if (state->owns_audio_subsystem)
		state->api.quit_subsystem(SDL_INIT_AUDIO);
	(void)dynamic_library_close(state->library);
	free(state);
}

static uint32_t
sdl_latency_ms(void *opaque)
{
	return ((struct sdl_state *)opaque)->latency;
}

const struct audio_backend_descriptor audio_backend_sdl = {
	AUDIO_BACKEND_SDL,
	sdl_available,
	sdl_open,
	sdl_close,
	NULL,
	sdl_latency_ms,
};
