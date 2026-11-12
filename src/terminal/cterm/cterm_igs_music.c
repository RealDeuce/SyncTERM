/* N: opaque byte loads and buffered MIDI/chip-note playback. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs_music.h"
#include "cterm_igs_audio.h"
#include "cterm.h"
#include "cterm_render.h"
#include "cterm_vt52.h"
#include "st_cterm.h"
#include "io_error.h"
#include "st_console_test.h"
#include <stdlib.h>
#include <string.h>

static uint64_t receive_deadline(uint64_t now)
{
	return now > UINT64_MAX - 5000 ? UINT64_MAX : now + 5000;
}
int cterm_igs_music_reset(struct cterm_igs *igs)
{
	if (igs->music == NULL) {
		igs->music = calloc(1, sizeof(*igs->music));
		if (igs->music == NULL) return IO_ERROR_OUT_OF_MEMORY;
	} else memset(igs->music, 0, sizeof(*igs->music));
	/* Original accessory DATA $13b5c and documented initial MIDI toggle. */
	igs->music->midi_enabled = 1;
	return 0;
}
static int end_flow(struct cterm_igs *igs)
{
	if (!igs->music->flow) return 0;
	igs->music->flow = false;
	return cterm_igs_flow(igs, true);
}
int cterm_igs_music_cancel(struct cterm_igs *igs)
{
	if (igs->music == NULL) return 0;
	struct cterm_igs_music *m = igs->music;
	if (m->playing) {
		igs->wait_deadline = 0; igs->note = (struct cterm_igs_note){0};
	}
	m->command = m->loading = m->playing = m->delaying = false;
	m->receive_deadline = 0;
	igs->input.key_first = igs->input.key_count = 0;
	bool feeding = igs->feeding;
	igs->feeding = true;
	int status = end_flow(igs);
	igs->feeding = feeding;
	if (!feeding && igs->reset_pending) cterm_igs_reset(igs);
	return status;
}
void cterm_igs_music_stop(struct cterm_igs *igs)
{
	if (igs->music != NULL && igs->music->midi_output) {
		(void)st_music_flush(igs->terminal->music_player, 0);
		igs->music->midi_output = false;
	}
}
void cterm_igs_music_prefix(struct cterm_igs *igs, int operation)
{
	struct cterm_igs_music *m = igs->music;
	m->command = true; m->operation = operation;
	m->local_count = m->count; m->received = 0;
	m->index = m->end = 0; m->checked = false;
}
int cterm_igs_music_load(struct cterm_igs *igs, int count)
{
	struct cterm_igs_music *m = igs->music;
	/* Publish before entering the raw reader, retaining old suffix bytes. */
	m->local_count = m->count = count;
	if (count > CTERM_IGS_MUSIC_BYTES) return IO_ERROR_OUT_OF_RANGE;
	m->loading = count > 0;
	if (!m->loading) return 0;
	if (igs->script.active) {m->receive_deadline=0;return 0;}
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status == 0) m->receive_deadline = receive_deadline(now);
	return status;
}
int cterm_igs_music_bytes(struct cterm_igs *igs, const uint8_t *bytes, size_t size,
    size_t *accepted, bool *end)
{
	struct cterm_igs_music *m = igs->music;
	*accepted = 0; *end = false;
	if (!m->loading || size > (size_t)m->local_count - m->received) return IO_ERROR_PROTOCOL;
	uint64_t now=0;
	int status = igs->script.active ? 0 : igs->now_ms(&now);
	if (status != 0) return status;
	bool expired = !igs->script.active && now >= m->receive_deadline;
	if (expired && size != 0) {
		const uint8_t bell = 7;
		status = cterm_vt52_output(igs->terminal, &bell, 1);
		if (status != 0 || igs->reset_pending) return status != 0 ? status : IO_ERROR_INTERRUPTED;
	}
	while (*accepted < size) {
		m->bytes[m->received++] = expired && *accepted == 0 ? ' ' : bytes[*accepted];
		++*accepted;
		/* Payload Ctrl-C/X is opaque. Only a separately queued local key
		 * aborts, after the current store has become persistent. */
		int key = cterm_igs_take_key(igs);
		if (key == 3 || key == 24) {
			m->count = 0; m->operation = 0;
			m->loading = false; *end = true;
			break;
		}
	}
	m->receive_deadline = igs->script.active?0:receive_deadline(now);
	return 0;
}
static int complete(struct cterm_igs *igs)
{
	struct cterm_igs_music *m = igs->music;
	m->command = m->loading = m->playing = m->delaying = false;
	m->receive_deadline = 0; igs->wait_deadline = 0;
	int status = end_flow(igs);
	if (status != 0) return status;
	return igs->script.active?0:cterm_igs_forward_keys(igs);
}
static int midi_byte(struct cterm_igs *igs, uint8_t byte)
{
	struct cterminal *t = igs->terminal;
	/* Explicit protocol music, like b/n, is independent of console quiet.
	 * Disabled/unavailable output still performs the protocol's waits. */
	if (t->screen.audio == NULL) return 0;
	int status = 0;
	if (t->music_player == NULL)
		status = st_music_create(&t->music_player, t->screen.audio, &t->music_stream);
	if (status == 0) status = st_music_queue_midi(t->music_player, &byte, 1);
	if (status == 0) igs->music->midi_output = true;
	if (status == IO_ERROR_BUSY) return status;
	if (status != 0 && (igs->warned['N' / 8] & (1u << ('N' % 8))) == 0) {
		igs->warned['N' / 8] |= (uint8_t)(1u << ('N' % 8));
		cterm_log(t, ST_CTERM_LOG_WARNING, "IGS MIDI output unavailable");
	}
	return 0;
}
int cterm_igs_music_resume(struct cterm_igs *igs)
{
	struct cterm_igs_music *m = igs->music;
	if (!m->playing) return IO_ERROR_PROTOCOL;
	if (m->delaying) {
		uint64_t now;
		int status = igs->now_ms(&now);
		if (status != 0) return status;
		if (now < igs->wait_deadline) return IO_ERROR_IN_PROGRESS;
		m->delaying = false; igs->wait_deadline = 0;
		m->index += 2; m->checked = false;
	}
	while (m->index < m->end) {
		if (!m->checked) {
			int key = cterm_igs_take_key(igs);
			if (key == 3 || key == 24) return complete(igs);
			if (key == '+') ++m->index;
			m->checked = true;
		}
		/* A shifted pair/partial final record may use retained bytes, but
		 * never wrap/cap an invalid index into an infinite native loop. */
		int size = m->midi ? 2 : 6;
		int offset = m->midi ? m->index - 1 : m->index;
		if (offset < 0 || offset > CTERM_IGS_MUSIC_BYTES - size) return IO_ERROR_OUT_OF_RANGE;
		int status;
		if (m->midi) {
			status = midi_byte(igs, m->bytes[offset]);
			if (status == IO_ERROR_BUSY) return IO_ERROR_IN_PROGRESS;
			uint64_t now;
			status = igs->now_ms(&now); /* Output precedes the local start. */
			if (status != 0) return status;
			uint64_t delay = (uint64_t)m->bytes[offset + 1] * 5;
			if (now > UINT64_MAX - delay) return IO_ERROR_OUT_OF_RANGE;
			igs->wait_deadline = now + delay;
			m->delaying = true;
			if (delay != 0) return IO_ERROR_IN_PROGRESS;
			/* Even a zero delay takes a second sample, without a key poll. */
			status = igs->now_ms(&now);
			if (status != 0) return status;
			m->delaying = false; igs->wait_deadline = 0;
			m->index += 2; m->checked = false;
		} else {
			int values[6];
			for (unsigned i = 0; i < 6; ++i) values[i] = m->bytes[offset + (int)i];
			status = cterm_igs_audio_note_begin(igs, values);
			m->index += 6; m->checked = false;
			if (status == IO_ERROR_IN_PROGRESS) {
				/* An already queued local event belongs to the INNER wait;
				 * its value does not become an outer-loop abort or shift. */
				if (igs->input.key_count != 0) { (void)cterm_igs_take_key(igs); igs->note.key = true; }
				return status;
			}
			if (status != 0) return status;
		}
	}
	return complete(igs);
}
int cterm_igs_music_execute(struct cterm_igs *igs, int first, int end)
{
	struct cterm_igs_music *m = igs->music;
	m->loading = false; m->receive_deadline = 0;
	/* Fresh 0/>2 paths save, suppress and restore MIDI before chip playback.
	 * Replays 5/6 preserve it, correcting the uninitialized native local. */
	int op = m->operation;
	m->midi = m->midi_enabled != 0 && op < 3 && op != 0;
	bool chip = op == 4 || (op == 5 && m->local_count > 5);
	if (op == 6 && m->local_count > 5 && (first < 0 || end < 0)) return IO_ERROR_OUT_OF_RANGE;
	bool range = op == 6 && m->local_count > 5 && first >= 0 && first < end && end < 1666;
	if (!m->midi && !chip && !range) return complete(igs);
	m->index = m->midi ? 1 : range ? first * 6 : 0;
	m->end = range ? end * 6 : m->local_count;
	m->playing = m->flow = true;
	int status = cterm_igs_flow(igs, false);
	return status == 0 ? cterm_igs_music_resume(igs) : status;
}

