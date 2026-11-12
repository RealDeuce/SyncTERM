/* Native key identity retained while a graphics command owns local input. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_KEY_H_
#define CTERM_KEY_H_
#include <stdint.h>
struct cterminal;
enum cterm_key_kind { CTERM_KEY_NORMAL, CTERM_KEY_CODEPAGE,
	CTERM_KEY_TEXT, CTERM_KEY_DOORWAY };
struct cterm_key {
	enum cterm_key_kind kind;
	uint32_t value, modifiers;
	int local;
};
/* Caller suppresses recapture and checks deferred reset after each event. */
int cterm_key_replay(struct cterminal *, const struct cterm_key *);
#endif
