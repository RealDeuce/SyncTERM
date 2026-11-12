/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_music.h"
#include "cterm_igs_script.h"
#include "cterm_vt52.h"
#include "io_error.h"
#include <string.h>

static uint64_t receive_deadline(uint64_t now)
{
	return now > UINT64_MAX - 5000 ? UINT64_MAX : now + 5000;
}
int cterm_igs_script_load(struct cterm_igs *igs)
{
	struct cterm_igs_script *s = &igs->script;
	if (s->active) return 0;
	s->loading = true; s->lookahead = false; s->pipes = 0; s->received = 0;
	igs->music->script_length = 0;
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status == 0) s->deadline = receive_deadline(now);
	return status;
}
int cterm_igs_script_bytes(struct cterm_igs *igs, const uint8_t *bytes, size_t size,
    size_t *accepted, bool *end)
{
	struct cterm_igs_script *s = &igs->script;
	struct cterm_igs_music *m = igs->music;
	*accepted = 0; *end = false;
	if (!s->loading || (size != 0 && bytes == NULL)) return IO_ERROR_PROTOCOL;
	if (size == 0) return 0;
	uint64_t now;
	int status = igs->now_ms(&now);
	if (status != 0) return status;
	bool expired = now >= s->deadline;
	if (expired) {
		const uint8_t bell = 7;
		status = cterm_vt52_output(igs->terminal, &bell, 1);
		if (status != 0 || igs->reset_pending) return status != 0 ? status : IO_ERROR_INTERRUPTED;
	}
	while (*accepted < size) {
		uint8_t byte = expired && *accepted == 0 ? ' ' : bytes[*accepted];
		if (s->lookahead) {
			s->lookahead = false;
			if (byte == '}') {
				s->pipes = 3;
				if (m->script_length < 2) return IO_ERROR_PROTOCOL;
				m->script_length -= 2;
			} else {
				s->pipes = 0;
				++m->script_length;
			}
		} else {
			/* Ordinary stores never exceed offset 9999. Failed lookahead
			 * can advance over a retained byte, but cannot enlarge storage. */
			if (m->script_length >= 10000) return IO_ERROR_OUT_OF_RANGE;
			s->pipes = byte == '|' ? s->pipes + 1 : 0;
			m->bytes[m->script_length++] = byte;
			s->lookahead = s->pipes == 2;
		}
		++*accepted; ++s->received;
		/* Capacity is checked after lookahead, including at offset 9999. */
		if (!s->lookahead && (s->pipes == 3 || m->script_length > 9999)) {
			/* The original extra decrement escapes storage for an empty
			 * payload. Keep an empty script instead of a huge unsafe length. */
			if (m->script_length != 0) --m->script_length;
			s->loading = false; s->deadline = 0; *end = true;
			return 0;
		}
	}
	s->deadline = receive_deadline(now);
	return 0;
}
void cterm_igs_script_clear(struct cterm_igs *igs)
{
	struct cterm_igs_music *m = igs->music;
	if (igs->script.active || m->script_length == 0) return;
	memset(m->bytes, 0, sizeof(m->bytes));
	m->script_length = 0; m->count = 0;
}
int cterm_igs_script_cancel(struct cterm_igs *igs)
{
	bool flow = igs->script.active && igs->script.return_flow;
	igs->script = (struct cterm_igs_script){0};
	return flow ? cterm_igs_flow(igs, true) : 0;
}
