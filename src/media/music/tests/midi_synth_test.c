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
static int capture(struct st_midi_sink *sink, double *rms, double dc[AUDIO_CHANNELS], int *peak)
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
	/* Independently measure AC power with Welford's central moments. Do not
	 * repeat calibration's sum-of-squares implementation here. */
	double energy[AUDIO_CHANNELS] = {0}; size_t samples[AUDIO_CHANNELS] = {0};
	for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) dc[ch] = 0;
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
				size_t ch = i % AUDIO_CHANNELS;
				double delta = frames[i] - dc[ch];
				dc[ch] += delta / ++samples[ch];
				energy[ch] += delta * (frames[i] - dc[ch]);
			}
		}
		at += count;
	}
	if (status == 0) status = st_music_wait(music);
	if (status == 0) {
		double variance = 0;
		for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) variance += energy[ch] / samples[ch];
		*rms = sqrt(variance / AUDIO_CHANNELS);
	}
	st_music_destroy(music); st_audio_runtime_destroy(runtime);
	return status;
}
/* A steady level alone can hide a plucky preset. Compare the first short
 * ANSI-note window with sustain at representative pitches across N0..N83. */
static int envelope(struct st_midi_sink *sink)
{
	const uint8_t keys[] = {36, 48, 60, 69, 81, 96, 108, 119};
	bool burst = false;
	for (size_t key = 0; key < sizeof(keys) / sizeof(keys[0]); ++key) {
		int status = st_midi_sink_start(sink);
		if (status == 0) status = st_midi_sink_ansi(sink);
		const uint8_t note[] = {0x90, keys[key], 100};
		if (status == 0) status = st_midi_sink_send(sink, note, sizeof(note));
		double early = 0, steady = 0; size_t early_count = 0, steady_count = 0;
		double early_sum[AUDIO_CHANNELS] = {0}, steady_sum[AUDIO_CHANNELS] = {0};
		int16_t frames[1024 * AUDIO_CHANNELS];
		for (size_t at = 0; status == 0 && at < 2U * AUDIO_SAMPLE_RATE;) {
			size_t count = 2U * AUDIO_SAMPLE_RATE - at;
			if (count > 1024) count = 1024;
			status = sink->render(sink->state, frames, count);
			if (status != 0) break;
			for (size_t i = 0; i < count * AUDIO_CHANNELS; ++i) {
				size_t position = at + i / AUDIO_CHANNELS;
				double energy = (double)frames[i] * frames[i];
				if (position >= AUDIO_SAMPLE_RATE / 100 && position < AUDIO_SAMPLE_RATE / 10) {
					early += energy; early_sum[i % AUDIO_CHANNELS] += frames[i]; ++early_count;
				}
				if (position >= AUDIO_SAMPLE_RATE / 2 && position < AUDIO_SAMPLE_RATE * 3U / 2) {
					steady += energy; steady_sum[i % AUDIO_CHANNELS] += frames[i]; ++steady_count;
				}
			}
			at += count;
		}
		st_midi_sink_release(sink, true);
		if (status != 0) return 1;
		for (size_t ch = 0; ch < AUDIO_CHANNELS; ++ch) {
			early -= early_sum[ch] * early_sum[ch] / (early_count / AUDIO_CHANNELS);
			steady -= steady_sum[ch] * steady_sum[ch] / (steady_count / AUDIO_CHANNELS);
		}
		if (early <= 0 || steady <= 0) return 1;
		double db = 10.0 * log10((early / early_count) / (steady / steady_count));
		printf("MIDI note %u: 10..100 ms versus sustain %+.3f dB\n", (unsigned)keys[key], db);
		if (db > 2.0) burst = true;
		/* Discard release/effects tails between reference notes. */
		for (size_t at = 0; at < 2U * AUDIO_SAMPLE_RATE; at += 1024)
			if (sink->render(sink->state, frames, 1024) != 0) return 1;
	}
	return burst ? 1 : 0;
}
/* Measure reverb tails around a file -> ANSI -> file transition. This checks
 * that making ANSI dry does not discard the file's controller settings. */
