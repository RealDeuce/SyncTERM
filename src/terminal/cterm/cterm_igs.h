/* IGS add-on input at the same pre-emulation boundary as RIP. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_H_
#define CTERM_IGS_H_

#include "st_graphics_parser.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cterminal;
struct cterm_igs {
	struct cterminal *terminal;
	st_graphics_parser_t *parser;
	unsigned resolution;
	uint32_t width, height;
	/* Local monotonic milliseconds; injectable for deadline tests. */
	int (*now_ms)(uint64_t *);
	uint64_t input_deadline;
	bool feeding, reset_pending;
	uint8_t warned[32];
};

int cterm_igs_create(struct cterminal *);
void cterm_igs_destroy(struct cterm_igs *);
void cterm_igs_reset(struct cterm_igs *);
int cterm_igs_feed(struct cterm_igs *, const void *, size_t, size_t *);
int cterm_igs_finish(struct cterm_igs *);
int cterm_igs_poll(struct cterm_igs *);

#endif
