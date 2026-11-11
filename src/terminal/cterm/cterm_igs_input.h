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

enum { CTERM_IGS_MACRO_BYTES = 80, CTERM_IGS_INPUT_KEYS = 64, CTERM_IGS_ZONES = 48 };
struct cterm_igs_zone {
	int16_t rectangle[4];
	uint8_t length, bytes[CTERM_IGS_MACRO_BYTES];
};
struct cterm_igs_input {
	uint8_t enabled, length, cr, bytes[CTERM_IGS_MACRO_BYTES];
	bool configuring, loading;
	int operation;
	size_t received;
	uint64_t deadline;
	struct cterm_igs_zone *zones;
	int zone, zone_loopback;
	bool zone_configuring, zone_loading;
	size_t zone_received;
	uint64_t zone_deadline;
	/* Terminal-owner state, never accessed by a worker. Native TOS bits:
	 * left=1/right=2. X3 waits for all buttons up; X6 waits for !=1. */
	struct cterm_igs_key keys[CTERM_IGS_INPUT_KEYS];
	size_t key_first, key_count;
	bool replaying;
	uint8_t left_mode;
	bool left_held, left_configuring;
	unsigned buttons;
	bool held, suspended, sending, suppressed;
};
bool cterm_igs_buffer_key(struct cterm_igs *, const struct cterm_igs_key *);
int cterm_igs_take_key(struct cterm_igs *);
int cterm_igs_forward_keys(struct cterm_igs *);
int cterm_igs_input_reset(struct cterm_igs *);
void cterm_igs_input_cancel_reader(struct cterm_igs *);
int cterm_igs_zone_length(int value);
int cterm_igs_zone_control(struct cterm_igs *, int zone);
int cterm_igs_zone_load(struct cterm_igs *, int length);
int cterm_igs_zone_bytes(struct cterm_igs *, const uint8_t *, size_t, size_t *accepted);
int cterm_igs_macro_length(int value);
int cterm_igs_macro_load(struct cterm_igs *, int length);
int cterm_igs_macro_bytes(struct cterm_igs *, const uint8_t *, size_t, size_t *accepted);
void cterm_igs_input_cancel(struct cterm_igs *);
int cterm_igs_input_activate(struct cterm_igs *);
int cterm_igs_left_finish(struct cterm_igs *);
bool cterm_igs_mouse(struct cterm_igs *, const struct st_console_event *);
void cterm_igs_mouse_sync(struct cterm_igs *, uint32_t buttons);
#endif
