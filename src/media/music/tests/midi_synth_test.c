/* Manually selected internal-synth smoke test; never opens a device. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "st_threads.h"
#include "st_time.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
/* Consume the actual player/mixer path without opening an audio device.
 * The player takes ownership of the supplied sink, including on errors. */
static int capture(struct st_midi_sink *sink, double *rms, int *peak)
{
	st_audio_runtime_t *runtime;
	int status = st_audio_runtime_create_pull(&runtime);
	if (status != 0) { st_midi_sink_close(sink); return status; }
	st_music_t *music; audio_stream_t *stream = NULL;
	status = st_music_create_with_sink(&music, runtime, &stream, sink);
	if (status != 0) { st_midi_sink_close(sink); st_audio_runtime_destroy(runtime); return status; }
	struct st_music_note *note = malloc(sizeof(*note));
	if (note == NULL) status = IO_ERROR_OUT_OF_MEMORY;
	else {
		*note = (struct st_music_note){33, 2U * AUDIO_SAMPLE_RATE, 0};
		status = st_music_queue_notes(music, note, 1);
		if (status != 0) free(note);
	}
	double energy = 0; size_t samples = 0;
	*peak = 0;
	uint64_t start;
	if (status == 0) status = st_time_monotonic_ms(&start);
	for (size_t at = 0; status == 0 && at < 2U * AUDIO_SAMPLE_RATE;) {
		size_t count = 2U * AUDIO_SAMPLE_RATE - at;
		if (count > 1024) count = 1024;
		while (audio_stream_buffered_frames(st_music_pcm_stream(music)) < count) {
			uint64_t now;
			if (st_time_monotonic_ms(&now) != 0 || now - start > 5000 || !st_music_busy(music)) {
				status = IO_ERROR_IO; break;
			}
			const struct timespec pause = {0, 2000000}; st_thread_sleep(&pause, NULL);
		}
		if (status != 0) break;
		int16_t frames[1024 * AUDIO_CHANNELS];
		status = st_audio_runtime_pull(runtime, frames, count);
		if (status != 0) break;
		for (size_t i = 0; i < count * AUDIO_CHANNELS; ++i) {
			if (abs(frames[i]) > *peak) *peak = abs(frames[i]);
			size_t position = at + i / AUDIO_CHANNELS;
			if (position >= AUDIO_SAMPLE_RATE / 2 && position < AUDIO_SAMPLE_RATE * 3U / 2) {
				energy += (double)frames[i] * frames[i]; ++samples;
			}
		}
		at += count;
	}
	if (status == 0) status = st_music_wait(music);
	if (status == 0) *rms = sqrt(energy / samples);
	st_music_destroy(music); st_audio_runtime_destroy(runtime);
	return status;
}
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
	if (levels) {
		float gain = sink.pcm_gain_db;
		double synth_rms, classic_rms; int synth_peak, classic_peak;
		status = capture(&sink, &synth_rms, &synth_peak);
		struct st_midi_sink classic = {0};
		if (status == 0) status = capture(&classic, &classic_rms, &classic_peak);
		if (status == 0 && synth_rms > 0 && classic_rms > 0) {
			printf("source gain %.3f dB; mixed ANSI synth RMS %.3f, peak %d; classic RMS %.3f, peak %d; difference %+.3f dB\n",
			    gain, synth_rms, synth_peak, classic_rms, classic_peak, 20.0 * log10(synth_rms / classic_rms));
			return 0;
		}
		return 1;
	}
	if (status == 0) status = st_midi_sink_ansi(&sink);
	const uint8_t note[] = {0x90, 69, 100};
	if (status == 0) status = st_midi_sink_send(&sink, note, sizeof(note));
	bool nonzero = false;
	for (int block = 0; status == 0 && block < 20; ++block) {
		int16_t frames[1024 * AUDIO_CHANNELS];
		status = sink.render(sink.state, frames, 1024);
		if (status != 0) break;
		for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) nonzero |= frames[i] != 0;
	}
	st_midi_sink_release(&sink, true); st_midi_sink_close(&sink);
	return status == 0 && nonzero ? 0 : 1;
}
