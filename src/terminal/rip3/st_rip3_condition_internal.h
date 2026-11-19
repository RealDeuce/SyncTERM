/* Source grammar services private to the angle preprocessor and its tests. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_CONDITION_INTERNAL_H_
#define ST_RIP3_CONDITION_INTERNAL_H_

#include "st_rip3_condition.h"

struct st_rip3_condition_program;

struct st_rip3_condition_provider {
	void *context;
	/* Exactly $& + decoded L + $, preserving L's case/bytes. Complete parser
	 * validation precedes invocation; normal dispatch/conditional permissions
	 * remain the provider's responsibility. No recursive value expansion. */
	int (*reference)(void *context, const struct st_rip3_condition_bytes *reference,
		struct st_rip3_condition_bytes *result);
	int (*template)(void *context, unsigned number, struct st_rip3_condition_bytes *result);
};

/* Parse bytes AFTER IF/ELSEIF, including a real closing >>. NUL terminates input;
 * consumed includes >> but not following bytes. No operand source-byte cap;
 * decoded ordinary left/RHS limits are 255 bytes, checked before RHS trimming.
 * Validation is allocation/provider-free and serves inactive parent branches.
 * Program creation copies all decoded operands, with no source-buffer lifetime
 * dependency. Every failure preserves output pointers/counts. These statuses
 * must be consumed inside the owning void RIP preprocessor, never returned
 * as terminal/session processing errors. */
int st_rip3_condition_source_validate(const uint8_t *source, size_t length, size_t *consumed);
int st_rip3_condition_program_parse(struct st_rip3_condition_program **result,
	const uint8_t *source, size_t length, size_t *consumed);
void st_rip3_condition_program_destroy(struct st_rip3_condition_program *program);
/* Typed reduction resolves all comparisons in source order, without Boolean
 * short circuit. Provider format/lookup failure, unavailable binding or result
 * longer than 255 bytes makes that comparison false, including !=. Empty is a
 * valid value. Returned spans are borrowed only until the next provider call.
 * Full syntax/preparation precedes all calls; errors preserve result. */
int st_rip3_condition_program_evaluate(bool *result,
	const struct st_rip3_condition_program *program, const struct st_rip3_condition_provider *provider);

#endif
