/* Manually selected internal-synth smoke test; never opens a device. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	if (argc != 2) return 2;
	enum st_midi_backend bit = strcmp(argv[1], "fluid") == 0 ? ST_MIDI_FLUIDSYNTH
	    : strcmp(argv[1], "adl") == 0 ? ST_MIDI_ADLMIDI : ST_MIDI_APPLE;
	struct st_midi_sink sink;
	int status = st_midi_sink_open(&sink, bit, NULL, NULL);
	if (status != 0) { fprintf(stderr, "synth unavailable: %d\n", status); return 77; }
	if (sink.render == NULL) { st_midi_sink_close(&sink); return 2; }
	const uint8_t note[] = {0x90, 69, 100};
	status = st_midi_sink_send(&sink, note, sizeof(note));
	bool nonzero = false;
	for (int block = 0; status == 0 && block < 20; ++block) {
		int16_t frames[1024 * AUDIO_CHANNELS];
		status = sink.render(sink.state, frames, 1024);
		for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) nonzero |= frames[i] != 0;
	}
	st_midi_sink_release(&sink, true); st_midi_sink_close(&sink);
	return status == 0 && nonzero ? 0 : 1;
}
