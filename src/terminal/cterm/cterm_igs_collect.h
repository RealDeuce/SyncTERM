/* < keyboard/zone collection; the reply belongs to the enclosing chain. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_COLLECT_H
#define CTERM_IGS_COLLECT_H
#include "cterm_igs_mouse.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterm_igs;
struct cterm_igs_collect {
	bool configuring, active, ready, pending, release;
	uint8_t append_cr, bytes[129];
	size_t count;
	int type, options, press_x, press_y, error;
	struct cterm_igs_pointer pointer;
};
int cterm_igs_collect_begin(struct cterm_igs *, int type, int options);
int cterm_igs_collect_resume(struct cterm_igs *);
int cterm_igs_collect_mouse(struct cterm_igs *, int x, int y, unsigned buttons);
int cterm_igs_collect_send(struct cterm_igs *);
int cterm_igs_collect_cancel(struct cterm_igs *);
#endif
