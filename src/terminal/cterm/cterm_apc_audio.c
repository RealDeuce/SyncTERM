/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm_apc.h"
#include "cterm_apc_cache.h"
#include "cterm.h"
#include "st_audio_file.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static audio_stream_t *
channel_stream(struct cterminal *terminal, unsigned channel)
{
	audio_engine_t *engine;
	audio_stream_t **stream;

	if (channel == 0) return st_music_pcm_stream(terminal->music_player);
	if (channel == 1) return terminal->fx_stream;
	if (channel >= 16) return NULL;
	stream = &terminal->apc->channels[channel - 2];
	if (*stream == NULL && terminal->screen.audio != NULL
	    && st_audio_runtime_acquire(terminal->screen.audio, &engine) == 0)
		(void)audio_stream_create(stream, engine, -12.0f, -12.0f);
	return *stream;
}

static bool
channel_busy(struct cterminal *terminal, unsigned channel)
{
	if (channel == 0) return st_music_busy(terminal->music_player);
	audio_stream_t *stream = channel_stream(terminal, channel);
	return stream != NULL && !audio_stream_is_idle(stream);
}

static size_t
parse_duration(const char *text, double frequency, bool periods)
{
	char *end;
	uint64_t value = strtoul(text, &end, 10);
	uint64_t frames;

	if (end == text) return 0;
	if (*end == 0 || *end == ';'
	    || (end[0] == 'm' && end[1] == 's' && (end[2] == 0 || end[2] == ';'))) {
		if (value > UINT64_MAX / AUDIO_SAMPLE_RATE) return 0;
		frames = (uint64_t)value * AUDIO_SAMPLE_RATE / 1000;
		return frames > SIZE_MAX ? 0 : (size_t)frames;
	}
	if (end[0] == 'f' && (end[1] == 0 || end[1] == ';')) return (size_t)value;
	if (periods && end[0] == 'p' && (end[1] == 0 || end[1] == ';')
	    && isfinite(frequency) && frequency > 0) {
		double count = (double)value * (double)AUDIO_SAMPLE_RATE / frequency + 0.5;
		return isfinite(count) && count >= 0 && count < (double)SIZE_MAX ? (size_t)count : 0;
	}
	return 0;
}

static float
parse_volume(const char *text)
{
	char *end;
	float value = strtof(text, &end);

	if (end == text) return 0.0f;
	if ((end[0] == 'd' || end[0] == 'D') && (end[1] == 'b' || end[1] == 'B')
	    && (end[2] == 0 || end[2] == ';')) return value;
	if (value <= 0.0f) return -60.0f;
	if (value > 100.0f) value = 100.0f;
	return 20.0f * log10f(value / 100.0f);
}

static int
parse_shape(const char *text)
{
	static const struct {const char *name; int shape;} shapes[] = {
		{"SINE_SAW_HARM", AUDIO_WAVE_SINE_SAW_HARM},
		{"SINE_SAW_CHORD", AUDIO_WAVE_SINE_SAW_CHORD},
		{"SINE_HARM", AUDIO_WAVE_SINE_HARM},
		{"SINE_SAW", AUDIO_WAVE_SINE_SAW},
		{"SILENCE", -2}, {"SAW", AUDIO_WAVE_SAWTOOTH},
		{"SIN", AUDIO_WAVE_SINE}, {"SQ", AUDIO_WAVE_SQUARE},
	};
	for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); ++i) {
		size_t length = strlen(shapes[i].name);
		if (strncmp(text, shapes[i].name, length) == 0
		    && (text[length] == 0 || text[length] == ';')) return shapes[i].shape;
	}
	return -1;
}

