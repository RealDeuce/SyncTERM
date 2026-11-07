/* Manually selected internal-synth smoke test; never opens a device. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	if (argc != 2 && (argc != 3 || strcmp(argv[2], "--levels") != 0)) return 2;
	bool levels = argc == 3;
	enum st_midi_backend bit = strcmp(argv[1], "fluid") == 0 ? ST_MIDI_FLUIDSYNTH
	    : strcmp(argv[1], "adl") == 0 ? ST_MIDI_ADLMIDI : ST_MIDI_APPLE;
	struct st_midi_sink sink;
	int status = st_midi_sink_open(&sink, bit, NULL, NULL);
	if (status != 0) { fprintf(stderr, "synth unavailable: %d\n", status); return 77; }
	if (sink.render == NULL) { st_midi_sink_close(&sink); return 2; }
	if (levels) status = st_midi_sink_start(&sink);
	const uint8_t oboe[] = {0xc0, 68};
	if (levels && status == 0) status = st_midi_sink_send(&sink, oboe, sizeof(oboe));
	const uint8_t note[] = {0x90, 69, 100};
	if (status == 0) status = st_midi_sink_send(&sink, note, sizeof(note));
	bool nonzero = false;
	double energy = 0; size_t samples = 0;
	for (size_t at = 0; status == 0 && at < (levels ? 2U * AUDIO_SAMPLE_RATE : 20U * 1024U); at += 1024) {
		int16_t frames[1024 * AUDIO_CHANNELS];
		status = sink.render(sink.state, frames, 1024);
		if (status != 0) break;
		for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
			nonzero |= frames[i] != 0;
			size_t position = at + i / AUDIO_CHANNELS;
			if (position >= AUDIO_SAMPLE_RATE / 2 && position < AUDIO_SAMPLE_RATE * 3U / 2) {
				energy += (double)frames[i] * frames[i]; ++samples;
			}
		}
	}
	if (levels && samples != 0 && energy > 0) {
		double rms = sqrt(energy / samples);
		printf("source gain %.3f dB; raw oboe RMS %.3f; calibrated RMS %.3f\n",
		    sink.pcm_gain_db, rms, rms * pow(10.0, sink.pcm_gain_db / 20.0));
	}
	st_midi_sink_release(&sink, true); st_midi_sink_close(&sink);
	return status == 0 && nonzero ? 0 : 1;
}
