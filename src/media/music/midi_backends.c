/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "music_config.h"
#include <stdlib.h>
#include <string.h>

uint32_t st_midi_compiled_backends(void)
{
	return (ST_MIDI_LINK_ALSA ? ST_MIDI_ALSA : 0)
	    | (ST_MIDI_LINK_FLUIDSYNTH ? ST_MIDI_FLUIDSYNTH : 0)
	    | (ST_MIDI_LINK_ADLMIDI ? ST_MIDI_ADLMIDI : 0)
	    | (ST_MIDI_LINK_WINMM ? ST_MIDI_WINMM : 0)
	    | (ST_MIDI_LINK_APPLE ? ST_MIDI_APPLE : 0);
}
enum st_midi_linkage st_midi_backend_linkage(enum st_midi_backend backend)
{
	switch (backend) {
	case ST_MIDI_ALSA: return ST_MIDI_LINK_ALSA;
	case ST_MIDI_FLUIDSYNTH: return ST_MIDI_LINK_FLUIDSYNTH;
	case ST_MIDI_ADLMIDI: return ST_MIDI_LINK_ADLMIDI;
	case ST_MIDI_WINMM: return ST_MIDI_LINK_WINMM;
	case ST_MIDI_APPLE: return ST_MIDI_LINK_APPLE;
	default: return ST_MIDI_NOT_BUILT;
	}
}
int st_midi_sink_open(struct st_midi_sink *sink, enum st_midi_backend kind,
	bool (*cancelled)(void *), void *context)
{
	int status;
	memset(sink, 0, sizeof(*sink)); sink->kind = kind;
	memset(sink->volume, 100, sizeof(sink->volume));
	memset(sink->pan, 64, sizeof(sink->pan));
	sink->cancelled = cancelled; sink->cancel_context = context;
	switch (kind) {
	case ST_MIDI_ALSA: status = st_midi_alsa_open(sink); break;
	case ST_MIDI_FLUIDSYNTH: status = st_midi_fluid_open(sink); break;
	case ST_MIDI_ADLMIDI: status = st_midi_adl_open(sink); break;
	case ST_MIDI_WINMM: status = st_midi_winmm_open(sink); break;
	case ST_MIDI_APPLE: status = st_midi_apple_open(sink); break;
	default: status = IO_ERROR_NOT_SUPPORTED;
	}
	/* FluidSynth calibrates before restoring its configured live gain. */
	if (status == 0 && kind != ST_MIDI_FLUIDSYNTH) status = st_midi_sink_calibrate(sink);
	if (status != 0) st_midi_sink_close(sink);
	return status;
}
int st_midi_sink_send(struct st_midi_sink *sink, const uint8_t *data, size_t size)
{
	return st_midi_decode(sink, data, size, sink->send, sink->state);
}
int st_midi_sink_gain(struct st_midi_sink *sink, float left, float right)
{
	if (sink->render != NULL) return 0;
	if (sink->gain != NULL) return sink->gain(sink->state, left, right);
	return st_midi_gain_controllers(sink, left, right, sink->send, sink->state);
}
void st_midi_sink_release(struct st_midi_sink *sink, bool immediate)
{
	if (sink->send == NULL) return;
	/* A selected MIDI destination is used as a normal file-player output,
	 * including all sixteen channels; no channel reservation UI is implied. */
	for (unsigned ch = 0; ch < 16; ++ch) {
		uint8_t message[3] = {(uint8_t)(0xb0 | ch), 64, 0};
		(void)sink->send(sink->state, message, sizeof(message));
		message[1] = 66;
		(void)sink->send(sink->state, message, sizeof(message));
		message[1] = 69;
		(void)sink->send(sink->state, message, sizeof(message));
		message[1] = immediate ? 120 : 123;
		(void)sink->send(sink->state, message, sizeof(message));
	}
	sink->status = 0; sink->used = 0; sink->sysex_size = 0;
	free(sink->sysex); sink->sysex = NULL; sink->sysex_capacity = 0;
}
int st_midi_sink_start(struct st_midi_sink *sink)
{
	st_midi_sink_release(sink, true);
	if (sink->set_ansi != NULL) {
		int status = sink->set_ansi(sink->state, false);
		if (status != 0) return status;
	}
	for (unsigned ch = 0; ch < 16; ++ch) {
		const uint8_t reset[] = {(uint8_t)(0xb0 | ch), 121, 0,
		    (uint8_t)(0xb0 | ch), 0, 0, (uint8_t)(0xb0 | ch), 32, 0,
		    (uint8_t)(0xc0 | ch), 0, (uint8_t)(0xb0 | ch), 7, 100,
		    (uint8_t)(0xb0 | ch), 10, 64};
		int status = st_midi_sink_send(sink, reset, sizeof(reset));
		if (status != 0) return status;
	}
	return 0;
}
int st_midi_sink_ansi(struct st_midi_sink *sink)
{
	/* Use a sustained sawtooth voice for short ANSI notes.
	 * Select the same bank and voice for playback and source calibration. */
	const uint8_t synth[] = {0xb0, 0, 0, 0xb0, 32, 0, 0xc0, 81};
	int status = st_midi_sink_send(sink, synth, sizeof(synth));
	if (status == 0 && sink->set_ansi != NULL) status = sink->set_ansi(sink->state, true);
	return status;
}
void st_midi_sink_close(struct st_midi_sink *sink)
{
	if (sink->state != NULL && sink->close != NULL) sink->close(sink->state);
	free(sink->sysex);
	memset(sink, 0, sizeof(*sink));
}