static void
load_patch(struct cterminal *terminal, char *text, size_t size, bool blob)
{
	unsigned long slot = 256;
	char *p;
	int16_t *frames = NULL;
	size_t count;
	int status;

	for (p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] != 'S' || p[2] != '=') break;
		slot = strtoul(p + 3, NULL, 10);
	}
	if (slot >= 256 || p == NULL || *p != ';') return;
	if (blob) {
		void *data = NULL;
		size_t decoded_size;

		if (size <= (size_t)(p + 1 - text)
		    || cterm_apc_decode_base64(&data, &decoded_size, p + 1,
		    size - (size_t)(p + 1 - text)) != 0) return;
		status = st_audio_file_decode_memory(&frames, &count, data, decoded_size);
		free(data);
	}
	else {
		FILE *stream = cterm_apc_cache_open(terminal->apc->directory, p + 1, false);
		if (stream == NULL) return;
		status = st_audio_file_decode_stream(&frames, &count, stream);
		(void)fclose(stream);
	}
	if (status != 0) return;
	free(terminal->apc->patches[slot].frames);
	terminal->apc->patches[slot] = (struct cterm_apc_patch){frames, count};
}

static void
synth_patch(struct cterminal *terminal, char *text)
{
	unsigned long slot = 256;
	int shape = -1;
	double frequency = 0;
	size_t count = 0;
	int16_t *frames;

	for (char *p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] == 'S' && p[2] == '=') slot = strtoul(p + 3, NULL, 10);
		else if (p[1] == 'W' && p[2] == '=') shape = parse_shape(p + 3);
		else if (p[1] == 'F' && p[2] == '=') frequency = strtod(p + 3, NULL);
		else if (p[1] == 'T' && p[2] == '=') count = parse_duration(p + 3, frequency, true);
	}
	if (slot >= 256 || (shape < 0 && shape != -2) || count == 0
	    || count > INT_MAX || count > SIZE_MAX / AUDIO_FRAME_SIZE) return;
	if (shape == -2) { frequency = 0; shape = AUDIO_WAVE_SINE; }
	frames = malloc(count * AUDIO_FRAME_SIZE);
	if (frames == NULL) return;
	if (audio_generate_tone(frames, count, frequency, (enum audio_wave_shape)shape, true) != 0) {
		free(frames); return;
	}
	free(terminal->apc->patches[slot].frames);
	terminal->apc->patches[slot] = (struct cterm_apc_patch){frames, count};
}

static void
copy_patch(struct cterminal *terminal, char *text)
{
	unsigned long source = 256, destination = 256;
	struct cterm_apc_patch *patch;
	int16_t *frames;

	for (char *p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] == 'S' && p[2] == '=') source = strtoul(p + 3, NULL, 10);
		else if (p[1] == 'D' && p[2] == '=') destination = strtoul(p + 3, NULL, 10);
	}
	if (source >= 256 || destination >= 256) return;
	patch = &terminal->apc->patches[source];
	if (patch->frames == NULL || patch->count == 0) return;
	frames = malloc(patch->count * AUDIO_FRAME_SIZE);
	if (frames == NULL) return;
	memcpy(frames, patch->frames, patch->count * AUDIO_FRAME_SIZE);
	free(terminal->apc->patches[destination].frames);
	terminal->apc->patches[destination] = (struct cterm_apc_patch){frames, patch->count};
}

static void
queue_patch(struct cterminal *terminal, char *text)
{
	unsigned long channel = 16, slot = 256;
	struct audio_buffer_options options = AUDIO_BUFFER_OPTIONS_INIT;
	audio_stream_t *stream;
	struct cterm_apc_patch *patch;

	for (char *p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] == 'C' && p[2] == '=') channel = strtoul(p + 3, NULL, 10);
		else if (p[1] == 'S' && p[2] == '=') slot = strtoul(p + 3, NULL, 10);
		else if (p[1] == 'I' && p[2] == '=') options.fade_in_frames = parse_duration(p + 3, 0, false);
		else if (p[1] == 'O' && p[2] == '=') options.fade_out_frames = parse_duration(p + 3, 0, false);
		else if (p[1] == 'X' && (p[2] == ';' || p[2] == 0)) options.crossfade = true;
		else if (p[1] == 'L' && (p[2] == ';' || p[2] == 0)) options.loop = true;
		else if (p[1] == 'V' && p[2] == 'L' && p[3] == '=') options.volume_left_db = parse_volume(p + 4);
		else if (p[1] == 'V' && p[2] == 'R' && p[3] == '=') options.volume_right_db = parse_volume(p + 4);
		else if (p[1] == 'V' && p[2] == '=') options.volume_left_db = options.volume_right_db = parse_volume(p + 3);
	}
	if (channel < 2 || channel >= 16 || slot >= 256) return;
	patch = &terminal->apc->patches[slot];
	if (patch->frames == NULL || patch->count == 0) return;
	stream = channel_stream(terminal, (unsigned)channel);
	if (stream == NULL) return;
	/* The reference consumes a patch even when append fails. The modern
	 * take API transfers ownership only on success, so free on failure. */
	if (audio_stream_append_take(stream, patch->frames, patch->count, &options) != 0)
		free(patch->frames);
	*patch = (struct cterm_apc_patch){0};
}

