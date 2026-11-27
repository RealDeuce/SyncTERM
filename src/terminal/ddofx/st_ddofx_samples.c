/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "path.h"
#include "st_ddofx_resource.h"
#include "st_ddofx_samples.h"
#include "st_ddofx_script.h"
#include "string_util.h"

enum {
	BANK_SIZE       = 100,
	PROFILE_COUNT   = 150,
	MAX_PARTS       = 20,
	MAX_SAMPLE_SIZE = 65536
};

struct sample {
	uint8_t *bytes;
	size_t   length;
};

struct bank {
	struct sample samples[BANK_SIZE];
	unsigned      count;
};

struct profile {
	struct sample sample;
	unsigned      expected, loaded;
};

struct realplay {
	FILE    *file;
	uint64_t length, position, buffer_start;
	size_t   available;
	unsigned fraction;
	uint8_t  buffer[8192];
};

struct sample_voice {
	audio_stream_t      *stream;
	const struct sample *source;
	uint64_t             position;
	unsigned             rate;
	atomic_bool          playing;
};

struct st_ddofx_samples {
	struct realplay     real;
	struct bank         theme, bites, terminal;
	bool                terminal_selected;
	struct profile      profiles[PROFILE_COUNT];
	char                theme_name[ST_DDOFX_BODY_MAX + 1], bite_file[ST_DDOFX_BODY_MAX + 1];
	struct sample_voice main, animation, motion;
};

const char *
st_ddofx_theme_name(const struct st_ddofx *ddo)
{
	return ddo->samples && *ddo->samples->theme_name ? ddo->samples->theme_name : "NONE";
}

static int16_t
amplitude(uint8_t byte)
{
	return (int16_t)((byte < 128 ? (int)byte : (int)byte - 256) * 256);
}

static size_t
render(void *context, int16_t *pcm, size_t frames)
{
	struct sample_voice *s      = context;
	const uint64_t       end    = (uint64_t)s->source->length * AUDIO_SAMPLE_RATE;
	size_t               filled = 0;
	while (filled < frames && s->position < end) {
		pcm[2 * filled] = pcm[2 * filled + 1] =
		    amplitude(s->source->bytes[s->position / AUDIO_SAMPLE_RATE]);
		s->position += s->rate;
		++filled;
	}
	if (s->position >= end)
		atomic_store_explicit(&s->playing, false, memory_order_relaxed);
	return filled;
}

static void
stop_voice(struct sample_voice *v)
{
	/* Destruction waits for the renderer before its sample storage is released. */
	audio_stream_destroy(v->stream);
	v->stream = NULL;
	v->source = NULL;
	atomic_store_explicit(&v->playing, false, memory_order_relaxed);
}

static void
stop(struct st_ddofx_samples *s)
{
	if (s->real.file) {
		fclose(s->real.file);
		s->real.file = NULL;
	}
	stop_voice(&s->main);
	stop_voice(&s->animation);
	stop_voice(&s->motion);
}

static bool
start_stream(struct st_ddofx_samples *s, struct sample_voice *v, st_audio_runtime_t *audio, unsigned rate)
{
	audio_engine_t *engine;
	if (st_audio_runtime_acquire(audio, &engine) != 0)
		return false;
	if (v == &s->main)
		stop(s);
	else
		stop_voice(v);
	v->rate = rate;
	return audio_stream_create(&v->stream, engine, 0, 0) == 0;
}

static void
play(struct st_ddofx_samples *s, struct sample_voice *v, st_audio_runtime_t *audio, const struct sample *source,
    int32_t rate)
{
	if (source->length == 0 || rate <= 0 || !start_stream(s, v, audio, (unsigned)rate))
		return;
	v->source   = source;
	v->position = 0;
	atomic_store_explicit(&v->playing, true, memory_order_relaxed);
	if (audio_stream_set_source(v->stream, render, v) != 0 || audio_stream_source_active(v->stream, true) != 0)
		stop_voice(v);
}

void
st_ddofx_animation_sample(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, bool motion, int sample,
    int rate)
{
	struct st_ddofx_samples *s = ddo->samples;
	if (s && sample >= 1 && (unsigned)sample <= s->theme.count)
		play(s, motion ? &s->motion : &s->animation, sink->audio, &s->theme.samples[sample - 1], rate);
}

enum {
	QUEUED_FRAMES = 4096
};

