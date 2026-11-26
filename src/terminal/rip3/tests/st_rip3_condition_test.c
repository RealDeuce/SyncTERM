/* Independent numeric/byte comparisons and original reduction-order vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_condition.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

#define T     {.kind = ST_RIP3_CONDITION_BOOLEAN, .value.boolean = true}
#define F     {.kind = ST_RIP3_CONDITION_BOOLEAN, .value.boolean = false}
#define AND   {.kind = ST_RIP3_CONDITION_AND}
#define OR    {.kind = ST_RIP3_CONDITION_OR}
#define NOT   {.kind = ST_RIP3_CONDITION_NOT}
#define OPEN  {.kind = ST_RIP3_CONDITION_OPEN}
#define CLOSE {.kind = ST_RIP3_CONDITION_CLOSE}

extern bool   st_rip3_condition_test_fail;
extern size_t st_rip3_condition_test_allocations;

static struct st_rip3_condition_bytes
bytes(const char *text)
{
	return (struct st_rip3_condition_bytes) {(const uint8_t *)text, strlen(text)};
}

static int
comparisons(void)
{
	static const struct {
		const char *left, *right;
		int         order;
	} cases[] = {
	    {"2",                    "10",      -1},
	    {"02",                   "2",       0 },
	    {"-2",                   "-10",     1 },
	    {"-0",                   "000",     0 },
	    {"2147483648",           "0",       -1},
	    {"4294967296",           "0",       0 },
	    {"-4294967295",          "1",       0 },
	    {"18446744073709551617", "1",       0 },
	    {"Alpha",                "aLPHA",   0 },
	    {"alpha",                "BETA",    -1},
	    {"Z",                    "a",       1 },
	    {"+2",                   "10",      -1},
	    {"2 ",                   "10",      1 },
	    {" 2",                   "10",      -1},
	    {"",                     "0",       -1},
	    {"",                     "",        0 },
	    {"-",                    "0",       -1},
	    {"$SHIFT$",              "$shift$", 0 },
	    {"2.0",                  "10",      1 },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct st_rip3_condition_bytes a = bytes(cases[i].left), b = bytes(cases[i].right);
		const bool expected[] = {cases[i].order == 0, cases[i].order != 0, cases[i].order < 0,
		    cases[i].order <= 0, cases[i].order > 0, cases[i].order >= 0};
		for (unsigned operation = 0; operation < 6; operation++) {
			bool result = !expected[operation];
			CHECK(
			    st_rip3_condition_compare(&result, (enum st_rip3_condition_operator)operation, &a, &b)
			        == 0
			    && result == expected[operation]);
		}
	}
	const uint8_t                  cp437[] = {0x82}, other[] = {0x90}, utf8[] = {0xc3, 0xa9};
	struct st_rip3_condition_bytes a = {cp437, 1}, b = {other, 1};
	bool                           result = false;
	CHECK(st_rip3_condition_compare(&result, ST_RIP3_CONDITION_LESS, &a, &b) == 0 && result);
	b = (struct st_rip3_condition_bytes) {utf8, 2};
	CHECK(st_rip3_condition_compare(&result, ST_RIP3_CONDITION_LESS, &a, &b) == 0 && result);
	const uint8_t nul[] = {'2', 0, '9'};
	a                   = (struct st_rip3_condition_bytes) {nul, sizeof(nul)};
	b                   = bytes("02");
	CHECK(st_rip3_condition_compare(&result, ST_RIP3_CONDITION_EQUAL, &a, &b) == 0 && result);
	a = (struct st_rip3_condition_bytes) {NULL, 0};
	b = bytes("");
	CHECK(st_rip3_condition_compare(&result, ST_RIP3_CONDITION_EQUAL, &a, &b) == 0 && result);
	CHECK(st_rip3_condition_compare(&result, (enum st_rip3_condition_operator)99, &a, &b)
	          == IO_ERROR_OUT_OF_RANGE
	      && result);
	a.length = 1;
	CHECK(st_rip3_condition_compare(&result, ST_RIP3_CONDITION_EQUAL, &a, &b) == IO_ERROR_INVALID_ARGUMENT
	      && result);
	CHECK(st_rip3_condition_compare(NULL, ST_RIP3_CONDITION_EQUAL, &b, &b) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

static int
reductions(void)
{
	/* Parsed equivalents of the three published N=1 precedence examples. */
	static const struct st_rip3_condition_token cases[][11] = {
	    {T, OR, F, AND, F},
	    {T, OR, OPEN, F, AND, F, CLOSE},
	    {NOT, T, AND, F},
	    {NOT, T, OR, T},
	    {OPEN, NOT, T, CLOSE, OR, T},
	    {NOT, NOT, T, AND, F},
	    {NOT, OPEN, T, AND, F, CLOSE},
	    {T, OR, F, AND, T, OR, F, AND, F},
	    {OPEN, OPEN, T, CLOSE, CLOSE},
	    {NOT, NOT, NOT, T},
	    {NOT, T, OR, F, AND, F},
	    {NOT, NOT, F, AND, NOT, T},
	    {NOT, OPEN, F, CLOSE, OR, T},
	};
	static const size_t lengths[]  = {5, 7, 4, 4, 6, 5, 6, 9, 5, 4, 6, 6, 6};
	static const bool   expected[] = {false, true, true, false, true, false, true, false, true, false, true,
	    true, false};
	for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		struct st_rip3_condition_token saved[11];
		memcpy(saved, cases[i], sizeof(saved));
		bool result = !expected[i];
		CHECK(st_rip3_condition_validate(cases[i], lengths[i]) == 0);
		CHECK(
		    st_rip3_condition_evaluate(&result, cases[i], lengths[i], NULL) == 0 && result == expected[i]);
		CHECK(memcmp(saved, cases[i], sizeof(saved)) == 0);
	}

	/* Iterative validation/reduction does not consume the native call stack
	 * for each pair of parentheses. */
	enum {
		DEPTH = 128
	};
	struct st_rip3_condition_token nested[2 * DEPTH + 1];
	for (size_t i = 0; i < DEPTH; i++)
		nested[i] = (struct st_rip3_condition_token)OPEN;
	nested[DEPTH] = (struct st_rip3_condition_token)T;
	for (size_t i = DEPTH + 1; i < 2 * DEPTH + 1; i++)
		nested[i] = (struct st_rip3_condition_token)CLOSE;
	bool result = false;
	CHECK(st_rip3_condition_evaluate(&result, nested, 2 * DEPTH + 1, NULL) == 0 && result);
	return 0;
}

