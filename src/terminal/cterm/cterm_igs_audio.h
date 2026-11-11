/* Session-owned IGS patch banks and shared GIST/console PSG. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_AUDIO_H
#define CTERM_IGS_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"
#include "st_igs_gist.h"
#include "st_igs_psg.h"
#include "st_threads.h"

struct cterm_igs;
struct cterm_igs_audio {
	/* The terminal owner alone attaches/destroys stream and sets the local
	 * wall-time anchor. mutex protects all model state used by the consumer.
	 * Never call an audio/runtime API while holding this mutex: consumer
	 * lock order is runtime/mixer/stream -> this model. */
	audio_stream_t *stream;
	st_mutex_t mutex;
	struct st_igs_gist gist;
	struct st_igs_psg psg;
	uint16_t patches[ST_IGS_GIST_PATCHES][ST_IGS_GIST_WORDS];
	uint16_t saved[ST_IGS_GIST_PATCHES][ST_IGS_GIST_WORDS];
	int multiplier;
	unsigned envelope_credit, console_credit, local_fraction;
	uint64_t local_ms;
	bool local_anchor_needed;
	bool running, console_allowed, warned;
	unsigned console_pending;
};
int cterm_igs_audio_effect(struct cterm_igs *, const int *, size_t count);
/* Shared numeric/byte-note helper. A positive local delay suspends its owner;
 * the owner consumes one key or reaches the deadline, then calls end. */
int cterm_igs_audio_note_begin(struct cterm_igs *, const int values[6]);
int cterm_igs_audio_note_end(struct cterm_igs *);
int cterm_igs_audio_console(struct cterm_igs *, bool click);
int cterm_igs_audio_poll(struct cterm_igs *);
void cterm_igs_audio_stop(struct cterm_igs *);
void cterm_igs_audio_destroy(struct cterm_igs *);

#endif