#if ST_CONSOLE_TEST
void cterm_igs_music_inspect(const struct cterm_igs *igs, struct st_console_test_application *app)
{
	const struct cterm_igs_music *m = igs->music;
	uint32_t hash = UINT32_C(2166136261);
	for (size_t i = 0; i < sizeof(m->bytes); ++i) hash = (hash ^ m->bytes[i]) * UINT32_C(16777619);
	st_console_test_number(app, "cterm.igs_music_buffer", hash);
	st_console_test_number(app, "cterm.igs_music_count", m->count);
	st_console_test_number(app, "cterm.igs_script_length", m->script_length);
#define SCRIPT_FIELD(name) st_console_test_number(app,"cterm.igs_script_" #name,igs->script.name)
	SCRIPT_FIELD(active); SCRIPT_FIELD(loading); SCRIPT_FIELD(received);
	SCRIPT_FIELD(cursor); SCRIPT_FIELD(parsed); SCRIPT_FIELD(paused); SCRIPT_FIELD(cooperative);
#undef SCRIPT_FIELD
	st_console_test_number(app, "cterm.igs_midi_enabled", m->midi_enabled);
	st_console_test_number(app, "cterm.igs_music_loading", m->loading);
	st_console_test_number(app, "cterm.igs_music_received", (int64_t)m->received);
	st_console_test_number(app, "cterm.igs_music_playing", m->playing);
	st_console_test_number(app, "cterm.igs_music_midi", m->midi);
	st_console_test_number(app, "cterm.igs_music_index", m->index);
	st_console_test_number(app, "cterm.igs_music_end", m->end);
	st_console_test_number(app, "cterm.igs_music_keys", (int64_t)igs->input.key_count);
}
#endif
