/* Shared N/X9 storage and owner-driven buffered music playback. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_MUSIC_H
#define CTERM_IGS_MUSIC_H
#include "cterm_igs.h"

enum { CTERM_IGS_MUSIC_BYTES = 10001, CTERM_IGS_MUSIC_KEYS = 64 };
struct cterm_igs_music {
	/* Independent lengths share one persistent byte buffer. */
	uint8_t bytes[CTERM_IGS_MUSIC_BYTES];
	int count;
	uint32_t script_length;
	uint8_t midi_enabled;
	/* Only the terminal owner accesses acquisition/playback state. */
	bool command, loading, playing, midi, delaying, flow, replaying;
	bool midi_output, checked;
	int operation, local_count, index, end;
	size_t received, key_first, key_count;
	struct cterm_igs_key keys[CTERM_IGS_MUSIC_KEYS];
	uint64_t receive_deadline;
};
int cterm_igs_music_reset(struct cterm_igs *);
int cterm_igs_music_cancel(struct cterm_igs *);
void cterm_igs_music_stop(struct cterm_igs *);
void cterm_igs_music_prefix(struct cterm_igs *, int operation);
int cterm_igs_music_load(struct cterm_igs *, int count);
int cterm_igs_music_bytes(struct cterm_igs *, const uint8_t *, size_t,
	size_t *accepted, bool *end);
int cterm_igs_music_execute(struct cterm_igs *, int first, int end);
int cterm_igs_music_resume(struct cterm_igs *);
bool cterm_igs_music_key(struct cterm_igs *, const struct cterm_igs_key *);
int cterm_igs_flow(struct cterm_igs *, bool on);
#endif
