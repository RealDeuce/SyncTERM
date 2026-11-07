/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "music_config.h"
#if ST_MIDI_LINK_WINMM
#include "st_threads.h"
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct win_state { HMIDIOUT output; atomic_bool done; struct st_midi_sink *sink; };
static void CALLBACK completed(HMIDIOUT output, UINT message, DWORD_PTR instance, DWORD_PTR one, DWORD_PTR two)
{
	(void)output; (void)one; (void)two;
	if (message == MOM_DONE) atomic_store_explicit(&((struct win_state *)instance)->done, true, memory_order_release);
}
static int send_message(void *context, const uint8_t *bytes, size_t size)
{
	struct win_state *w = context;
	if (size == 0) return 0;
	if (bytes[0] != 0xf0) {
		if (size > 3) return IO_ERROR_INVALID_FORMAT;
		DWORD message = bytes[0];
		for (size_t i = 1; i < size; ++i) message |= (DWORD)bytes[i] << (i * 8);
		return midiOutShortMsg(w->output, message) == MMSYSERR_NOERROR ? 0 : IO_ERROR_IO;
	}
	if (size > MAXDWORD) return IO_ERROR_OUT_OF_RANGE;
	MIDIHDR header;
	memset(&header, 0, sizeof(header));
	header.lpData = (LPSTR)bytes; header.dwBufferLength = (DWORD)size;
	if (midiOutPrepareHeader(w->output, &header, sizeof(header)) != MMSYSERR_NOERROR) return IO_ERROR_IO;
	atomic_store_explicit(&w->done, false, memory_order_release);
	MMRESULT result = midiOutLongMsg(w->output, &header, sizeof(header));
	bool cancelled = false;
	if (result == MMSYSERR_NOERROR) {
		while (!atomic_load_explicit(&w->done, memory_order_acquire)) {
			if (!cancelled && w->sink->cancelled != NULL && w->sink->cancelled(w->sink->cancel_context)) {
				cancelled = true; (void)midiOutReset(w->output);
			}
			struct timespec pause = {0, 2000000}; (void)st_thread_sleep(&pause, NULL);
		}
	}
	MMRESULT unprepare = midiOutUnprepareHeader(w->output, &header, sizeof(header));
	if (cancelled) return IO_ERROR_CANCELLED;
	return result == MMSYSERR_NOERROR && unprepare == MMSYSERR_NOERROR ? 0 : IO_ERROR_IO;
}
static WORD level(float db)
{
	double value = pow(10.0, db / 20.0) * 65535;
	return value >= 65535 ? 65535 : value <= 0 ? 0 : (WORD)(value + 0.5);
}
static int gain(void *context, float left, float right)
{
	struct win_state *w = context;
	DWORD volume = (DWORD)level(left) | (DWORD)level(right) << 16;
	return midiOutSetVolume(w->output, volume) == MMSYSERR_NOERROR ? 0 : IO_ERROR_IO;
}
static void close_output(void *context)
{
	struct win_state *w = context;
	(void)midiOutReset(w->output); (void)midiOutClose(w->output); free(w);
}
int st_midi_winmm_open(struct st_midi_sink *sink)
{
	struct win_state *w = calloc(1, sizeof(*w));
	if (w == NULL) return IO_ERROR_OUT_OF_MEMORY;
	w->sink = sink; atomic_init(&w->done, true);
	if (midiOutOpen(&w->output, MIDI_MAPPER, (DWORD_PTR)completed, (DWORD_PTR)w, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
		free(w); return IO_ERROR_NOT_FOUND;
	}
	sink->state = w; sink->send = send_message; sink->close = close_output;
	UINT device;
	MIDIOUTCAPSW caps;
	if (midiOutGetID(w->output, &device) == MMSYSERR_NOERROR
	    && midiOutGetDevCapsW(device, &caps, sizeof(caps)) == MMSYSERR_NOERROR
	    && (caps.dwSupport & (MIDICAPS_VOLUME | MIDICAPS_LRVOLUME)) == (MIDICAPS_VOLUME | MIDICAPS_LRVOLUME)) sink->gain = gain;
	return 0;
}
#else
int st_midi_winmm_open(struct st_midi_sink *sink) { (void)sink; return IO_ERROR_NOT_SUPPORTED; }
#endif
