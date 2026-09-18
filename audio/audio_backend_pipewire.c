/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "dynamic_library.h"
#include "io_error.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw-utils.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void (*pw_init_fn)(int *, char ***);
typedef void (*pw_deinit_fn)(void);
typedef struct pw_thread_loop *(*pw_thread_loop_new_fn)(const char *,
	const struct spa_dict *);
typedef void (*pw_thread_loop_destroy_fn)(struct pw_thread_loop *);
typedef struct pw_loop *(*pw_thread_loop_get_loop_fn)(struct pw_thread_loop *);
typedef int (*pw_thread_loop_start_fn)(struct pw_thread_loop *);
typedef void (*pw_thread_loop_stop_fn)(struct pw_thread_loop *);
typedef struct pw_stream *(*pw_stream_new_simple_fn)(struct pw_loop *,
	const char *, struct pw_properties *, const struct pw_stream_events *,
	void *);
typedef void (*pw_stream_destroy_fn)(struct pw_stream *);
typedef int (*pw_stream_connect_fn)(struct pw_stream *, enum pw_direction,
	uint32_t, enum pw_stream_flags, const struct spa_pod **, uint32_t);
typedef int (*pw_stream_disconnect_fn)(struct pw_stream *);
typedef struct pw_buffer *(*pw_stream_dequeue_buffer_fn)(struct pw_stream *);
typedef int (*pw_stream_queue_buffer_fn)(struct pw_stream *,
	struct pw_buffer *);

struct pipewire_api {
	void (*init)(int *, char ***);
	void (*deinit)(void);
	struct pw_thread_loop *(*thread_loop_new)(const char *,
	    const struct spa_dict *);
	void (*thread_loop_destroy)(struct pw_thread_loop *);
	struct pw_loop *(*thread_loop_get_loop)(struct pw_thread_loop *);
	int (*thread_loop_start)(struct pw_thread_loop *);
	void (*thread_loop_stop)(struct pw_thread_loop *);
	struct pw_stream *(*stream_new_simple)(struct pw_loop *, const char *,
	    struct pw_properties *, const struct pw_stream_events *, void *);
	void (*stream_destroy)(struct pw_stream *);
	int (*stream_connect)(struct pw_stream *, enum pw_direction, uint32_t,
	    enum pw_stream_flags, const struct spa_pod **, uint32_t);
	int (*stream_disconnect)(struct pw_stream *);
	struct pw_buffer *(*stream_dequeue_buffer)(struct pw_stream *);
	int (*stream_queue_buffer)(struct pw_stream *, struct pw_buffer *);
};

struct pipewire_state {
	dynamic_library_t *library;
	struct pipewire_api api;
	struct audio_backend_callbacks callbacks;
	struct pw_thread_loop *loop;
	struct pw_stream *stream;
	bool initialized;
	bool loop_started;
};

#define LOAD_PW(api, library, member, name) do { \
	void *st_audio_symbol = dynamic_library_symbol((library), (name)); \
	if (st_audio_symbol == NULL) { \
		(void)dynamic_library_close((library)); \
		return IO_ERROR_NOT_SUPPORTED; \
	} \
	(api)->member = (pw_ ## member ## _fn)st_audio_symbol; \
} while (0)

static int
load_pipewire(dynamic_library_t **result, struct pipewire_api *api)
{
	static const char *const names[] = { "pipewire-0.3" };
	dynamic_library_t *library = NULL;
	int status = dynamic_library_open(&library, names, 1, 0,
	    DYNAMIC_LIBRARY_LOCAL);

	if (status != 0)
		return status;
	LOAD_PW(api, library, init, "pw_init");
	LOAD_PW(api, library, deinit, "pw_deinit");
	LOAD_PW(api, library, thread_loop_new, "pw_thread_loop_new");
	LOAD_PW(api, library, thread_loop_destroy, "pw_thread_loop_destroy");
	LOAD_PW(api, library, thread_loop_get_loop, "pw_thread_loop_get_loop");
	LOAD_PW(api, library, thread_loop_start, "pw_thread_loop_start");
	LOAD_PW(api, library, thread_loop_stop, "pw_thread_loop_stop");
	LOAD_PW(api, library, stream_new_simple, "pw_stream_new_simple");
	LOAD_PW(api, library, stream_destroy, "pw_stream_destroy");
	LOAD_PW(api, library, stream_connect, "pw_stream_connect");
	LOAD_PW(api, library, stream_disconnect, "pw_stream_disconnect");
	LOAD_PW(api, library, stream_dequeue_buffer,
	    "pw_stream_dequeue_buffer");
	LOAD_PW(api, library, stream_queue_buffer, "pw_stream_queue_buffer");
	*result = library;
	return 0;
}

