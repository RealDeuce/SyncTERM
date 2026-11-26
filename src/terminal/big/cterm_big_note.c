/* BIG n: incremental class0 reads, live WORD sweeps and local delay waits. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "io_error.h"
#include "st_time.h"

void
cterm_big_note_begin(struct cterm_big *b, bool chained)
{
	b->note = (struct cterm_big_note) {.active = true, .chained = chained, .phase = CTERM_BIG_NOTE_MODE};
}

bool
cterm_big_note_paused(const struct cterm_big *b)
{
	return b->note.active && b->note.phase != CTERM_BIG_NOTE_MODE && b->note.phase != CTERM_BIG_NOTE_READ0
	       && b->note.phase != CTERM_BIG_NOTE_READ1;
}

static int
number(struct cterm_big *b, const uint8_t *bytes, size_t size, size_t *used, uint16_t *value, bool *done)
{
	static const struct st_graphics_parameter    scalar = {.kind = ST_GRAPHICS_NUMBER};
	static const struct st_graphics_command_view view   = {.code = 'n'};
	struct cterm_big_note                       *n      = &b->note;
	*used                                               = 0;
	*done                                               = false;
	int status;
	if (!n->number_started) {
		status = st_big_numeric_field(&b->numeric, &view, &scalar);
		if (status != 0)
			return status;
		n->number_started = true;
	}
	int result = 0;
	if (!n->wire) {
		status = st_big_numeric_number(&b->numeric, &view, &scalar, &result, done);
		if (status != 0)
			return status;
		if (!*done)
			n->wire = true;
	}
	if (n->wire) {
		status = st_big_numeric_read_number(&b->numeric, &view, &scalar, bytes, size, used, &result, done);
		if (status != 0 || !*done)
			return status;
	}
	if (b->reset_pending)
		return IO_ERROR_INTERRUPTED;
	*value            = (uint16_t)result;
	n->number_started = n->wire = false;
	return 0;
}

static int
sample(struct cterm_big *b, uint64_t *value)
{
	struct cterm_big_note *n = &b->note;
	int status = b->numeric.io.now_ns == NULL ? st_time_monotonic_ns(value)
	                                          : b->numeric.io.now_ns(b->numeric.io.context, value);
	if (status == IO_ERROR_IN_PROGRESS)
		return IO_ERROR_PROTOCOL;
	if (status != 0)
		return status;
	if (b->reset_pending)
		return IO_ERROR_INTERRUPTED;
	if (n->sampled && *value < n->sample)
		return IO_ERROR_OUT_OF_RANGE;
	n->sample  = *value;
	n->sampled = true;
	return 0;
}

static int
delay(struct cterm_big *b, uint16_t ms, enum cterm_big_note_phase after)
{
	uint64_t ns;
	int      status = sample(b, &ns);
	if (status != 0)
		return status;
	uint64_t span      = (uint64_t)ms * UINT64_C(1000000);
	b->note.deadline   = UINT64_MAX - ns < span ? UINT64_MAX : ns + span;
	b->note.after_wait = after;
	b->note.phase      = CTERM_BIG_NOTE_WAIT;
	return 0;
}

int
cterm_big_note_poll(struct cterm_big *b, const uint8_t *bytes, size_t size, size_t *used, size_t *budget,
    bool *complete)
{
	struct cterm_big_note *n = &b->note;
	*used                    = 0;
	*complete                = false;
	for (;;) {
		if (b->reset_pending)
			return IO_ERROR_INTERRUPTED;
		int status = 0;
		switch (n->phase) {
			case CTERM_BIG_NOTE_MODE:
			case CTERM_BIG_NOTE_READ0:
			case CTERM_BIG_NOTE_READ1:
			{
				uint16_t value;
				size_t   accepted;
				bool     done;
				status = number(b, bytes == NULL ? NULL : bytes + *used, size - *used, &accepted,
				    &value, &done);
				*used += accepted;
				if (status != 0 || !done)
					return status;
				if (n->phase == CTERM_BIG_NOTE_MODE) {
					n->mode  = value;
					n->phase = CTERM_BIG_NOTE_CHECK0;
					break;
				}
				if (n->phase == CTERM_BIG_NOTE_READ0) {
					if (n->index == 0)
						n->from = value;
					else if (n->index == 1)
						n->sustain = value;
					else {
						n->off = value;
						if (st_big_number_signed(n->from) < 0)
							n->from = 0;
						n->phase = CTERM_BIG_NOTE_SOUND0;
						break;
					}
				}
				else
					switch (n->index) {
						case 0:
							n->from = value;
							break;
						case 1:
							n->to = value;
							break;
						case 2:
							n->step = st_big_number_signed(value) < 1 ? 1 : value;
							break;
						case 3:
							n->delay = value;
							break;
						case 4:
							n->sustain = value;
							break;
						case 5:
							n->off   = value;
							n->phase = CTERM_BIG_NOTE_SELECT;
							break;
						default:
							return IO_ERROR_PROTOCOL;
					}
				++n->index;
				break;
			}
			case CTERM_BIG_NOTE_CHECK0:
				n->index = 0;
				n->phase = n->mode == 0 ? CTERM_BIG_NOTE_READ0 : CTERM_BIG_NOTE_CHECK1;
				break;
			case CTERM_BIG_NOTE_SOUND0:
				status = cterm_big_audio_frequency(b, n->from);
				if (status == 0)
					status = delay(b, n->sustain, CTERM_BIG_NOTE_STOP);
				n->single = true;
				break;
			case CTERM_BIG_NOTE_CHECK1:
				n->index  = 0;
				n->single = false;
				n->phase  = n->mode == 1 ? CTERM_BIG_NOTE_READ1 : CTERM_BIG_NOTE_DONE;
				break;
			case CTERM_BIG_NOTE_SELECT:
				n->ascending = st_big_number_signed(n->from) <= st_big_number_signed(n->to);
				n->current   = n->from;
				n->phase     = CTERM_BIG_NOTE_RAMP;
				break;
			case CTERM_BIG_NOTE_RAMP:
			{
				bool enter = n->ascending
				                 ? st_big_number_signed(n->current) < st_big_number_signed(n->to)
				                 : st_big_number_signed(n->current) > st_big_number_signed(n->to);
				if (!enter) {
					n->phase = CTERM_BIG_NOTE_TAIL;
					break;
				}
				if (*budget == 0)
					return IO_ERROR_IN_PROGRESS;
				--*budget;
				status = cterm_big_audio_frequency(b, n->current);
				if (status == 0)
					status = delay(b, n->delay, CTERM_BIG_NOTE_UPDATE);
				break;
			}
			case CTERM_BIG_NOTE_UPDATE:
				n->current =
				    (uint16_t)(n->ascending ? n->current + n->step : n->current - n->step);
				n->phase = CTERM_BIG_NOTE_RAMP;
				break;
			case CTERM_BIG_NOTE_TAIL:
				status = delay(b, n->sustain, CTERM_BIG_NOTE_STOP);
				break;
			case CTERM_BIG_NOTE_STOP:
				if (n->off != 0)
					status = cterm_big_audio_stop(b);
				if (status != 0)
					break;
				if (n->single)
					n->phase = CTERM_BIG_NOTE_CHECK1;
				else if (n->ascending) {
					/* Original ascending tail reloads TO into FROM before its
				 * following descending test, making that test false. No F(TO). */
					n->from  = n->to;
					n->phase = CTERM_BIG_NOTE_DONE;
				}
				else
					n->phase = CTERM_BIG_NOTE_DONE;
				break;
			case CTERM_BIG_NOTE_WAIT:
			{
				uint64_t ns;
				status = sample(b, &ns);
				if (status != 0)
					break;
				if (ns < n->deadline)
					return IO_ERROR_IN_PROGRESS;
				n->phase = n->after_wait;
				break;
			}
			case CTERM_BIG_NOTE_DONE:
				status = cterm_big_music_clock(b, true);
				if (status != 0)
					return status;
				n->active = false;
				n->phase  = CTERM_BIG_NOTE_OFF;
				*complete = true;
				return 0;
			default:
				return IO_ERROR_PROTOCOL;
		}
		if (status != 0)
			return status;
	}
}