static int effects(struct st_midi_sink *sink)
{
	if (sink->set_ansi == NULL) return 77;
	double tail[3];
	int16_t frames[1024 * AUDIO_CHANNELS];
	for (int pass = 0; pass < 3; ++pass) {
		int status = pass == 1 ? st_midi_sink_ansi(sink) : st_midi_sink_start(sink);
		if (pass == 1 && status == 0) status = st_midi_sink_ansi(sink);
		if (pass == 0 && status == 0) {
			const uint8_t reverb[] = {0xb0, 91, 127, 0xb0, 93, 0};
			status = st_midi_sink_send(sink, reverb, sizeof(reverb));
		}
		/* Use the same file instrument in all three captures, so only the
		 * renderer's mode and restored effects differ. */
		const uint8_t note[] = {0xc0, 0, 0x90, 69, 100};
		if (status == 0) status = st_midi_sink_send(sink, note, sizeof(note));
		for (size_t at = 0; status == 0 && at < AUDIO_SAMPLE_RATE / 2; at += 1024)
			status = sink->render(sink->state, frames, 1024);
		st_midi_sink_release(sink, true);
		/* Skip the synth's partially rendered block after All Sounds Off. */
		if (status == 0) status = sink->render(sink->state, frames, 1024);
		double energy = 0;
		for (int block = 0; status == 0 && block < 4; ++block) {
			status = sink->render(sink->state, frames, 1024);
			if (status == 0) for (size_t i = 0; i < 1024 * AUDIO_CHANNELS; ++i)
				energy += (double)frames[i] * frames[i];
		}
		if (status != 0) return 1;
		tail[pass] = sqrt(energy / (4U * 1024U * AUDIO_CHANNELS));
		for (size_t at = 0; at < 4U * AUDIO_SAMPLE_RATE; at += 1024)
			if (sink->render(sink->state, frames, 1024) != 0) return 1;
	}
	printf("effect-tail RMS: file %.3f, ANSI %.3f, restored file %.3f\n", tail[0], tail[1], tail[2]);
	return tail[0] > 10 && tail[1] < tail[0] / 10 && tail[2] > tail[0] / 2 && tail[2] < tail[0] * 2 ? 0 : 1;
}
int main(int argc, char **argv)
{
	if (argc != 2 && (argc != 3 || (strcmp(argv[2], "--levels") != 0 && strcmp(argv[2], "--envelope") != 0 && strcmp(argv[2], "--effects") != 0))) return 2;
	bool levels = argc == 3 && strcmp(argv[2], "--levels") == 0;
	enum st_midi_backend bit = strcmp(argv[1], "fluid") == 0 ? ST_MIDI_FLUIDSYNTH
	    : strcmp(argv[1], "adl") == 0 ? ST_MIDI_ADLMIDI : ST_MIDI_APPLE;
	struct st_midi_sink sink;
	int status = st_midi_sink_open(&sink, bit, NULL, NULL);
	if (status != 0) { fprintf(stderr, "synth unavailable: %d\n", status); return 77; }
	if (sink.render == NULL) { st_midi_sink_close(&sink); return 2; }
	if (argc == 3 && !levels) {
		status = strcmp(argv[2], "--effects") == 0 ? effects(&sink) : envelope(&sink);
		st_midi_sink_close(&sink); return status;
	}
	if (levels) {
		float gain = sink.pcm_gain_db;
		double synth_rms, classic_rms, synth_dc[AUDIO_CHANNELS], classic_dc[AUDIO_CHANNELS]; int synth_peak, classic_peak;
		status = capture(&sink, &synth_rms, synth_dc, &synth_peak);
		struct st_midi_sink classic = {0};
		if (status == 0) status = capture(&classic, &classic_rms, classic_dc, &classic_peak);
		if (status == 0 && synth_rms > 0 && classic_rms > 0) {
			printf("source gain %.3f dB; mixed ANSI synth AC RMS %.3f, peak %d; classic AC RMS %.3f, peak %d; difference %+.3f dB\n",
			    gain, synth_rms, synth_peak, classic_rms, classic_peak, 20.0 * log10(synth_rms / classic_rms));
			printf("output DC: synth L/R %.3f/%.3f; classic L/R %.3f/%.3f\n",
			    synth_dc[0], synth_dc[1], classic_dc[0], classic_dc[1]);
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
