/* X9 shared storage/acquisition and serialized script-source state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_SCRIPT_H
#define CTERM_IGS_SCRIPT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterm_igs;
struct cterm_igs_script {
	bool loading, lookahead, active;
	bool configuring, operand_ready, starting, ready, header, chained, pending_g;
	bool paused, cooperative, text, disabled, post;
	/* Only the enclosing dispatch boundary is saved. & operands, index,
	 * random state and the shared bytes remain live across the call. */
	bool return_loop, return_flow, return_chained;
	int operation;
	unsigned pipes;
	size_t received, cursor, parsed;
	uint64_t deadline;
};
int cterm_igs_script_load(struct cterm_igs *);
int cterm_igs_script_bytes(struct cterm_igs *, const uint8_t *, size_t,
	size_t *accepted, bool *end);
void cterm_igs_script_clear(struct cterm_igs *);
int cterm_igs_script_cancel(struct cterm_igs *);
#endif
