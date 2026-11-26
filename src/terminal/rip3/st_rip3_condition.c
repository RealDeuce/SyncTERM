/* RIPtel comparison values and staged Boolean reduction. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_condition_internal.h"

static bool
valid_bytes(const struct st_rip3_condition_bytes *value)
{
	return value != NULL && (value->bytes != NULL || value->length == 0);
}

static struct st_rip3_condition_bytes
effective(const struct st_rip3_condition_bytes *value)
{
	struct st_rip3_condition_bytes result = *value;
	if (result.length != 0) {
		const uint8_t *end = memchr(result.bytes, 0, result.length);
		if (end != NULL)
			result.length = (size_t)(end - result.bytes);
	}
	return result;
}

static bool
numeric(const struct st_rip3_condition_bytes *text, int64_t *result)
{
	if (text->length == 0)
		return false;
	bool   negative = text->bytes[0] == '-';
	size_t at       = negative ? 1 : 0;
	if (at == text->length)
		return false;
	uint32_t value = 0;
	for (; at < text->length; at++) {
		uint8_t ch = text->bytes[at];
		if (ch < '0' || ch > '9')
			return false;
		value = value * UINT32_C(10) + (uint32_t)(ch - '0');
	}
	if (negative)
		value = UINT32_C(0) - value;
	*result = value <= INT32_MAX ? (int64_t)value : (int64_t)value - INT64_C(4294967296);
	return true;
}

static uint8_t
fold(uint8_t ch)
{
	return ch >= 'A' && ch <= 'Z' ? (uint8_t)(ch + ('a' - 'A')) : ch;
}

int
st_rip3_condition_compare(bool *result, enum st_rip3_condition_operator operation,
    const struct st_rip3_condition_bytes *left, const struct st_rip3_condition_bytes *right)
{
	if (result == NULL || !valid_bytes(left) || !valid_bytes(right))
		return IO_ERROR_INVALID_ARGUMENT;
	if ((unsigned)operation > ST_RIP3_CONDITION_GREATER_EQUAL)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_condition_bytes a = effective(left), b = effective(right);
	int64_t                        x, y;
	int                            order = 0;
	if (numeric(&a, &x) && numeric(&b, &y))
		order = x < y ? -1 : x > y;
	else {
		size_t common = a.length < b.length ? a.length : b.length;
		for (size_t i = 0; i < common; i++) {
			uint8_t p = fold(a.bytes[i]), q = fold(b.bytes[i]);
			if (p != q) {
				order = p < q ? -1 : 1;
				break;
			}
		}
		if (order == 0)
			order = a.length < b.length ? -1 : a.length > b.length;
	}
	bool value = false;
	switch (operation) {
		case ST_RIP3_CONDITION_EQUAL:
			value = order == 0;
			break;
		case ST_RIP3_CONDITION_NOT_EQUAL:
			value = order != 0;
			break;
		case ST_RIP3_CONDITION_LESS:
			value = order < 0;
			break;
		case ST_RIP3_CONDITION_LESS_EQUAL:
			value = order <= 0;
			break;
		case ST_RIP3_CONDITION_GREATER:
			value = order > 0;
			break;
		case ST_RIP3_CONDITION_GREATER_EQUAL:
			value = order >= 0;
			break;
	}
	*result = value;
	return 0;
}

int
st_rip3_condition_validate(const struct st_rip3_condition_token *tokens, size_t count)
{
	if (count == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (tokens == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (count > SIZE_MAX / sizeof(*tokens))
		return IO_ERROR_OUT_OF_RANGE;
	bool   operand = true;
	size_t depth   = 0;
	for (size_t i = 0; i < count; i++) {
		const struct st_rip3_condition_token *token = &tokens[i];
		switch (token->kind) {
			case ST_RIP3_CONDITION_NOT:
				if (!operand)
					return IO_ERROR_INVALID_FORMAT;
				break;
			case ST_RIP3_CONDITION_OPEN:
				if (!operand)
					return IO_ERROR_INVALID_FORMAT;
				depth++;
				break;
			case ST_RIP3_CONDITION_CLOSE:
				if (operand || depth == 0)
					return IO_ERROR_INVALID_FORMAT;
				depth--;
				break;
			case ST_RIP3_CONDITION_AND:
			case ST_RIP3_CONDITION_OR:
				if (operand)
					return IO_ERROR_INVALID_FORMAT;
				operand = true;
				break;
			case ST_RIP3_CONDITION_COMPARISON:
			{
				const struct st_rip3_condition_comparison *comparison = &token->value.comparison;
				if ((unsigned)comparison->operation > ST_RIP3_CONDITION_GREATER_EQUAL)
					return IO_ERROR_OUT_OF_RANGE;
				if (!valid_bytes(&comparison->reference) || !valid_bytes(&comparison->literal))
					return IO_ERROR_INVALID_ARGUMENT;
				if (comparison->reference.length == 0)
					return IO_ERROR_INVALID_FORMAT;
				if (!operand)
					return IO_ERROR_INVALID_FORMAT;
				operand = false;
				break;
			}
			case ST_RIP3_CONDITION_BOOLEAN:
				if (!operand)
					return IO_ERROR_INVALID_FORMAT;
				operand = false;
				break;
			default:
				return IO_ERROR_INVALID_FORMAT;
		}
	}
	return operand || depth != 0 ? IO_ERROR_INVALID_FORMAT : 0;
}

/* Replace one contiguous reduction with a Boolean. Source and destination may
 * overlap; count was bounded before allocation and each reduction shrinks it. */