uint32_t
st_ddofx_sample_wait_ms(const struct st_ddofx *ddo)
{
	const struct st_ddofx_samples *s = ddo->samples;
	if (s == NULL || s->real.file == NULL)
		return UINT32_MAX;
	size_t queued = audio_stream_buffered_frames(s->main.stream);
	return queued <= QUEUED_FRAMES / 2 ? 0
	                                   : (uint32_t)((queued - QUEUED_FRAMES / 2) * 1000 / AUDIO_SAMPLE_RATE);
}

void
st_ddofx_samples_poll(struct st_ddofx *ddo)
{
	struct st_ddofx_samples *s = ddo->samples;
	if (st_ddofx_sample_wait_ms(ddo) != 0)
		return;

	/* File access stays on the session thread. Each refill reads at most
	 * one block and queues at most 93 ms, independent of file size/rate. */
	struct realplay *r = &s->real;
	if (r->position >= r->buffer_start + r->available) {
		r->buffer_start = r->position;
		if (fs_stream_seek(r->file, (int64_t)r->position) != 0
		    || (r->available = fread(r->buffer, 1, sizeof(r->buffer), r->file)) == 0) {
			fclose(r->file);
			r->file = NULL;
			return;
		}
	}

	int16_t *pcm = malloc(QUEUED_FRAMES * AUDIO_FRAME_SIZE);
	if (pcm == NULL) {
		stop(s);
		return;
	}
	size_t filled = 0;
	while (filled < QUEUED_FRAMES && r->position < r->length && r->position - r->buffer_start < r->available) {
		pcm[2 * filled] = pcm[2 * filled + 1] = amplitude(r->buffer[r->position - r->buffer_start]);
		r->fraction += s->main.rate;
		r->position += r->fraction / AUDIO_SAMPLE_RATE;
		r->fraction %= AUDIO_SAMPLE_RATE;
		++filled;
	}
	if (audio_stream_append_take(s->main.stream, pcm, filled, NULL) != 0) {
		free(pcm);
		stop(s);
	}
	else if (r->position >= r->length) {
		fclose(r->file);
		r->file = NULL;
	}
}

static void
clear_sample(struct st_ddofx_samples *s, struct sample *sample)
{
	if (s->main.source == sample)
		stop_voice(&s->main);
	if (s->animation.source == sample)
		stop_voice(&s->animation);
	if (s->motion.source == sample)
		stop_voice(&s->motion);
	free(sample->bytes);
	*sample = (struct sample) {0};
}

static void
clear_bank(struct st_ddofx_samples *s, struct bank *bank)
{
	for (unsigned i = 0; i < bank->count; ++i)
		clear_sample(s, &bank->samples[i]);
	bank->count = 0;
}

static void
clear_profile(struct st_ddofx_samples *s, struct profile *profile)
{
	clear_sample(s, &profile->sample);
	profile->loaded = profile->expected = 0;
}

void
st_ddofx_clear_sample_profiles(struct st_ddofx *ddo)
{
	if (ddo->samples != NULL)
		for (unsigned i = 0; i < PROFILE_COUNT; ++i)
			clear_profile(ddo->samples, &ddo->samples->profiles[i]);
}

void
st_ddofx_samples_destroy(struct st_ddofx *ddo)
{
	struct st_ddofx_samples *s = ddo->samples;
	if (s == NULL)
		return;
	stop(s);
	clear_bank(s, &s->theme);
	clear_bank(s, &s->bites);
	clear_bank(s, &s->terminal);
	st_ddofx_clear_sample_profiles(ddo);
	free(s);
	ddo->samples = NULL;
}

static bool
samples_ready(struct st_ddofx *ddo)
{
	if (ddo->samples == NULL) {
		ddo->samples = calloc(1, sizeof(*ddo->samples));
		if (ddo->samples == NULL)
			return false;
		atomic_init(&ddo->samples->main.playing, false);
		atomic_init(&ddo->samples->animation.playing, false);
		atomic_init(&ddo->samples->motion.playing, false);
	}
	return true;
}