static bool
pipewire_available(void)
{
	struct pipewire_api api;
	dynamic_library_t *library = NULL;
	bool result = load_pipewire(&library, &api) == 0;

	(void)dynamic_library_close(library);
	return result;
}

static void
pipewire_state_changed(void *data, enum pw_stream_state old,
    enum pw_stream_state state, const char *error)
{
	struct pipewire_state *backend = data;

	(void)old;
	(void)error;
	if (state == PW_STREAM_STATE_ERROR)
		backend->callbacks.failed(backend->callbacks.context);
}

static void
pipewire_process(void *data)
{
	struct pipewire_state *state = data;
	struct pw_buffer *pw_buffer;
	struct spa_buffer *buffer;
	struct spa_data *destination;
	size_t frames;

	pw_buffer = state->api.stream_dequeue_buffer(state->stream);
	if (pw_buffer == NULL)
		return;
	buffer = pw_buffer->buffer;
	if (buffer->n_datas == 0 || buffer->datas[0].data == NULL
	    || buffer->datas[0].chunk == NULL)
		goto queue;
	destination = &buffer->datas[0];
	frames = destination->maxsize / AUDIO_FRAME_SIZE;
	if (pw_buffer->requested > 0 && pw_buffer->requested < frames)
		frames = (size_t)pw_buffer->requested;
	(void)state->callbacks.pull(state->callbacks.context,
	    destination->data, frames);
	destination->chunk->offset = 0;
	destination->chunk->stride = AUDIO_FRAME_SIZE;
	destination->chunk->size = (uint32_t)(frames * AUDIO_FRAME_SIZE);
	pw_buffer->size = frames;
queue:
	if (state->api.stream_queue_buffer(state->stream, pw_buffer) < 0)
		state->callbacks.failed(state->callbacks.context);
}

static const struct pw_stream_events pipewire_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = pipewire_state_changed,
	.process = pipewire_process,
};

static int
pipewire_open(void **result,
    const struct audio_backend_callbacks *callbacks)
{
	struct pipewire_state *state = calloc(1, sizeof(*state));
	uint8_t pod_buffer[1024];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod_buffer,
	    sizeof(pod_buffer));
	struct spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(
	    .format = SPA_AUDIO_FORMAT_S16,
	    .rate = AUDIO_SAMPLE_RATE,
	    .channels = AUDIO_CHANNELS,
	    .position = { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR });
	const struct spa_pod *parameters[1];
	int status;

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->callbacks = *callbacks;
	status = load_pipewire(&state->library, &state->api);
	if (status != 0)
		goto fail;
	state->api.init(NULL, NULL);
	state->initialized = true;
	state->loop = state->api.thread_loop_new("SyncTERM audio", NULL);
	if (state->loop == NULL) {
		status = IO_ERROR_IO;
		goto fail;
	}
	state->stream = state->api.stream_new_simple(
	    state->api.thread_loop_get_loop(state->loop), "SyncTERM", NULL,
	    &pipewire_events, state);
	if (state->stream == NULL) {
		status = IO_ERROR_IO;
		goto fail;
	}
	parameters[0] = spa_format_audio_raw_build(&builder,
	    SPA_PARAM_EnumFormat, &info);
	if (state->api.stream_connect(state->stream, PW_DIRECTION_OUTPUT,
	    PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
	    | PW_STREAM_FLAG_RT_PROCESS, parameters, 1) < 0
	    || state->api.thread_loop_start(state->loop) < 0) {
		status = IO_ERROR_IO;
		goto fail;
	}
	state->loop_started = true;
	*result = state;
	return 0;

fail:
	if (state->stream != NULL) {
		(void)state->api.stream_disconnect(state->stream);
		state->api.stream_destroy(state->stream);
	}
	if (state->loop != NULL)
		state->api.thread_loop_destroy(state->loop);
	if (state->initialized)
		state->api.deinit();
	(void)dynamic_library_close(state->library);
	free(state);
	return status;
}

static void
pipewire_close(void *opaque)
{
	struct pipewire_state *state = opaque;

	if (state == NULL)
		return;
	if (state->loop_started)
		state->api.thread_loop_stop(state->loop);
	(void)state->api.stream_disconnect(state->stream);
	state->api.stream_destroy(state->stream);
	state->api.thread_loop_destroy(state->loop);
	state->api.deinit();
	(void)dynamic_library_close(state->library);
	free(state);
}

static uint32_t
pipewire_latency_ms(void *opaque)
{
	(void)opaque;
	return 24;
}

const struct audio_backend_descriptor audio_backend_pipewire = {
	AUDIO_BACKEND_PIPEWIRE,
	pipewire_available,
	pipewire_open,
	pipewire_close,
	NULL,
	pipewire_latency_ms,
};
