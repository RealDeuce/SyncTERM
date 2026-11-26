/* Typed comparison/reduction stage for the RIP3 angle preprocessor. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_CONDITION_H_
#define ST_RIP3_CONDITION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_condition_bytes {
	const uint8_t *bytes;
	size_t         length;
};

enum {
	ST_RIP3_CONDITION_OPERAND_LIMIT      = 255,
	ST_RIP3_CONDITION_ENCODED_CAPACITY   = ST_RIP3_CONDITION_OPERAND_LIMIT * 4,
	ST_RIP3_CONDITION_REFERENCE_CAPACITY = ST_RIP3_CONDITION_ENCODED_CAPACITY + 4,
};

enum st_rip3_condition_operator {
	ST_RIP3_CONDITION_EQUAL,
	ST_RIP3_CONDITION_NOT_EQUAL,
	ST_RIP3_CONDITION_LESS,
	ST_RIP3_CONDITION_LESS_EQUAL,
	ST_RIP3_CONDITION_GREATER,
	ST_RIP3_CONDITION_GREATER_EQUAL,
};

enum st_rip3_condition_kind {
	ST_RIP3_CONDITION_BOOLEAN,
	ST_RIP3_CONDITION_COMPARISON,
	ST_RIP3_CONDITION_AND,
	ST_RIP3_CONDITION_OR,
	ST_RIP3_CONDITION_NOT,
	ST_RIP3_CONDITION_OPEN,
	ST_RIP3_CONDITION_CLOSE,
};

struct st_rip3_condition_comparison {
	enum st_rip3_condition_operator operation;
	/* Parsed variable/template reference and already unquoted/trimmed literal.
	 * Their source spelling/grammar is the enclosing lexer's responsibility. */
	struct st_rip3_condition_bytes reference, literal;
};

struct st_rip3_condition_token {
	enum st_rip3_condition_kind kind;

	union {
		bool                                boolean;
		struct st_rip3_condition_comparison comparison;
	} value;
};

struct st_rip3_condition_source {
	void *context;
	/* Called once per comparison, in source order, with no Boolean short
	 * circuit. Returned bytes are borrowed until the next call. A failed
	 * lookup/evaluation makes that comparison false, including !=. No returned
	 * bytes are recursively expanded. Callback must not reenter the evaluator
	 * or mutate input tokens/bindings. One session thread owns invocation. */
	int (*value)(void *context, const struct st_rip3_condition_bytes *reference,
	    struct st_rip3_condition_bytes *result);
};

/* Pure helpers below the parser's nonfatal boundary, not stream/session entry
 * points. Numeric values are ASCII decimal with optional leading minus and at
 * least one digit. Only when both qualify, compare signed wrapped 32-bit values;
 * otherwise compare byte strings with ASCII case folding, high bytes unchanged.
 * C-string NUL ends each effective value, even inside an explicit borrowed span.
 * Quotes/whitespace are not removed here. Failures preserve result. */
int st_rip3_condition_compare(bool *result, enum st_rip3_condition_operator operation,
    const struct st_rip3_condition_bytes *left, const struct st_rip3_condition_bytes *right);
/* Check typed operand/operator/group structure without invoking a provider or
 * allocating. Inactive parents use this stage without evaluating comparisons.
 * It does not parse variable-reference spelling or the enclosing IF structure. */
int st_rip3_condition_validate(const struct st_rip3_condition_token *tokens, size_t count);
/* Validate the whole expression and prepare scratch space before any provider
 * call. Resolve all comparisons left to right, then repeatedly reduce adjacent
 * Boolean/AND-or-OR/Boolean triples left to right, NOT/Boolean pairs, and grouped
 * single Booleans in that order. Equal AND/OR precedence and late NOT are
 * intentional RIPtel behavior. A missing/failed provider gives false comparisons.
 * Empty/malformed input and resource failures preserve result. Tokens and
 * bindings remain borrowed/unchanged; result must not alias them. Parser callers
 * absorb helper statuses. */
int st_rip3_condition_evaluate(bool *result, const struct st_rip3_condition_token *tokens, size_t count,
    const struct st_rip3_condition_source *source);

#endif
