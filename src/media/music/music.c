/* Logical channel zero. The mutex owns the FIFO, generation and controls;
 * the worker exclusively owns synthesis and native MIDI calls. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "st_threads.h"
#include "st_time.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define REQUEST_LIMIT 128
#define NOTE_STORAGE_LIMIT (64U * 1024U * 1024U)
#define AHEAD_FRAMES 4096U
#define BLOCK_FRAMES 1024U
/* Bound live-input latency without a second MIDI/synthesis worker. */
#define LIVE_FRAMES 256U
struct request {
	FILE *file;
	struct st_music_note *notes;
	uint8_t *bytes;
	size_t count;
	uint64_t sequence;
	struct request *next;
};
struct completion { uint64_t sequence, frame; };
struct st_music {
	st_audio_runtime_t *runtime;
	audio_stream_t **pcm;
	st_mutex_t mutex;
	st_condition_t changed;
	st_thread_t thread;
	bool started, stopping, active, injected, was_file;
	uint64_t generation, working_generation, accepted, completed;
	struct request *head, *tail;
	size_t requests, storage_bytes;
	struct st_midi_sink sink;
	uint32_t attempted, policy;
	float left, right, from_left, from_right, target_left, target_right;
	uint64_t clock, ramp_start;
	size_t ramp_frames, fade_frames;
	bool controls_changed, fading;
	bool gain_valid;
	float applied_left, applied_right;
	uint64_t fade_start, fade_cutoff;
	/* PCM completion markers allow later captures to render before an
	 * earlier capture drains, without completing its waiter prematurely. */
	struct completion markers[REQUEST_LIMIT];
	size_t marker_first, marker_count;
	uint64_t pcm_origin, rendered, progress;
};
static void pause_worker(void)
{
	const struct timespec delay = {0, 2000000};
	(void)st_thread_sleep(&delay, NULL);
}
static bool cancelled(void *context)
{
	st_music_t *m = context;
	st_mutex_lock(&m->mutex);
	bool result = m->stopping || m->generation != m->working_generation;
	st_mutex_unlock(&m->mutex);
	return result || st_audio_runtime_stopped(m->runtime);
}
static bool validation_stopped(void *context)
{
	st_music_t *m = context;
	st_mutex_lock(&m->mutex); bool stop = m->stopping; st_mutex_unlock(&m->mutex);
	return stop || st_audio_runtime_stopped(m->runtime);
}
static int pcm_locked(st_music_t *m)
{
	if (*m->pcm != NULL) return 0;
	audio_engine_t *engine;
	int result = st_audio_runtime_acquire(m->runtime, &engine);
	if (result != 0) return result;
	return audio_stream_create(m->pcm, engine, m->left, m->right);
}
/* Called while idle, under the player mutex. Backend constructors do not
 * invoke the cancellation callback. Failed methods are retried after policy
 * changes, rather than on every availability query or ANSI capture. */
static bool select_locked(st_music_t *m)
{
	if (m->injected) return m->sink.send != NULL;
	uint32_t mask = st_audio_runtime_midi_allowed(m->runtime);
	if (mask != m->policy) { m->policy = mask; m->attempted = 0; }
	if (m->sink.send != NULL && (mask & m->sink.kind)) return true;
	st_midi_sink_close(&m->sink);
	const enum st_midi_backend order[] = {ST_MIDI_WINMM, ST_MIDI_APPLE,
	    ST_MIDI_ALSA, ST_MIDI_FLUIDSYNTH, ST_MIDI_ADLMIDI};
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
		uint32_t bit = order[i];
		if (!(mask & bit) || !(st_midi_compiled_backends() & bit) || (m->attempted & bit)) continue;
		if (st_audio_runtime_is_pull(m->runtime) && (bit == ST_MIDI_ALSA || bit == ST_MIDI_WINMM)) continue;
		m->attempted |= bit;
		if (st_midi_sink_open(&m->sink, order[i], cancelled, m) == 0) {
			if (m->sink.render == NULL || pcm_locked(m) == 0) return true;
			st_midi_sink_close(&m->sink);
		}
	}
	return false;
}
static void discard(struct request *r)
{
	if (r->file != NULL) fclose(r->file);
	free(r->notes);
	free(r->bytes);
	free(r);
}
static size_t request_storage(const struct request *r)
{
	return r->bytes != NULL ? r->count : r->count * sizeof(*r->notes);
}
static void drop_pending(st_music_t *m)
{
	while (m->head != NULL) {
		struct request *r = m->head;
		m->head = r->next;
		m->storage_bytes -= request_storage(r);
		--m->requests;
		discard(r);
	}
	m->tail = NULL;
}
static void complete_markers(st_music_t *m, uint64_t clock, bool all)
{
	while (m->marker_count != 0) {
		struct completion *c = &m->markers[m->marker_first];
		if (!all && c->frame > clock) break;
		m->completed = c->sequence; --m->requests;
		m->marker_first = (m->marker_first + 1) % REQUEST_LIMIT;
		--m->marker_count;
		st_condition_broadcast(&m->changed);
	}
}
/* Clock is rendered PCM minus outstanding frames for internal playback, and
 * elapsed monotonic frames for native output. No wall clock paces synthesis. */