struct provider {
	unsigned calls;
	bool     wrong;
	uint8_t  value[8];
};

static int
lookup(void *context, const struct st_rip3_condition_bytes *reference, struct st_rip3_condition_bytes *result)
{
	struct provider *p = context;
	p->calls++;
	if (reference->length != 1 || reference->bytes[0] != (uint8_t)('A' + p->calls - 1))
		p->wrong = true;
	if (p->calls == 2)
		return IO_ERROR_NOT_FOUND;
	/* Reuse one scratch buffer: values must be compared before the next call. */
	p->value[0] = p->calls == 1 ? '1' : '0';
	*result     = (struct st_rip3_condition_bytes) {p->value, 1};
	return 0;
}

static int
providers(void)
{
	struct st_rip3_condition_token tokens[5] = {
	    {.kind = ST_RIP3_CONDITION_COMPARISON},
	    OR,
	    {.kind = ST_RIP3_CONDITION_COMPARISON},
	    AND,
	    {.kind = ST_RIP3_CONDITION_COMPARISON},
	};
	for (size_t i = 0; i < 3; i++) {
		tokens[2 * i].value.comparison = (struct st_rip3_condition_comparison) {ST_RIP3_CONDITION_EQUAL,
		    bytes(i == 0   ? "A"
		          : i == 1 ? "B"
		                   : "C"),
		    bytes("1")};
	}
	/* A=true, B lookup fails, C=false: (true OR false) AND false = false.
	 * Neither true OR nor false AND may skip an operand lookup. */
	struct provider                       p      = {0};
	const struct st_rip3_condition_source source = {&p, lookup};
	bool                                  result = true;
	CHECK(st_rip3_condition_validate(tokens, 5) == 0 && p.calls == 0);
	CHECK(st_rip3_condition_evaluate(&result, tokens, 5, &source) == 0 && !result && p.calls == 3 && !p.wrong);
	p                                  = (struct provider) {0};
	tokens[1].kind                     = ST_RIP3_CONDITION_AND;
	tokens[3].kind                     = ST_RIP3_CONDITION_OR;
	tokens[4].value.comparison.literal = bytes("0");
	CHECK(st_rip3_condition_evaluate(&result, tokens, 5, &source) == 0 && result && p.calls == 3 && !p.wrong);
	/* Failed comparisons are false even for !=; missing provider is also a
	 * value failure, not a terminal/session error. */
	tokens[0].value.comparison.operation = ST_RIP3_CONDITION_NOT_EQUAL;
	CHECK(st_rip3_condition_evaluate(&result, tokens, 1, NULL) == 0 && !result);
	p                                    = (struct provider) {.calls = 1};
	tokens[2].value.comparison.operation = ST_RIP3_CONDITION_NOT_EQUAL;
	CHECK(st_rip3_condition_evaluate(&result, tokens + 2, 1, &source) == 0 && !result && p.calls == 2
	      && !p.wrong);
	/* Preflight the complete structure before calling any provider. */
	p              = (struct provider) {0};
	tokens[4].kind = ST_RIP3_CONDITION_AND;
	result         = true;
	CHECK(st_rip3_condition_evaluate(&result, tokens, 5, &source) == IO_ERROR_INVALID_FORMAT && result
	      && p.calls == 0);
	tokens[4].kind                     = ST_RIP3_CONDITION_COMPARISON;
	st_rip3_condition_test_fail        = true;
	st_rip3_condition_test_allocations = 0;
	CHECK(st_rip3_condition_evaluate(&result, tokens, 5, &source) == IO_ERROR_OUT_OF_MEMORY && result
	      && p.calls == 0 && st_rip3_condition_test_allocations == 1);
	st_rip3_condition_test_fail        = false;
	tokens[4].value.comparison.literal = (struct st_rip3_condition_bytes) {NULL, 1};
	st_rip3_condition_test_allocations = 0;
	CHECK(st_rip3_condition_evaluate(&result, tokens, 5, &source) == IO_ERROR_INVALID_ARGUMENT && result
	      && p.calls == 0 && st_rip3_condition_test_allocations == 0);
	CHECK(st_rip3_condition_evaluate(&result, tokens, 1, &source) == 0 && !result && p.calls == 1 && !p.wrong);
	return 0;
}

