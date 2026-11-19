/* Source grammar services private to the angle preprocessor and its tests. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_CONDITION_INTERNAL_H_
#define ST_RIP3_CONDITION_INTERNAL_H_

#include "st_rip3_condition.h"

struct st_rip3_condition_program;
struct st_rip3_condition_evaluation;

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

enum st_rip3_condition_evaluation_state {
	ST_RIP3_CONDITION_RUNNING,
	ST_RIP3_CONDITION_VALUE,
	ST_RIP3_CONDITION_COMPLETE,
	ST_RIP3_CONDITION_FAILED,
};

/* Own a prepared token/operand copy, independent of the parsed program and
 * caller source. No provider has run when this factory fails. The preprocessor
 * owns this continuation across any user dialog, input delay or source yield. */
int st_rip3_condition_evaluation_create(struct st_rip3_condition_evaluation **result,
	const struct st_rip3_condition_program *program);
void st_rip3_condition_evaluation_destroy(struct st_rip3_condition_evaluation *evaluation);
/* Examine at most budget prepared tokens. Stop at each valid provider request;
 * malformed synthesized references resolve false without requesting a provider.
 * No Boolean short circuit or callback invocation. Processing errors are owned
 * here and become FAILED, never a returned terminal/session error. */
void st_rip3_condition_evaluation_process(struct st_rip3_condition_evaluation *evaluation,
	size_t budget);
enum st_rip3_condition_evaluation_state st_rip3_condition_evaluation_state(
	const struct st_rip3_condition_evaluation *evaluation);
/* Borrowed request valid only in VALUE state, until supplied/destroyed. Its
 * ordinary spelling is exactly $&L$; a two-byte '?' + numeric-slot span denotes
 * the separate template provider. A waiting owner may inspect it repeatedly. */
const struct st_rip3_condition_bytes *st_rip3_condition_evaluation_request(
	const struct st_rip3_condition_evaluation *evaluation);
/* Complete only the current request. Failed/unavailable/oversized/invalid value
 * spans give false even for !=. Successful empty spans remain valid. Compare
 * immediately, so borrowed provider scratch can be reused on return. Supply
 * never starts the next lookup. Calls outside VALUE are ignored. */
void st_rip3_condition_evaluation_supply(struct st_rip3_condition_evaluation *evaluation,
	const struct st_rip3_condition_bytes *value, int status);
/* Completion accessor; leave result unchanged unless COMPLETE. */
bool st_rip3_condition_evaluation_result(const struct st_rip3_condition_evaluation *evaluation,
	bool *result);

/* Allocation-free reduction of prepared Boolean/operator/group tokens. Mutates
 * scratch only. Private helper; owning void processors absorb any status. */
int st_rip3_condition_reduce(bool *result, struct st_rip3_condition_token *work, size_t count);

#endif