static int controls(st_music_t *m, uint64_t clock)
{
	st_mutex_lock(&m->mutex);
	m->clock = clock;
	complete_markers(m, clock, false);
	bool changed = m->controls_changed;
	m->controls_changed = false;
	if (m->ramp_frames != 0) {
		uint64_t elapsed = clock - m->ramp_start;
		double fraction = elapsed >= m->ramp_frames ? 1.0 : (double)elapsed / m->ramp_frames;
		m->left = (float)(m->from_left + fraction * (m->target_left - m->from_left));
		m->right = (float)(m->from_right + fraction * (m->target_right - m->from_right));
		if (fraction == 1.0) m->ramp_frames = 0;
	}
	bool fade_done = m->fading && clock - m->fade_start >= m->fade_frames;
	float left = m->left, right = m->right;
	if (m->fading) {
		double fraction = fade_done ? 1.0 : (double)(clock - m->fade_start) / m->fade_frames;
		left = (float)(left + fraction * (-192.0 - left));
		right = (float)(right + fraction * (-192.0 - right));
	}
	if (changed && *m->pcm != NULL) {
		(void)audio_stream_set_volume(*m->pcm, left, right);
		uint64_t elapsed = m->fading ? clock - m->fade_start : clock - m->ramp_start;
		size_t length = m->fading ? m->fade_frames : m->ramp_frames;
		size_t remaining = elapsed >= length ? 0 : length - (size_t)elapsed;
		(void)audio_stream_ramp_volume(*m->pcm, m->fading ? -192 : m->target_left,
		    m->fading ? -192 : m->target_right, remaining);
	}
	bool stop = m->stopping || m->generation != m->working_generation || fade_done;
	st_mutex_unlock(&m->mutex);
	if (stop || st_audio_runtime_stopped(m->runtime)) return IO_ERROR_CANCELLED;
	if (m->sink.send != NULL && (!m->gain_valid || left != m->applied_left || right != m->applied_right)) {
		int result = st_midi_sink_gain(&m->sink, left, right);
		if (result != 0) return result;
		m->gain_valid = true; m->applied_left = left; m->applied_right = right;
	}
	return 0;
}
struct playback { st_music_t *music; uint64_t start_ns, rendered, origin, progress, base; bool pcm; };
static int update(struct playback *p)
{
	uint64_t clock;
	if (p->pcm) {
		size_t pending = audio_stream_buffered_frames(*p->music->pcm);
		clock = p->rendered > pending ? p->rendered - pending : 0;
	} else {
		uint64_t now;
		if (st_time_monotonic_ns(&now) != 0) return IO_ERROR_IO;
		uint64_t elapsed = now - p->start_ns;
		clock = elapsed / 1000000000U * AUDIO_SAMPLE_RATE
		    + elapsed % 1000000000U * AUDIO_SAMPLE_RATE / 1000000000U;
	}
	if (clock < p->progress) clock = p->progress;
	p->progress = clock;
	return controls(p->music, p->origin + clock);
}
/* Advance to an absolute frame in the request. Classic tones use the same
 * bounded PCM path; chunking does not restart their phase or edge envelope. */
