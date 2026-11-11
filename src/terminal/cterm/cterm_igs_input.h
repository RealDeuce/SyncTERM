/* IGS owner-side raw input and mouse state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_INPUT_H
#define CTERM_IGS_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterm_igs;
struct st_console_event;
/* Preserve entry kind and encoding while N or a mouse hold owns input. */
enum cterm_igs_key_kind { CTERM_IGS_KEY_NORMAL, CTERM_IGS_KEY_CODEPAGE,
	CTERM_IGS_KEY_TEXT, CTERM_IGS_KEY_DOORWAY };
struct cterm_igs_key {
	enum cterm_igs_key_kind kind;
	uint32_t value, modifiers;
	int local;
};

enum { CTERM_IGS_MACRO_BYTES = 80, CTERM_IGS_INPUT_KEYS = 64 };
struct cterm_igs_input {
	uint8_t enabled, length, cr, bytes[CTERM_IGS_MACRO_BYTES];
	bool configuring, loading;
	int operation;
	size_t received;
	uint64_t deadline;
	/* Terminal-owner state, never accessed by a worker. Native TOS bits:
	 * left=1/right=2. A hold ends only at an all-buttons-up sample. */
	struct cterm_igs_key keys[CTERM_IGS_INPUT_KEYS];
	size_t key_first, key_count;
	bool replaying;
	unsigned buttons;
	bool held, suspended, sending, suppressed;
};
bool cterm_igs_buffer_key(struct cterm_igs *, const struct cterm_igs_key *);
int cterm_igs_take_key(struct cterm_igs *);
int cterm_igs_forward_keys(struct cterm_igs *);
int cterm_igs_macro_length(int value);
int cterm_igs_macro_load(struct cterm_igs *, int length);
int cterm_igs_macro_bytes(struct cterm_igs *, const uint8_t *, size_t, size_t *accepted);
void cterm_igs_input_cancel(struct cterm_igs *);
int cterm_igs_input_activate(struct cterm_igs *);
bool cterm_igs_mouse(struct cterm_igs *, const struct st_console_event *);
void cterm_igs_mouse_sync(struct cterm_igs *, uint32_t buttons);
#endif