void
st_ddofx_play_realplay(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *name,
    unsigned rate)
{
	if ((!st_audio_runtime_is_pull(sink->audio) && !st_audio_runtime_allowed(sink->audio))
	    || !samples_ready(ddo))
		return;
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, name);
	if (file == NULL)
		return;
	int64_t                  length = fs_stream_size(file);
	struct st_ddofx_samples *s      = ddo->samples;
	if (length > 0 && start_stream(s, &s->main, sink->audio, rate)) {
		s->real = (struct realplay) {.file = file, .length = (uint64_t)length};
		st_ddofx_samples_poll(ddo);
	}
	else
		fclose(file);
}

static void
play_file(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	const char *name = st_ddofx_field(&args);
	int32_t     rate;
	if (args == NULL || !st_ddofx_number(ddo, args, &rate) || rate <= 0)
		return;
	st_ddofx_play_realplay(ddo, sink, st_ddofx_value(ddo, name), (unsigned)rate);
}

enum load_result {
	LOADED,
	ABSENT,
	FAILED
};

static enum load_result
append_member(FILE *file, const char *name, struct sample *sample)
{
	uint32_t length;
	if (st_ddofx_resource_find(file, name, -1, &length, NULL) < 0)
		return ABSENT;
	if (length == 0 || length > MAX_SAMPLE_SIZE)
		return FAILED;
	/* Each caller enforces its bank/part count before appending. */
	uint8_t *data = realloc(sample->bytes, sample->length + length);
	if (data == NULL)
		return FAILED;
	sample->bytes = data;
	if (fread(data + sample->length, 1, length, file) != length)
		return FAILED;
	sample->length += length;
	return LOADED;
}

static enum load_result
append_number(FILE *file, const char *prefix, unsigned index, struct sample *sample)
{
	char member[13];
	int  len = snprintf(member, sizeof(member), "%s%u.SAM", prefix, index);
	if (len < 1 || (size_t)len >= sizeof(member))
		return ABSENT;
	return append_member(file, member, sample);
}

static bool
load_bank(struct st_ddofx_samples *s, const struct st_ddofx_sink *sink, const char *category, const char *name,
    struct bank *bank)
{
	FILE *file = sink->open_asset ? sink->open_asset(sink->context, category, NULL, name) : NULL;
	if (file == NULL)
		return false;
	struct bank next = {0};
	bool        ok   = fs_stream_size(file) >= 792;
	unsigned    limit = bank == &s->terminal ? 27 : BANK_SIZE;
	while (ok && next.count < limit) {
		struct sample   *entry  = &next.samples[next.count];
		enum load_result result = append_number(file, "SAM", next.count + 1, entry);
		if (result != LOADED) {
			free(entry->bytes);
			entry->bytes = NULL;
			ok           = result == ABSENT;
			break;
		}
		++next.count;
	}
	fclose(file);
	if (ok) {
		clear_bank(s, bank);
		*bank = next;
	}
	else
		clear_bank(s, &next);
	return ok;
}

static char *
term_selection_path(const struct st_ddofx_sink *sink)
{
	char *path = NULL;
	if (sink->cache_directory)
		(void)path_join_alloc(&path, sink->cache_directory, "terminal-theme");
	return path;
}

static void
select_term_theme(struct st_ddofx_samples *s, const struct st_ddofx_sink *sink, const char *name)
{
	if (!ascii_case_compare(name, "NONE")) {
		clear_bank(s, &s->terminal);
		s->terminal_selected = true;
	}
	else if (st_ddofx_dos_filename(name) && load_bank(s, sink, "THEMES", name, &s->terminal)) {
		s->terminal_selected = true;
		char *path           = term_selection_path(sink);
		if (path)
			(void)fs_replace_file(path, name, strlen(name));
		free(path);
	}
}

void
st_ddofx_term_sound(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, unsigned number)
{
	if ((!st_audio_runtime_is_pull(sink->audio) && !st_audio_runtime_allowed(sink->audio))
	    || !samples_ready(ddo))
		return;
	struct st_ddofx_samples *s = ddo->samples;
	if (!s->terminal_selected) {
		char *path = term_selection_path(sink);
		FILE *file;
		char  name[14] = "DEFAULT.THE";
		if (path && fs_open(&file, path, "rb") == 0) {
			size_t n = fread(name, 1, sizeof(name) - 1, file);
			name[n]  = 0;
			if (ferror(file) || n > 12 || strlen(name) != n || !st_ddofx_dos_filename(name))
				strcpy(name, "DEFAULT.THE");
			fclose(file);
		}
		free(path);
		s->terminal_selected = load_bank(s, sink, "THEMES", name, &s->terminal);
	}
	if (number && number <= s->terminal.count)
		play(s, &s->main, sink->audio, &s->terminal.samples[number - 1], 8500);
}