static int advance(struct playback *p, uint64_t target, int pitch, uint64_t tone_start, const int16_t *tone)
{
	st_music_t *m = p->music;
	if (target > UINT64_MAX - p->base || tone_start > UINT64_MAX - p->base) return IO_ERROR_OUT_OF_RANGE;
	target += p->base; tone_start += p->base;
	for (;;) {
		int result = update(p);
		if (result != 0) return result;
		if (!p->pcm) {
			uint64_t now;
			if (st_time_monotonic_ns(&now) != 0) return IO_ERROR_IO;
			uint64_t elapsed = now - p->start_ns;
			uint64_t frames = elapsed / 1000000000U * AUDIO_SAMPLE_RATE
			    + elapsed % 1000000000U * AUDIO_SAMPLE_RATE / 1000000000U;
			if (frames >= target) return 0;
			pause_worker(); continue;
		}
		if (p->rendered >= target) return 0;
		size_t pending = audio_stream_buffered_frames(*m->pcm);
		if (pending >= AHEAD_FRAMES) { pause_worker(); continue; }
		size_t count = BLOCK_FRAMES;
		if (count > target - p->rendered) count = (size_t)(target - p->rendered);
		if (count > AHEAD_FRAMES - pending) count = AHEAD_FRAMES - pending;
		int16_t *frames = calloc(count, AUDIO_FRAME_SIZE);
		if (frames == NULL) return IO_ERROR_OUT_OF_MEMORY;
		if (m->sink.render != NULL) result = m->sink.render(m->sink.state, frames, count);
		else if (pitch >= 0 && tone != NULL) memcpy(frames, tone + (size_t)(p->rendered - tone_start) * AUDIO_CHANNELS, count * AUDIO_FRAME_SIZE);
		st_mutex_lock(&m->mutex);
		if (m->stopping || m->generation != m->working_generation) result = IO_ERROR_CANCELLED;
		struct audio_buffer_options options = AUDIO_BUFFER_OPTIONS_INIT;
		if (m->sink.render != NULL)
			options.volume_left_db = options.volume_right_db = m->sink.pcm_gain_db;
		if (result == 0) result = audio_stream_append_take(*m->pcm, frames, count, &options);
		st_mutex_unlock(&m->mutex);
		if (result != 0) { free(frames); return result; }
		p->rendered += count;
	}
}
static uint64_t ns_frames(uint64_t ns)
{
	return ns / 1000000000U * AUDIO_SAMPLE_RATE + ns % 1000000000U * AUDIO_SAMPLE_RATE / 1000000000U;
}
static int play(struct playback *p, struct request *r)
{
	st_music_t *m = p->music;
	int result = 0;
	if (r->file != NULL) {
		struct st_midi_file *file;
		result = st_midi_file_open(&file, r->file, cancelled, m);
		if (result != 0) return result;
		result = st_midi_sink_start(&m->sink);
		m->was_file = true;
		struct st_midi_event event;
		int next = 0;
		while (result == 0 && (next = st_midi_file_next(file, &event)) == 0) {
			result = advance(p, ns_frames(event.time_ns), -1, 0, 0);
			if (result == 0) result = st_midi_sink_send(&m->sink, event.bytes, event.size);
		}
		if (result == 0 && next < 0) result = next;
		if (result == 0) result = advance(p, ns_frames(event.time_ns), -1, 0, 0);
		st_midi_file_close(file);
		st_midi_sink_release(&m->sink, result != 0);
		if (result == 0) result = advance(p, ns_frames(event.time_ns) + 2U * AUDIO_SAMPLE_RATE, -1, 0, 0);
		/* Native destinations keep sounding without further PCM pulls.
		 * Bound long releases and held voices to the advertised tail. */
		st_midi_sink_release(&m->sink, true);
	} else {
		if (m->was_file && m->sink.send != NULL) result = st_midi_sink_start(&m->sink);
		m->was_file = false;
		if (result == 0 && m->sink.send != NULL) result = st_midi_sink_ansi(&m->sink);
		uint64_t at = 0;
		for (size_t i = 0; result == 0 && i < r->count; ++i) {
			struct st_music_note *n = &r->notes[i];
			uint8_t message[] = {0x90, (uint8_t)(n->pitch + 36), 100};
			if (m->sink.send != NULL && n->pitch >= 0 && n->sound_frames != 0)
				result = st_midi_sink_send(&m->sink, message, sizeof(message));
			int16_t *tone = NULL;
			if (result == 0 && m->sink.send == NULL && n->pitch >= 0 && n->sound_frames != 0) {
				tone = malloc(n->sound_frames * AUDIO_FRAME_SIZE);
				result = tone == NULL ? IO_ERROR_OUT_OF_MEMORY : audio_generate_tone(tone, n->sound_frames,
				    st_music_note_frequency(n->pitch), AUDIO_WAVE_SINE_SAW_HARM, true);
			}
			if (result == 0) result = advance(p, at + n->sound_frames, n->pitch, at, tone);
			free(tone);
			message[0] = 0x80; message[2] = 0;
			if (m->sink.send != NULL && n->pitch >= 0 && n->sound_frames != 0)
				(void)st_midi_sink_send(&m->sink, message, sizeof(message));
			at += n->sound_frames;
			if (result == 0) result = advance(p, at + n->pause_frames, -1, 0, 0);
			at += n->pause_frames;
		}
	}
	return result;
}
/* Return early when another request can render on the same PCM backend.
 * Otherwise retain logical busy through backend acknowledgement/latency. */