static int
failures(void)
{
	static const struct st_rip3_condition_token bad[][5] = {
	    {T, F},
	    {AND, T},
	    {T, OR},
	    {OPEN, CLOSE},
	    {OPEN, T},
	    {T, CLOSE},
	    {T, NOT, F},
	    {NOT},
	    {T, OPEN, F, CLOSE},
	    {OPEN, T, AND, CLOSE},
	};
	static const size_t lengths[] = {2, 2, 2, 2, 2, 2, 3, 1, 4, 4};
	bool                result    = true;
	for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
		CHECK(st_rip3_condition_evaluate(&result, bad[i], lengths[i], NULL) == IO_ERROR_INVALID_FORMAT
		      && result);
	CHECK(st_rip3_condition_evaluate(&result, NULL, 0, NULL) == IO_ERROR_INVALID_FORMAT && result);
	CHECK(st_rip3_condition_evaluate(&result, NULL, 1, NULL) == IO_ERROR_INVALID_ARGUMENT && result);
	CHECK(st_rip3_condition_evaluate(&result, bad[0], SIZE_MAX, NULL) == IO_ERROR_OUT_OF_RANGE && result);
	CHECK(st_rip3_condition_evaluate(NULL, bad[0], 1, NULL) == IO_ERROR_INVALID_ARGUMENT);
	return 0;
}

int
main(void)
{
	CHECK(comparisons() == 0 && reductions() == 0 && providers() == 0 && failures() == 0);
	return 0;
}