static void
load_bites(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *arg)
{
	struct st_ddofx_samples *s = ddo->samples;
	if (*arg == ',')
		strcpy(s->bite_file, st_ddofx_value(ddo, arg + 1));
	else if (!ascii_case_compare(arg, "NONE"))
		clear_bank(s, &s->bites);
	else if (!ascii_case_compare(arg, "LOAD"))
		load_bank(s, sink, "BBSTHEME", s->bite_file, &s->bites);
	else {
		int32_t index;
		if (s->bites.count == BANK_SIZE || !st_ddofx_number(ddo, arg, &index) || index < 1
		    || index > BANK_SIZE)
			return;
		FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, s->bite_file);
		if (file != NULL) {
			struct sample sample = {0};
			if (append_number(file, "SAM", (unsigned)index, &sample) == LOADED)
				s->bites.samples[s->bites.count++] = sample;
			else
				free(sample.bytes);
			fclose(file);
		}
	}
}

static void
play_slot(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	struct st_ddofx_samples *s             = ddo->samples;
	bool                     explicit_rate = *body++ == 'P';
	bool                     bite          = *body == (explicit_rate ? 'B' : 'b');
	if (bite)
		++body;
	int32_t id, rate = 8500;
	if (!st_ddofx_field_number(ddo, &body, &id) || (explicit_rate && !st_ddofx_field_number(ddo, &body, &rate))
	    || body != NULL)
		return;
	struct bank *bank = bite ? &s->bites : &s->theme;
	if (id >= 1 && (unsigned)id <= bank->count)
		play(s, &s->main, sink->audio, &bank->samples[id - 1], rate);
}

enum profile_command {
	LOAD,
	DEFINE,
	ADD,
	UNLOAD,
	PLAY,
	CHECK
};

static void
big_sample(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, enum profile_command cmd, char *args)
{
	int32_t id, number;
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > PROFILE_COUNT)
		return;
	struct st_ddofx_samples *s = ddo->samples;
	if (cmd == CHECK) {
		if (id > 0 && args == NULL) {
			const char           *reply = "N/A";
			const struct profile *p     = s ? &s->profiles[id - 1] : NULL;
			if (p != NULL && p->loaded && p->loaded == p->expected)
				reply = s->main.source == &p->sample
				                && atomic_load_explicit(&s->main.playing, memory_order_relaxed)
				            ? "1"
				            : "0";
			st_ddofx_query_reply(ddo, sink, reply, false);
		}
		return;
	}
	if (!st_audio_runtime_is_pull(sink->audio) && !st_audio_runtime_allowed(sink->audio))
		return;
	if (!samples_ready(ddo))
		return;
	s = ddo->samples;
	if (!id) {
		if (cmd != PLAY)
			st_ddofx_clear_sample_profiles(ddo);
		return;
	}
	struct profile *p = &s->profiles[id - 1];
	if (cmd == UNLOAD)
		clear_profile(s, p);
	else if (cmd == DEFINE && args != NULL && st_ddofx_number(ddo, args, &number) && number >= 1
	         && number <= MAX_PARTS) {
		clear_profile(s, p);
		p->expected = number;
	}
	else if (cmd == PLAY && p->loaded && p->loaded == p->expected && args
	         && st_ddofx_number(ddo, args, &number))
		play(s, &s->main, sink->audio, &p->sample, number);
	else if (args && (cmd == LOAD || (cmd == ADD && p->loaded < p->expected))) {
		const char *name = st_ddofx_value(ddo, args);
		FILE       *file = sink->open_asset(sink->context, "BBSTHEME", NULL, s->theme_name);
		if (file == NULL)
			return;
		if (cmd == ADD) {
			if (append_member(file, name, &p->sample) == LOADED)
				++p->loaded;
		}
		else if (*name && strlen(name) <= 6) {
			struct profile   next   = {0};
			enum load_result result = LOADED;
			while (next.loaded < MAX_PARTS
			       && (result = append_number(file, name, next.loaded + 1, &next.sample)) == LOADED)
				++next.loaded;
			if (result != FAILED) {
				clear_profile(s, p);
				next.expected = next.loaded;
				*p            = next;
			}
			else
				free(next.sample.bytes);
		}
		fclose(file);
	}
}