static int drain_pcm(struct playback *p, bool *continuing)
{
	st_music_t *m = p->music;
	*continuing = false;
	for (;;) {
		int result = update(p);
		if (result != 0) return result;
		st_mutex_lock(&m->mutex);
		bool next = m->head != NULL && !m->fading && !m->stopping
		    && m->generation == m->working_generation
		    && (m->injected || m->policy == st_audio_runtime_midi_allowed(m->runtime))
		    && (m->head->file == NULL || m->sink.send != NULL);
		st_mutex_unlock(&m->mutex);
		if (next) { *continuing = true; return 0; }
		if (audio_stream_buffered_frames(*m->pcm) == 0) {
			result = audio_stream_drain(*m->pcm);
			return result == 0 ? update(p) : result;
		}
		pause_worker();
	}
}
/* A live stream retains decoder state and held voices between writes. Only
 * this worker calls the sink. A software sink keeps rendering while idle;
 * native sinks keep sounding themselves. The caller supplies byte timing.
 * Enter and leave with the player mutex held. */
static void live(st_music_t *m)
{
	bool continuing = m->active;
	m->active = true; m->working_generation = m->generation;
	bool midi = continuing ? m->sink.send != NULL : select_locked(m);
	int result = midi ? 0 : IO_ERROR_NOT_SUPPORTED;
	bool pcm = midi && m->sink.render != NULL;
	if (result == 0 && pcm) result = pcm_locked(m);
	if (!continuing) { m->rendered = m->progress = 0; m->pcm_origin = m->clock; }
	struct playback p = {.music = m, .pcm = pcm, .origin = pcm ? m->pcm_origin : m->clock,
	    .rendered = m->rendered, .progress = m->progress};
	if (!continuing) { m->controls_changed = true; m->gain_valid = false; }
	uint64_t sequence = m->completed;
	st_mutex_unlock(&m->mutex);
	if (result == 0 && st_time_monotonic_ns(&p.start_ns) != 0) result = IO_ERROR_IO;
	/* Restore an ANSI-specific synthesizer mode without injecting a reset or
	 * releasing the wire stream. Later ANSI captures need their normal reset. */
	if (result == 0 && m->sink.set_ansi != NULL) result = m->sink.set_ansi(m->sink.state, false);
	m->was_file = true;
	while (result == 0) {
		result = update(&p);
		st_mutex_lock(&m->mutex);
		if (!m->injected && m->policy != st_audio_runtime_midi_allowed(m->runtime))
			result = IO_ERROR_CANCELLED;
		struct request *r = m->head;
		if (result != 0 || (r != NULL && r->bytes == NULL)) {
			st_mutex_unlock(&m->mutex);
			break;
		}
		if (r != NULL) {
			m->head = r->next; if (m->head == NULL) m->tail = NULL;
			st_mutex_unlock(&m->mutex);
			result = st_midi_decode_stream(&m->sink, r->bytes, r->count, m->sink.send, m->sink.state);
			st_mutex_lock(&m->mutex);
			m->storage_bytes -= request_storage(r);
			sequence = r->sequence;
			if (result == 0 && pcm) {
				uint64_t latency = ((uint64_t)audio_stream_latency_ms(*m->pcm) * AUDIO_SAMPLE_RATE + 999) / 1000;
				struct completion *c = &m->markers[(m->marker_first + m->marker_count) % REQUEST_LIMIT];
				*c = (struct completion){sequence, p.origin + p.rendered + latency};
				++m->marker_count;
				complete_markers(m, p.origin + p.progress, false);
			} else {
				--m->requests;
				if (result == 0) m->completed = sequence;
			}
			discard(r);
			st_condition_broadcast(&m->changed);
			st_mutex_unlock(&m->mutex);
			continue;
		}
		st_mutex_unlock(&m->mutex);
		if (pcm) {
			size_t pending = audio_stream_buffered_frames(*m->pcm);
			if (pending < LIVE_FRAMES) {
				/* advance only fills the vacant live window; it never waits
				 * for the consumer with incoming MIDI bytes queued. */
				p.base = 0;
				if (p.rendered > UINT64_MAX - (LIVE_FRAMES - pending)) result = IO_ERROR_OUT_OF_RANGE;
				else result = advance(&p, p.rendered + LIVE_FRAMES - pending, -1, 0, NULL);
				continue;
			}
		}
		struct timespec deadline;
		if (st_time_utc_deadline_after_ms(&deadline, pcm ? 2 : 20) != 0) {
			result = IO_ERROR_IO; break;
		}
		st_mutex_lock(&m->mutex);
		/* The outer loop rechecks queued bytes, controls and cancellation
		 * after every wake. Runtime/policy changes need no player signal. */
		if (m->head == NULL && !m->controls_changed && !m->stopping
		    && m->generation == m->working_generation) {
			int waited = st_condition_timedwait(&m->changed, &m->mutex, &deadline);
			if (waited != ST_THREAD_SUCCESS && waited != ST_THREAD_TIMED_OUT) result = IO_ERROR_IO;
		}
		st_mutex_unlock(&m->mutex);
	}
	st_mutex_lock(&m->mutex);
	if (m->generation != m->working_generation || m->stopping) result = IO_ERROR_CANCELLED;
	m->rendered = p.rendered; m->progress = p.progress;
	if (result == 0) return; /* The next file/ANSI request owns the transition. */
	/* A failed destination cannot retain a meaningful partial wire message.
	 * Cancel its queued prefix; a flushed generation leaves newer writes
	 * alone. Subsequent writes can select a fresh sink. */
	while (m->generation == m->working_generation && m->head != NULL && m->head->bytes != NULL) {
		struct request *r = m->head;
		m->head = r->next; if (m->head == NULL) m->tail = NULL;
		m->storage_bytes -= request_storage(r); --m->requests;
		sequence = r->sequence; discard(r);
	}
	st_mutex_unlock(&m->mutex);
	st_midi_sink_release(&m->sink, true);
	st_mutex_lock(&m->mutex);
	if (*m->pcm != NULL) (void)audio_stream_clear(*m->pcm);
	complete_markers(m, 0, true);
	if (m->completed < sequence) m->completed = sequence;
	if (m->fading) {
		m->fading = false; m->controls_changed = true;
		if (m->completed < m->fade_cutoff) m->completed = m->fade_cutoff;
		if (*m->pcm != NULL) (void)audio_stream_set_volume(*m->pcm, m->left, m->right);
	}
	m->active = false;
	st_condition_broadcast(&m->changed);
}
static int worker(void *context)
{
	st_music_t *m = context;
	st_mutex_lock(&m->mutex);
	for (;;) {
		while (m->head == NULL && !m->stopping)
			st_condition_wait(&m->changed, &m->mutex);
		if (m->stopping) break;
		if (m->head->bytes != NULL) { live(m); continue; }
		struct request *r = m->head;
		m->head = r->next; if (m->head == NULL) m->tail = NULL;
		bool continuing = m->active;
		m->active = true; m->working_generation = m->generation;
		bool midi = continuing ? m->sink.send != NULL : select_locked(m);
		int result = r->file != NULL && !midi ? IO_ERROR_NOT_SUPPORTED : 0;
		bool pcm = !midi || m->sink.render != NULL;
		if (result == 0 && pcm) result = pcm_locked(m);
		if (!continuing) { m->rendered = m->progress = 0; m->pcm_origin = m->clock; }
		struct playback p = {.music = m, .pcm = pcm, .origin = pcm ? m->pcm_origin : m->clock,
		    .base = pcm ? m->rendered : 0, .rendered = m->rendered, .progress = m->progress};
		if (!continuing) { m->controls_changed = true; m->gain_valid = false; }
		st_mutex_unlock(&m->mutex);
		if (result == 0 && st_time_monotonic_ns(&p.start_ns) == 0) result = play(&p, r);
		else if (result == 0) result = IO_ERROR_IO;
		st_mutex_lock(&m->mutex);
		m->storage_bytes -= request_storage(r);
		uint64_t sequence = r->sequence;
		if (result == 0 && pcm) {
			uint64_t latency = ((uint64_t)audio_stream_latency_ms(*m->pcm) * AUDIO_SAMPLE_RATE + 999) / 1000;
			struct completion *c = &m->markers[(m->marker_first + m->marker_count) % REQUEST_LIMIT];
			*c = (struct completion){sequence, p.origin + p.rendered + latency};
			++m->marker_count;
		} else --m->requests;
		discard(r);
		st_mutex_unlock(&m->mutex);
		bool more = false;
		if (result == 0 && pcm) result = drain_pcm(&p, &more);
		st_mutex_lock(&m->mutex);
		m->rendered = p.rendered; m->progress = p.progress;
		/* A flush can revoke the next request after drain_pcm observed it. */
		if (m->generation != m->working_generation || m->stopping) result = IO_ERROR_CANCELLED;
		if (m->head == NULL) more = false;
		if (result == 0 && pcm && more) continue;
		if (result != 0) {
			st_mutex_unlock(&m->mutex);
			st_midi_sink_release(&m->sink, true);
			st_mutex_lock(&m->mutex);
		}
		if (result != 0 && *m->pcm != NULL) (void)audio_stream_clear(*m->pcm);
		complete_markers(m, 0, true);
		m->active = false;
		if (m->completed < sequence) m->completed = sequence;
		if (m->fading) {
			m->fading = false; m->controls_changed = true;
			if (m->completed < m->fade_cutoff) m->completed = m->fade_cutoff;
			if (*m->pcm != NULL) (void)audio_stream_set_volume(*m->pcm, m->left, m->right);
		}
		if (m->head == NULL) m->completed = m->accepted;
		st_condition_broadcast(&m->changed);
	}
	st_mutex_unlock(&m->mutex);
	st_midi_sink_release(&m->sink, true);
	return 0;
}
int st_music_create(st_music_t **result, st_audio_runtime_t *runtime, audio_stream_t **pcm)
{
	if (result == NULL || runtime == NULL || pcm == NULL) return IO_ERROR_INVALID_ARGUMENT;
	st_music_t *m = calloc(1, sizeof(*m));
	if (m == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (st_mutex_init(&m->mutex, ST_MUTEX_PLAIN) != ST_THREAD_SUCCESS) { free(m); return IO_ERROR_IO; }
	if (st_condition_init(&m->changed) != ST_THREAD_SUCCESS) {
		st_mutex_destroy(&m->mutex); free(m); return IO_ERROR_IO;
	}
	m->runtime = runtime; m->pcm = pcm;
	m->left = m->right = m->target_left = m->target_right = -12;
	*result = m;
	return 0;
}
int st_music_create_with_sink(st_music_t **result, st_audio_runtime_t *runtime,
	audio_stream_t **pcm, const struct st_midi_sink *sink)
{
	int status = st_music_create(result, runtime, pcm);
	if (status == 0) {
		(*result)->sink = *sink; (*result)->injected = true;
		(*result)->sink.cancelled = cancelled; (*result)->sink.cancel_context = *result;
		memset((*result)->sink.volume, 100, sizeof((*result)->sink.volume));
		memset((*result)->sink.pan, 64, sizeof((*result)->sink.pan));
	}
	return status;
}
void st_music_destroy(st_music_t *m)
{
	if (m == NULL) return;
	st_mutex_lock(&m->mutex);
	m->stopping = true; ++m->generation; drop_pending(m);
	if (*m->pcm != NULL) (void)audio_stream_clear(*m->pcm);
	st_condition_broadcast(&m->changed);
	st_mutex_unlock(&m->mutex);
	if (m->started) (void)st_thread_join(m->thread, NULL);
	st_midi_sink_close(&m->sink);
	audio_stream_destroy(*m->pcm); *m->pcm = NULL;
	st_condition_destroy(&m->changed); st_mutex_destroy(&m->mutex); free(m);
}
bool st_music_available(st_music_t *m)
{
	if (m == NULL || st_audio_runtime_stopped(m->runtime)) return false;
	st_mutex_lock(&m->mutex);
	bool result = m->active ? m->sink.send != NULL : select_locked(m);
	st_mutex_unlock(&m->mutex);
	return result;
}
static int enqueue(st_music_t *m, struct request *r)
{
	st_mutex_lock(&m->mutex);
	int result = 0;
	if (m->stopping || st_audio_runtime_stopped(m->runtime)) result = IO_ERROR_CANCELLED;
	else if (m->requests == REQUEST_LIMIT || request_storage(r) > NOTE_STORAGE_LIMIT - m->storage_bytes
	    || m->accepted == UINT64_MAX) result = IO_ERROR_BUSY;
	else if (!m->started) {
		if (st_thread_create(&m->thread, worker, m) != ST_THREAD_SUCCESS) result = IO_ERROR_IO;
		else m->started = true;
	}
	if (result == 0) {
		r->sequence = ++m->accepted; ++m->requests;
		m->storage_bytes += request_storage(r);
		if (m->tail == NULL) m->head = r; else m->tail->next = r;
		m->tail = r;
		st_condition_signal(&m->changed);
	}
	st_mutex_unlock(&m->mutex);
	return result;
}
int st_music_queue_file(st_music_t *m, FILE *file)
{
	if (m == NULL || file == NULL) return IO_ERROR_INVALID_ARGUMENT;
	st_mutex_lock(&m->mutex);
	int status = m->stopping || st_audio_runtime_stopped(m->runtime) ? IO_ERROR_CANCELLED
	    : m->requests == REQUEST_LIMIT ? IO_ERROR_BUSY : 0;
	st_mutex_unlock(&m->mutex);
	if (status != 0) return status;
	if (!st_music_available(m)) return IO_ERROR_NOT_SUPPORTED;
	/* Validate without sending anything. The queued authority is just the open
	 * file; only the active request retains a bounded parser/message arena. */
	struct st_midi_file *parsed;
	status = st_midi_file_open(&parsed, file, validation_stopped, m);
	if (status != 0) return status;
	st_midi_file_close(parsed);
	struct request *r = calloc(1, sizeof(*r));
	if (r == NULL) return IO_ERROR_OUT_OF_MEMORY;
	r->file = file;
	status = enqueue(m, r);
	if (status != 0) free(r);
	return status;
}
int st_music_queue_notes(st_music_t *m, struct st_music_note *notes, size_t count)
{
	if (m == NULL || (count != 0 && notes == NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (count > NOTE_STORAGE_LIMIT / sizeof(*notes)) return IO_ERROR_MESSAGE_TOO_LARGE;
	for (size_t i = 0; i < count; ++i) {
		if (notes[i].pitch < -1 || notes[i].pitch > 83 || notes[i].sound_frames > AUDIO_SAMPLE_RATE * 60U
		    || notes[i].pause_frames > AUDIO_SAMPLE_RATE * 60U) return IO_ERROR_OUT_OF_RANGE;
	}
	struct request *r = calloc(1, sizeof(*r));
	if (r == NULL) return IO_ERROR_OUT_OF_MEMORY;
	r->notes = notes; r->count = count;
	int status = enqueue(m, r);
	if (status != 0) free(r);
	return status;
}
int st_music_queue_midi(st_music_t *m, const uint8_t *bytes, size_t count)
{
	if (m == NULL || (count != 0 && bytes == NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (count > NOTE_STORAGE_LIMIT) return IO_ERROR_MESSAGE_TOO_LARGE;
	if (count == 0) return 0;
	if (!st_music_available(m)) return st_audio_runtime_stopped(m->runtime)
	    ? IO_ERROR_CANCELLED : IO_ERROR_NOT_SUPPORTED;
	struct request *r = calloc(1, sizeof(*r));
	if (r == NULL) return IO_ERROR_OUT_OF_MEMORY;
	r->bytes = malloc(count);
	if (r->bytes == NULL) { free(r); return IO_ERROR_OUT_OF_MEMORY; }
	memcpy(r->bytes, bytes, count); r->count = count;
	int status = enqueue(m, r);
	if (status != 0) discard(r);
	return status;
}
int st_music_wait(st_music_t *m)
{
	if (m == NULL) return IO_ERROR_INVALID_ARGUMENT;
	st_mutex_lock(&m->mutex);
	uint64_t target = m->accepted, generation = m->generation;
	while (m->completed < target && generation == m->generation && !m->stopping)
		st_condition_wait(&m->changed, &m->mutex);
	int result = generation != m->generation || m->stopping ? IO_ERROR_CANCELLED : 0;
	st_mutex_unlock(&m->mutex);
	return result;
}
bool st_music_busy(st_music_t *m)
{
	if (m == NULL) return false;
	st_mutex_lock(&m->mutex);
	bool busy = m->active || m->head != NULL;
	st_mutex_unlock(&m->mutex);
	return busy;
}
int st_music_flush(st_music_t *m, size_t fade_frames)
{
	if (m == NULL) return 0;
	st_mutex_lock(&m->mutex);
	uint64_t cutoff = m->accepted;
	drop_pending(m);
	if (fade_frames != 0 && m->active) {
		m->fading = true; m->fade_frames = fade_frames; m->fade_start = m->clock;
		m->fade_cutoff = cutoff;
		m->controls_changed = true;
		st_condition_signal(&m->changed);
		if (*m->pcm != NULL && (m->sink.send == NULL || m->sink.render != NULL)) {
			(void)audio_stream_ramp_volume(*m->pcm, -192, -192, fade_frames);
			m->controls_changed = false;
		}
	} else {
		++m->generation;
		if (*m->pcm != NULL) (void)audio_stream_clear(*m->pcm);
		st_condition_broadcast(&m->changed);
		while (m->active && m->working_generation != m->generation) st_condition_wait(&m->changed, &m->mutex);
		if (m->completed < cutoff) m->completed = cutoff;
	}
	st_mutex_unlock(&m->mutex);
	return 0;
}
int st_music_volume(st_music_t *m, float left, float right, size_t ramp)
{
	if (m == NULL || !isfinite(left) || !isfinite(right) || left < -192 || right < -192 || left > 96 || right > 96)
		return IO_ERROR_INVALID_ARGUMENT;
	st_mutex_lock(&m->mutex);
	m->from_left = m->left; m->from_right = m->right;
	m->target_left = left; m->target_right = right; m->ramp_start = m->clock;
	m->ramp_frames = m->active ? ramp : 0;
	if (m->ramp_frames == 0) { m->left = left; m->right = right; }
	m->controls_changed = true;
	st_condition_signal(&m->changed);
	if (*m->pcm != NULL && (m->sink.send == NULL || m->sink.render != NULL)) {
		(void)audio_stream_ramp_volume(*m->pcm, left, right, m->ramp_frames);
		m->controls_changed = false;
	}
	st_mutex_unlock(&m->mutex);
	return 0;
}
int st_music_get_volume(st_music_t *m, float *left, float *right)
{
	if (m == NULL || left == NULL || right == NULL) return IO_ERROR_INVALID_ARGUMENT;
	st_mutex_lock(&m->mutex); *left = m->left; *right = m->right; st_mutex_unlock(&m->mutex);
	return 0;
}
audio_stream_t *st_music_pcm_stream(st_music_t *m)
{
	if (m == NULL) return NULL;
	st_mutex_lock(&m->mutex); audio_stream_t *stream = *m->pcm; st_mutex_unlock(&m->mutex);
	return stream;
}
