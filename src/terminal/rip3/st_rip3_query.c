/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_query_internal.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_query {
	const struct st_rip3_host_variables *variables;
	const struct st_rip3_host_sink *sink;
	uint8_t *bytes;
	size_t length, cursor;
};

enum token_kind { LITERAL, CONTROL, REFERENCE };
struct token {
	enum token_kind kind;
	size_t length;
	uint8_t control;
	bool special_key;
};

static bool
reserved(const uint8_t *bytes, size_t length)
{
	return bytes[0] == 0 || bytes[0] == '[' || bytes[0] == '\\'
	    || (length > 1 && ((bytes[0] == '(' && bytes[1] == '(')
	        || (bytes[0] == '<' && bytes[1] == '<')));
}

static bool
token(const uint8_t *bytes, size_t length, struct token *result)
{
	struct token value = {.kind=LITERAL,.length=1};
	if (reserved(bytes,length)) return false;
	if (bytes[0] == '$') {
		struct st_rip3_builtin_reference reference;
		const uint8_t *end = memchr(bytes+1,'$',length-1);
		if (end == NULL) return false;
		value.kind = REFERENCE; value.length = (size_t)(end-bytes)+1;
		/* Nested expansion/dispatch must happen before the ordinary decoder.
		 * Do not execute a parameter containing an unexpanded introducer. */
		for (size_t i = 1; i+1 < value.length; i++) {
			if (reserved(bytes+i,value.length-i) || bytes[i] == '^' || bytes[i] == '`')
				return false;
		}
		if (st_rip3_builtin_reference_parse(&reference,bytes,value.length) != 0
		    || !st_rip3_builtin_known(reference.name,strlen(reference.name))) return false;
	}
	else if (bytes[0] == '^' || bytes[0] == '`') {
		if (length < 2) return false;
		uint8_t code = bytes[1];
		value.kind = CONTROL; value.length = 2;
		if (code == bytes[0]) value.control = code;
		else {
			if (code >= 'a' && code <= 'z') code -= 'a'-'A';
			if (code < '@' || code > '_') return false;
			value.control = code-'@';
		}
		/* The documented special keystrokes include a literal '[' after
		 * ESC, despite '[' otherwise introducing a template. Recognize only
		 * those explicit complete forms; do not invent a general '[' escape. */
		if (bytes[1] == '[' && length >= 4 && bytes[2] == '['
		    && strchr("ABCDHKL",bytes[3]) != NULL && bytes[3] != 0) {
			value.length = 4; value.special_key = true;
		}
	}
	else {
		while (value.length < length) {
			const uint8_t *next = bytes+value.length;
			if (reserved(next,length-value.length) || *next == '$' || *next == '^' || *next == '`') break;
			value.length++;
		}
	}
	*result = value;
	return true;
}

int
st_rip3_query_create(struct st_rip3_query **result,
    const struct st_rip3_host_variables *variables, const struct st_rip3_host_sink *sink)
{
	if (result == NULL || variables == NULL || sink == NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_query *query = calloc(1,sizeof(*query));
	if (query == NULL) return IO_ERROR_OUT_OF_MEMORY;
	query->variables = variables; query->sink = sink;
	*result = query;
	return 0;
}

void
st_rip3_query_cancel(struct st_rip3_query *query)
{
	if (query == NULL) return;
	free(query->bytes); query->bytes = NULL;
	query->length = query->cursor = 0;
}

void
st_rip3_query_destroy(struct st_rip3_query *query)
{
	st_rip3_query_cancel(query);
	free(query);
}

bool
st_rip3_query_pending(const struct st_rip3_query *query)
{
	return query != NULL && query->cursor < query->length;
}

void
st_rip3_query_process(struct st_rip3_query *query, size_t budget)
{
	if (query == NULL) return;
	for (size_t i = 0; i < budget && st_rip3_query_pending(query); i++) {
		struct token value;
		if (st_rip3_input_wait_ns(query->variables->input) != 0) return;
		const uint8_t *bytes = query->bytes+query->cursor;
		/* The owned immutable program passed this scan before any effects. */
		if (!token(bytes,query->length-query->cursor,&value)) {
			st_rip3_query_cancel(query);
			return;
		}
		query->cursor += value.length;
		if (value.kind == REFERENCE)
			st_rip3_host_variables_process(query->variables,query->sink,bytes,value.length);
		else if (value.kind == CONTROL) {
			uint8_t output[3] = {value.control,'[',bytes[value.length-1]};
			query->sink->reply(query->sink->context,output,value.special_key ? 3 : 1);
		}
		else query->sink->reply(query->sink->context,bytes,value.length);
	}
	if (!st_rip3_query_pending(query)) st_rip3_query_cancel(query);
}

int
st_rip3_query_start(struct st_rip3_query *query, const uint8_t *bytes, size_t length)
{
	if (query == NULL || query->sink->reply == NULL) return IO_ERROR_NOT_SUPPORTED;
	if (st_rip3_query_pending(query)) return IO_ERROR_BUSY;
	if ((bytes == NULL && length != 0) || length == SIZE_MAX) return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < length;) {
		struct token value;
		if (!token(bytes+i,length-i,&value)) return IO_ERROR_NOT_SUPPORTED;
		i += value.length;
	}
	if (length == 0) return 0;
	uint8_t *copy = malloc(length);
	if (copy == NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy,bytes,length);
	query->bytes = copy; query->length = length; query->cursor = 0;
	st_rip3_query_process(query,64);
	return 0;
}
