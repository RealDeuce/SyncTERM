/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "audio_internal.h"

#include "io_error.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdint.h>
#include <stdlib.h>

#define CORE_AUDIO_BUFFER_COUNT 3
#define CORE_AUDIO_BUFFER_FRAMES 512

struct core_audio_state {
	struct audio_backend_callbacks callbacks;
	AudioQueueRef queue;
	AudioQueueBufferRef buffers[CORE_AUDIO_BUFFER_COUNT];
};

static bool
core_audio_available(void)
{
	return true;
}

static void
core_audio_callback(void *data, AudioQueueRef queue,
    AudioQueueBufferRef buffer)
{
	struct core_audio_state *state = data;

	(void)state->callbacks.pull(state->callbacks.context,
	    buffer->mAudioData, CORE_AUDIO_BUFFER_FRAMES);
	buffer->mAudioDataByteSize = CORE_AUDIO_BUFFER_FRAMES * AUDIO_FRAME_SIZE;
	if (AudioQueueEnqueueBuffer(queue, buffer, 0, NULL) != noErr)
		state->callbacks.failed(state->callbacks.context);
}

static int
core_audio_open(void **result,
    const struct audio_backend_callbacks *callbacks)
{
	struct core_audio_state *state = calloc(1, sizeof(*state));
	AudioStreamBasicDescription format = { 0 };
	size_t i;

	if (state == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	state->callbacks = *callbacks;
	format.mSampleRate = AUDIO_SAMPLE_RATE;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger
	    | kLinearPCMFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
	format.mBytesPerPacket = AUDIO_FRAME_SIZE;
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = AUDIO_FRAME_SIZE;
	format.mChannelsPerFrame = AUDIO_CHANNELS;
	format.mBitsPerChannel = 16;
	if (AudioQueueNewOutput(&format, core_audio_callback, state, NULL, NULL,
	    0, &state->queue) != noErr)
		goto fail;
	for (i = 0; i < CORE_AUDIO_BUFFER_COUNT; i++) {
		if (AudioQueueAllocateBuffer(state->queue,
		    CORE_AUDIO_BUFFER_FRAMES * AUDIO_FRAME_SIZE,
		    &state->buffers[i]) != noErr)
			goto fail;
		(void)state->callbacks.pull(state->callbacks.context,
		    state->buffers[i]->mAudioData, CORE_AUDIO_BUFFER_FRAMES);
		state->buffers[i]->mAudioDataByteSize =
		    CORE_AUDIO_BUFFER_FRAMES * AUDIO_FRAME_SIZE;
		if (AudioQueueEnqueueBuffer(state->queue, state->buffers[i], 0,
		    NULL) != noErr)
			goto fail;
	}
	if (AudioQueueStart(state->queue, NULL) != noErr)
		goto fail;
	*result = state;
	return 0;

fail:
	if (state->queue != NULL)
		(void)AudioQueueDispose(state->queue, true);
	free(state);
	return IO_ERROR_IO;
}

static void
core_audio_close(void *opaque)
{
	struct core_audio_state *state = opaque;

	if (state == NULL)
		return;
	(void)AudioQueueStop(state->queue, true);
	(void)AudioQueueDispose(state->queue, true);
	free(state);
}

static uint32_t
core_audio_latency_ms(void *opaque)
{
	(void)opaque;
	return CORE_AUDIO_BUFFER_COUNT * CORE_AUDIO_BUFFER_FRAMES * 1000
	    / AUDIO_SAMPLE_RATE;
}

const struct audio_backend_descriptor audio_backend_core_audio = {
	AUDIO_BACKEND_CORE_AUDIO,
	core_audio_available,
	core_audio_open,
	core_audio_close,
	NULL,
	core_audio_latency_ms,
};
