/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define COBJMACROS
#include <initguid.h>
#include "audio_internal.h"

#include "io_error.h"
#include "st_threads.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

struct wasapi_state {
	struct audio_backend_callbacks callbacks;
	st_mutex_t mutex;
	st_condition_t condition;
	st_thread_t thread;
	HANDLE stop_event;
	HANDLE ready_event;
	int initialization_status;
	bool initialized;
	UINT32 buffer_frames;
};

static bool
wasapi_available(void)
{
	return true;
}

static void
wasapi_publish_initialization(struct wasapi_state *state, int status)
{
	(void)st_mutex_lock(&state->mutex);
	state->initialization_status = status;
	state->initialized = true;
	(void)st_condition_signal(&state->condition);
	(void)st_mutex_unlock(&state->mutex);
}

static int
wasapi_thread(void *argument)
{
	struct wasapi_state *state = argument;
	IMMDeviceEnumerator *enumerator = NULL;
	IMMDevice *device = NULL;
	IAudioClient *client = NULL;
	IAudioRenderClient *render = NULL;
	WAVEFORMATEX format = { 0 };
	HANDLE task = NULL;
	DWORD task_index = 0;
	bool com_initialized = false;
	bool published = false;
	bool started = false;
	HRESULT result;

	result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(result))
		goto fail;
	com_initialized = true;
	result = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL,
	    CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
	    (void **)&enumerator);
	if (FAILED(result))
		goto fail;
	result = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator,
	    eRender, eConsole, &device);
	if (FAILED(result))
		goto fail;
	result = IMMDevice_Activate(device, &IID_IAudioClient,
	    CLSCTX_INPROC_SERVER, NULL, (void **)&client);
	if (FAILED(result))
		goto fail;
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = (WORD)AUDIO_CHANNELS;
	format.nSamplesPerSec = (DWORD)AUDIO_SAMPLE_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = (WORD)AUDIO_FRAME_SIZE;
	format.nAvgBytesPerSec = (DWORD)(AUDIO_SAMPLE_RATE * AUDIO_FRAME_SIZE);
	result = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
	    AUDCLNT_STREAMFLAGS_EVENTCALLBACK
	    | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
	    | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
	    1000000, 0, &format, NULL);
	if (FAILED(result))
		goto fail;
	result = IAudioClient_SetEventHandle(client, state->ready_event);
	if (FAILED(result))
		goto fail;
	result = IAudioClient_GetBufferSize(client, &state->buffer_frames);
	if (FAILED(result))
		goto fail;
	result = IAudioClient_GetService(client, &IID_IAudioRenderClient,
	    (void **)&render);
	if (FAILED(result))
		goto fail;
	result = IAudioClient_Start(client);
	if (FAILED(result))
		goto fail;
	started = true;
	task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
	wasapi_publish_initialization(state, 0);
	published = true;
	for (;;) {
		HANDLE events[2] = { state->stop_event, state->ready_event };
		DWORD wait_result = WaitForMultipleObjects(2, events, FALSE,
		    INFINITE);
		UINT32 padding;
		UINT32 frames;
		BYTE *bytes;

		if (wait_result == WAIT_OBJECT_0)
			break;
		if (wait_result != WAIT_OBJECT_0 + 1
		    || FAILED(IAudioClient_GetCurrentPadding(client, &padding)))
			goto runtime_fail;
		frames = state->buffer_frames - padding;
		if (frames == 0)
			continue;
		if (FAILED(IAudioRenderClient_GetBuffer(render, frames, &bytes)))
			goto runtime_fail;
		(void)state->callbacks.pull(state->callbacks.context,
		    (int16_t *)(void *)bytes, frames);
		if (FAILED(IAudioRenderClient_ReleaseBuffer(render, frames, 0)))
			goto runtime_fail;
	}
	goto done;

runtime_fail:
	state->callbacks.failed(state->callbacks.context);
	goto done;
fail:
	wasapi_publish_initialization(state, IO_ERROR_IO);
	published = true;
done:
	if (!published)
		wasapi_publish_initialization(state, IO_ERROR_IO);
	if (task != NULL)
		(void)AvRevertMmThreadCharacteristics(task);
	if (started)
		(void)IAudioClient_Stop(client);
	if (render != NULL)
		IAudioRenderClient_Release(render);
	if (client != NULL)
		IAudioClient_Release(client);
	if (device != NULL)
		IMMDevice_Release(device);
	if (enumerator != NULL)
		IMMDeviceEnumerator_Release(enumerator);
	if (com_initialized)
		CoUninitialize();
	return 0;
}

static int
wasapi_open(void **result, const struct audio_backend_callbacks *callbacks)
{
	struct wasapi_state *state = calloc(1, sizeof(*state));
	int status;

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->callbacks = *callbacks;
	if (st_mutex_init(&state->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) {
		free(state);
		return IO_ERROR_IO;
	}
	if (st_condition_init(&state->condition) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&state->mutex);
		free(state);
		return IO_ERROR_IO;
	}
	state->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	state->ready_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (state->stop_event == NULL || state->ready_event == NULL
	    || st_thread_create(&state->thread, wasapi_thread, state)
	    != ST_THREAD_SUCCESS) {
		if (state->stop_event != NULL)
			(void)CloseHandle(state->stop_event);
		if (state->ready_event != NULL)
			(void)CloseHandle(state->ready_event);
		st_condition_destroy(&state->condition);
		st_mutex_destroy(&state->mutex);
		free(state);
		return IO_ERROR_IO;
	}
	(void)st_mutex_lock(&state->mutex);
	while (!state->initialized)
		(void)st_condition_wait(&state->condition, &state->mutex);
	status = state->initialization_status;
	(void)st_mutex_unlock(&state->mutex);
	if (status != 0) {
		(void)st_thread_join(state->thread, NULL);
		(void)CloseHandle(state->stop_event);
		(void)CloseHandle(state->ready_event);
		st_condition_destroy(&state->condition);
		st_mutex_destroy(&state->mutex);
		free(state);
		return status;
	}
	*result = state;
	return 0;
}

static void
wasapi_close(void *opaque)
{
	struct wasapi_state *state = opaque;

	if (state == NULL)
		return;
	(void)SetEvent(state->stop_event);
	(void)st_thread_join(state->thread, NULL);
	(void)CloseHandle(state->ready_event);
	(void)CloseHandle(state->stop_event);
	st_condition_destroy(&state->condition);
	st_mutex_destroy(&state->mutex);
	free(state);
}

static uint32_t
wasapi_latency_ms(void *opaque)
{
	struct wasapi_state *state = opaque;

	return (uint32_t)((uint64_t)state->buffer_frames * 1000
	    / AUDIO_SAMPLE_RATE);
}

const struct audio_backend_descriptor audio_backend_wasapi = {
	AUDIO_BACKEND_WASAPI,
	wasapi_available,
	wasapi_open,
	wasapi_close,
	NULL,
	wasapi_latency_ms,
};