static void
flush_channel(struct cterminal *terminal, char *text)
{
	unsigned long channel = 16;
	size_t fadeout = 0;
	audio_stream_t *stream;

	for (char *p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] == 'C' && p[2] == '=') channel = strtoul(p + 3, NULL, 10);
		else if (p[1] == 'O' && p[2] == '=') fadeout = parse_duration(p + 3, 0, false);
	}
	if (channel >= 16) return;
	if (channel == 0) { (void)st_music_flush(terminal->music_player, fadeout); return; }
	stream = channel_stream(terminal, (unsigned)channel);
	if (stream == NULL) return;
	if (fadeout != 0 && fadeout <= SIZE_MAX / AUDIO_FRAME_SIZE) {
		struct audio_buffer_options options = AUDIO_BUFFER_OPTIONS_INIT;
		int16_t *silence = calloc(fadeout, AUDIO_FRAME_SIZE);

		if (silence != NULL) {
			options.fade_in_frames = fadeout;
			options.crossfade = true;
			if (audio_stream_append_take(stream, silence, fadeout, &options) == 0) return;
			free(silence);
		}
	}
	(void)audio_stream_clear(stream);
}

static void
volume_channel(struct cterminal *terminal, char *text)
{
	unsigned long channel = 16;
	float left = 0, right = 0;
	bool have_left = false, have_right = false;
	size_t count = 0;
	audio_stream_t *stream;

	for (char *p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] == 'C' && p[2] == '=') channel = strtoul(p + 3, NULL, 10);
		else if (p[1] == 'V' && p[2] == 'L' && p[3] == '=') { left = parse_volume(p + 4); have_left = true; }
		else if (p[1] == 'V' && p[2] == 'R' && p[3] == '=') { right = parse_volume(p + 4); have_right = true; }
		else if (p[1] == 'V' && p[2] == '=') { left = right = parse_volume(p + 3); have_left = have_right = true; }
		else if (p[1] == 'T' && p[2] == '=') count = parse_duration(p + 3, 0, false);
	}
	if (channel >= 16) return;
	if (channel == 0) {
		if ((!have_left && !have_right) || !cterm_music_ensure(terminal)) return;
		float current_left, current_right;
		(void)st_music_get_volume(terminal->music_player, &current_left, &current_right);
		(void)st_music_volume(terminal->music_player, have_left ? left : current_left,
		    have_right ? right : current_right, count);
		return;
	}
	stream = channel_stream(terminal, (unsigned)channel);
	if (stream == NULL || (!have_left && !have_right)) return;
	if (!have_left || !have_right) {
		float current_left = -12.0f, current_right = -12.0f;
		(void)audio_stream_get_volume(stream, &current_left, &current_right);
		if (!have_left) left = current_left;
		if (!have_right) right = current_right;
	}
	if (count != 0) (void)audio_stream_ramp_volume(stream, left, right, count);
	else (void)audio_stream_set_volume(stream, left, right);
}