struct speech {
	const struct st_ddofx_sink *sink;
	const char *container, *output;
	char *phrase;
	bool append;
};

static int
write_speech(FILE *output, void *context)
{
	struct speech *speech = context;
	if (speech->append) {
		FILE *previous;
		int status = fs_open(&previous, speech->output, "rb");
		if (status == 0) {
			int64_t length = fs_stream_size(previous);
			status = length < 0 ? IO_ERROR_IO : st_ddofx_copy_bytes(output, previous, (uint64_t)length);
			fclose(previous);
		}
		if (status != 0 && status != IO_ERROR_NOT_FOUND)
			return status;
	}

	FILE *bank = speech->sink->open_asset(speech->sink->context, "BBSTHEME", NULL, speech->container);
	if (!bank)
		return 0;
	int status = 0;
	for (char *token = speech->phrase; token && !status;) {
		char *next = strchr(token, '-');
		if (next)
			*next++ = 0;
		size_t size = strlen(token);
		if (size && size <= 8) {
			char member[13];
			unsigned divisor = 1;
			if (size == 2 && ascii_to_upper((unsigned char)token[0]) == 'P'
			    && token[1] >= '1' && token[1] <= '4') {
				strcpy(member, "PAUSE.SND");
				divisor = (unsigned)(token[1] - '0');
			}
			else
				snprintf(member, sizeof(member), "%s.SND", token);
			uint32_t length;
			if (st_ddofx_resource_find(bank, member, -1, &length, NULL) >= 0)
				status = st_ddofx_copy_bytes(output, bank, length / divisor);
		}
		token = next;
	}
	fclose(bank);
	return status;
}

static void
assemble_speech(const struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *args)
{
	const char *container = st_ddofx_field(&args);
	const char *name = st_ddofx_field(&args);
	char *phrase = st_ddofx_field(&args);
	int32_t operation;
	if (!args || !sink->theme_path || !st_ddofx_dos_filename(container) || !st_ddofx_dos_filename(name)
	    || !st_ddofx_number(ddo, args, &operation))
		return;
	char *path = sink->theme_path(sink->context, name);
	if (!path)
		return;
	if (operation == 3)
		(void)fs_remove_file(path);
	else {
		struct speech speech = {.sink = sink, .container = container, .output = path,
		    .phrase = phrase, .append = operation != 1};
		(void)fs_replace_file_with(path, write_speech, &speech);
	}
	free(path);
}

bool
st_ddofx_samples_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	if (*body == '6') {
		static const char *const names[] = {"LBSOUND,", "DEFBSND,", "ADDBSND,", "UNLOADBS,", "PLAYBS,",
		    "CHECKBS,"};
		for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
			size_t len = strlen(names[i]);
			if (ascii_case_ncompare(body + 1, names[i], len) == 0) {
				big_sample(ddo, sink, (enum profile_command)i, body + 1 + len);
				return true;
			}
		}
		return false;
	}
	if (*body == 'M' && ascii_case_ncompare(body + 1, "TALK,", 5) == 0) {
		assemble_speech(ddo, sink, body + 6);
		return true;
	}
	if (*body == 'M' && ascii_case_ncompare(body + 1, "PREAL,", 6) == 0) {
		play_file(ddo, sink, body + 7);
		return true;
	}
	if (*body != 'T' && *body != 't' && *body != 'p' && *body != 'P')
		return false;
	if ((!st_audio_runtime_is_pull(sink->audio) && !st_audio_runtime_allowed(sink->audio))
	    || !samples_ready(ddo))
		return true;
	struct st_ddofx_samples *s = ddo->samples;
	if (*body == 'T')
		select_term_theme(s, sink, st_ddofx_value(ddo, body + 1));
	else if (*body != 't')
		play_slot(ddo, sink, body);
	else if (body[1] == 'b')
		load_bites(ddo, sink, body + 2);
	else {
		const char *name = st_ddofx_value(ddo, body + 1);
		if (!ascii_case_compare(name, "NONE")) {
			clear_bank(s, &s->theme);
			s->theme_name[0] = 0;
		}
		else if (load_bank(s, sink, "BBSTHEME", name, &s->theme))
			strcpy(s->theme_name, name);
	}
	return true;
}