static void
collapse(struct st_rip3_condition_token *tokens, size_t *count, size_t at, size_t width, bool value)
{
	tokens[at] = (struct st_rip3_condition_token) {.kind = ST_RIP3_CONDITION_BOOLEAN, .value.boolean = value};
	memmove(tokens + at + 1, tokens + at + width, (*count - at - width) * sizeof(*tokens));
	*count -= width - 1;
}

int
st_rip3_condition_evaluate(bool *result, const struct st_rip3_condition_token *tokens, size_t count,
    const struct st_rip3_condition_source *source)
{
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = st_rip3_condition_validate(tokens, count);
	if (status != 0)
		return status;
	struct st_rip3_condition_token *work = malloc(count * sizeof(*work));
	if (work == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	memcpy(work, tokens, count * sizeof(*work));
	for (size_t i = 0; i < count; i++) {
		if (tokens[i].kind != ST_RIP3_CONDITION_COMPARISON)
			continue;
		const struct st_rip3_condition_comparison *comparison = &tokens[i].value.comparison;
		struct st_rip3_condition_bytes             value      = {0};
		bool                                       answer     = false;
		if (source != NULL && source->value != NULL
		    && source->value(source->context, &comparison->reference, &value) == 0)
			(void)st_rip3_condition_compare(&answer, comparison->operation, &value,
			    &comparison->literal);
		work[i] =
		    (struct st_rip3_condition_token) {.kind = ST_RIP3_CONDITION_BOOLEAN, .value.boolean = answer};
	}
	status = st_rip3_condition_reduce(result, work, count);
	free(work);
	return status;
}

int
st_rip3_condition_reduce(bool *result, struct st_rip3_condition_token *work, size_t count)
{
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	int status = st_rip3_condition_validate(work, count);
	if (status != 0)
		return status;
	for (size_t i = 0; i < count; i++)
		if (work[i].kind == ST_RIP3_CONDITION_COMPARISON)
			return IO_ERROR_INVALID_FORMAT;
	while (count > 1) {
		size_t before = count;
		for (size_t i = 0; i + 2 < count;) {
			if (work[i].kind == ST_RIP3_CONDITION_BOOLEAN
			    && work[i + 2].kind == ST_RIP3_CONDITION_BOOLEAN
			    && (work[i + 1].kind == ST_RIP3_CONDITION_AND
			        || work[i + 1].kind == ST_RIP3_CONDITION_OR)) {
				bool a = work[i].value.boolean, b = work[i + 2].value.boolean;
				bool value = work[i + 1].kind == ST_RIP3_CONDITION_AND ? a && b : a || b;
				collapse(work, &count, i, 3, value);
				/* Revisit the same position for the next binary operation. */
			}
			else
				i++;
		}
		for (size_t i = 0; i + 1 < count; i++) {
			if (work[i].kind == ST_RIP3_CONDITION_NOT && work[i + 1].kind == ST_RIP3_CONDITION_BOOLEAN)
				collapse(work, &count, i, 2, !work[i + 1].value.boolean);
			/* Do not move backward to an earlier NOT before the next binary
			 * phase; that would change multiple-NOT expression results. */
		}
		for (size_t i = 0; i + 2 < count; i++) {
			if (work[i].kind == ST_RIP3_CONDITION_OPEN && work[i + 1].kind == ST_RIP3_CONDITION_BOOLEAN
			    && work[i + 2].kind == ST_RIP3_CONDITION_CLOSE)
				collapse(work, &count, i, 3, work[i + 1].value.boolean);
		}
		if (count == before)
			return IO_ERROR_INVALID_FORMAT;
	}
	bool answer = work[0].value.boolean;
	*result     = answer;
	return 0;
}