bool
cterm_apc_audio_dispatch(struct cterminal *terminal, char *text, size_t size)
{
	char *arguments = strchr(text, ';');
	size_t length = arguments == NULL ? strlen(text) : (size_t)(arguments - text);
	unsigned long channel = 16;

	if (arguments == NULL) arguments = text + length;
#define VERB(name) (length == sizeof(name) - 1 && memcmp(text, name, sizeof(name) - 1) == 0)
	if (VERB("LoadBlob")) load_patch(terminal, arguments, size - length, true);
	else if (VERB("Load")) load_patch(terminal, arguments, size - length, false);
	else if (VERB("Synth")) synth_patch(terminal, arguments);
	else if (VERB("Copy")) copy_patch(terminal, arguments);
	else if (VERB("QueueMIDI")) {
		if (*arguments == ';' && cterm_music_ensure(terminal)) {
			FILE *file = cterm_apc_cache_open(terminal->apc->directory, arguments + 1, false);
			if (file != NULL && st_music_queue_file(terminal->music_player, file) != 0) fclose(file);
		}
	}
	else if (VERB("Queue")) queue_patch(terminal, arguments);
	else if (VERB("Flush")) flush_channel(terminal, arguments);
	else if (VERB("Volume")) volume_channel(terminal, arguments);
	else if (VERB("Wait") || VERB("Update")) {
		for (char *p = arguments; p != NULL && *p == ';'; p = strchr(p + 1, ';'))
			if (p[1] == 'C' && p[2] == '=') channel = strtoul(p + 3, NULL, 10);
		if (channel < 16) {
			if (VERB("Update")) {
				terminal->apc->notify[channel] = true;
				if (channel == 0 && channel_busy(terminal, 0)) terminal->apc->was_running[0] = true;
			}
			else if (channel == 0) (void)st_music_wait(terminal->music_player);
			else {
				audio_stream_t *stream = channel_stream(terminal, (unsigned)channel);
				if (stream != NULL) (void)audio_stream_drain(stream);
			}
		}
	}
	else return false;
#undef VERB
	return true;
}

bool
cterm_apc_feature_query(struct cterminal *terminal, const char *text)
{
	char reply[80];
	int count;

	if (strcmp(text, "midi") == 0)
		count = snprintf(reply, sizeof(reply), "\x1b[=7;102;%dn",
		    cterm_music_ensure(terminal) && st_music_available(terminal->music_player) ? 1 : 0);
	else if (strcmp(text, "libsndfile") == 0)
		count = snprintf(reply, sizeof(reply), "\x1b[=7;100;%dn", st_audio_file_available() ? 1 : 0);
	else if (strncmp(text, "libsndfileFormat;", 17) == 0) {
		char *end;
		unsigned long major, subtype;

		errno = 0; major = strtoul(text + 17, &end, 0);
		if (end == text + 17 || *end != ';' || errno != 0 || major > UINT32_MAX) return true;
		text = end + 1;
		errno = 0; subtype = strtoul(text, &end, 0);
		if (end == text || *end != 0 || errno != 0 || subtype > UINT32_MAX) return true;
		count = snprintf(reply, sizeof(reply), "\x1b[=7;101;%lu;%lu;%dn", major, subtype,
		    st_audio_file_format_available((uint32_t)major, (uint32_t)subtype) ? 1 : 0);
	}
	else return false;
	if (count > 0 && (size_t)count < sizeof(reply)) cterm_respond(terminal, reply, (size_t)count);
	return true;
}

void
cterm_apc_audio_query(struct cterminal *terminal, int count, const uint64_t *params)
{
	char reply[256];
	size_t used = 4;

	while (terminal->parent != NULL) terminal = terminal->parent;
	if (terminal->apc == NULL) return;
	memcpy(reply, "\x1b[=7", 4);
	if (count < 2) {
		for (unsigned i = 0; i < 16; ++i) {
			if (channel_busy(terminal, i))
				used += (size_t)snprintf(reply + used, sizeof(reply) - used, ";%u;1", i);
		}
	}
	else if (params[1] < 16) {
		unsigned channel = (unsigned)params[1];
		used += (size_t)snprintf(reply + used, sizeof(reply) - used, ";%u;%d", channel,
		    channel_busy(terminal, channel) ? 1 : 0);
	}
	reply[used++] = 'n';
	cterm_respond(terminal, reply, used);
}

void
cterm_apc_poll(struct cterminal *terminal)
{
	if (terminal->apc == NULL) return;
	for (unsigned i = 0; i < 16; ++i) {
		bool running;

		if (!terminal->apc->notify[i]) continue;
		running = channel_busy(terminal, i);
		if (terminal->apc->was_running[i] && !running) {
			char reply[32];
			int size = snprintf(reply, sizeof(reply), "\x1b[=7;%u;0n", i);
			cterm_respond(terminal, reply, (size_t)size);
			terminal->apc->notify[i] = false;
		}
		terminal->apc->was_running[i] = running;
	}
}
