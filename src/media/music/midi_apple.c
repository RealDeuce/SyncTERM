/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "music_config.h"
#if ST_MIDI_LINK_APPLE
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <math.h>
#include <stdlib.h>

struct apple_state { AudioUnit unit; AudioBufferList *buffers; uint64_t frame; float left[1024], right[1024]; };
static int send_message(void *context, const uint8_t *b, size_t size)
{
	struct apple_state *a = context;
	if (size > UINT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	if (b[0] == 0xf0) { (void)MusicDeviceSysEx(a->unit, b, (UInt32)size); return 0; }
	if (b[0] >= 0xf0) return 0;
	return MusicDeviceMIDIEvent(a->unit, b[0], size > 1 ? b[1] : 0, size > 2 ? b[2] : 0, 0) == noErr ? 0 : IO_ERROR_IO;
}
static int16_t sample(float value)
{
	if (!isfinite(value)) return 0;
	double scaled = value * 32768.0;
	return scaled >= 32767 ? 32767 : scaled <= -32768 ? -32768 : (int16_t)scaled;
}
static int render(void *context, int16_t *frames, size_t count)
{
	struct apple_state *a = context;
	if (count > 1024) return IO_ERROR_OUT_OF_RANGE;
	a->buffers->mNumberBuffers = 2;
	a->buffers->mBuffers[0] = (AudioBuffer){1, (UInt32)(count * sizeof(float)), a->left};
	a->buffers->mBuffers[1] = (AudioBuffer){1, (UInt32)(count * sizeof(float)), a->right};
	AudioTimeStamp time = {0}; time.mFlags = kAudioTimeStampSampleTimeValid; time.mSampleTime = (Float64)a->frame;
	AudioUnitRenderActionFlags flags = 0;
	if (AudioUnitRender(a->unit, &flags, &time, 0, (UInt32)count, a->buffers) != noErr) return IO_ERROR_IO;
	a->frame += count;
	for (size_t i = 0; i < count; ++i) { frames[i * 2] = sample(a->left[i]); frames[i * 2 + 1] = sample(a->right[i]); }
	return 0;
}
static void close_synth(void *context)
{
	struct apple_state *a = context;
	if (a->unit != NULL) { (void)AudioUnitUninitialize(a->unit); (void)AudioComponentInstanceDispose(a->unit); }
	free(a->buffers); free(a);
}
int st_midi_apple_open(struct st_midi_sink *sink)
{
	const AudioComponentDescription description = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth, kAudioUnitManufacturer_Apple, 0, 0};
	AudioComponent component = AudioComponentFindNext(NULL, &description);
	if (component == NULL) return IO_ERROR_NOT_FOUND;
	struct apple_state *a = calloc(1, sizeof(*a));
	if (a == NULL) return IO_ERROR_OUT_OF_MEMORY;
	a->buffers = calloc(1, offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer));
	if (a->buffers == NULL) { close_synth(a); return IO_ERROR_OUT_OF_MEMORY; }
	if (AudioComponentInstanceNew(component, &a->unit) != noErr) { close_synth(a); return IO_ERROR_IO; }
	const AudioStreamBasicDescription format = {
		.mSampleRate = AUDIO_SAMPLE_RATE, .mFormatID = kAudioFormatLinearPCM,
		.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagsNativeEndian,
		.mBytesPerPacket = sizeof(float), .mFramesPerPacket = 1,
		.mBytesPerFrame = sizeof(float), .mChannelsPerFrame = 2, .mBitsPerChannel = 32,
	};
	UInt32 maximum = 1024;
	if (AudioUnitSetProperty(a->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)) != noErr
	    || AudioUnitSetProperty(a->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, sizeof(maximum)) != noErr
	    || AudioUnitInitialize(a->unit) != noErr) { close_synth(a); return IO_ERROR_IO; }
	sink->state = a; sink->send = send_message; sink->render = render; sink->close = close_synth;
	return 0;
}
#else
int st_midi_apple_open(struct st_midi_sink *sink) { (void)sink; return IO_ERROR_NOT_SUPPORTED; }
#endif
