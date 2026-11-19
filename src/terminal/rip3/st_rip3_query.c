/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_query_internal.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_user_variables.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_query {
	const struct st_rip3_host_variables *variables;
	const struct st_rip3_host_sink *sink;
	uint8_t *bytes, *output;
	size_t length, cursor, used, capacity;
	unsigned pass;
	bool allocation_failed;
	/* Non-NULL only for an operation begun by this query. */
	struct st_rip3_user_variables *active_user;
};

enum token_kind { LITERAL, CONTROL, ESCAPED, REFERENCE, USER_REFERENCE };
struct token {
	enum token_kind kind;
	size_t length;
	uint8_t control;
};

static size_t
reference_length(const uint8_t *bytes, size_t length, size_t *literal)
{
	bool arguments=false;
	*literal=0;
	for (size_t i=1; i<length; i++) {
		/* Argument-local dollars are payload. A quoted closing dollar after
		 * the argument/name instead leaves a literal reference for pass two. */
		if (bytes[i]=='\\' && i+1<length) {
			if (!arguments && bytes[i+1]=='$') { *literal=i+2;return 0; }
			i++;continue;
		}
		if (bytes[i]=='(') arguments=true;
		else if (bytes[i]==')') arguments=false;
		if (bytes[i]=='$') return i+1;
	}
	return 0;
}

static bool
reference(const uint8_t *bytes, size_t length, bool users, struct token *result)
{
	struct st_rip3_builtin_reference builtin;
	if (st_rip3_builtin_reference_parse(&builtin,bytes,length)==0
	    && st_rip3_builtin_known(builtin.name,strlen(builtin.name))) {
		result->kind=REFERENCE;return true;
	}
	struct st_rip3_variable_reference user;
	if (!users || st_rip3_variable_reference_parse(&user,bytes,length)!=0
	    || st_rip3_builtin_known(user.name,strlen(user.name))) return false;
	result->kind=USER_REFERENCE;return true;
}

static bool
token(const uint8_t *bytes, size_t length, bool users, unsigned pass, bool preflight,
    struct token *result)
{
	struct token value={.kind=LITERAL,.length=1};
	if (bytes[0]==0) return false;
	if (bytes[0]=='$') {
		size_t literal;
		value.length=reference_length(bytes,length,&literal);
		if (value.length==0) {
			/* The documented host quote-both-dollars form has no unquoted
			 * terminator in pass one. Preserve it until outer unquoting.
			 * A stray dollar left after a quoted opener in pass two is literal. */
			if (preflight && literal==0) return false;
			value.length=pass==1 && literal!=0 ? literal : 1;
		}
		else if (!reference(bytes,value.length,users,&value)) return false;
	}
	else if (bytes[0]=='\\' && pass==2) {
		value.kind=ESCAPED;value.length=length>1 ? 2 : 1;
	}
	else if (bytes[0]=='^' || bytes[0]=='`') {
		value.length=length>1 && !(pass==1 && bytes[1]=='$') ? 2 : 1;
		if (value.length==2) {
			uint8_t code=bytes[1];
			if (code>='a' && code<='z') code-='a'-'A';
			if (bytes[1]==bytes[0]) { value.kind=CONTROL;value.control=bytes[0]; }
			else if ((code>='A' && code<='Z') || code=='[' || code=='\\' || code==']' || code=='^' || code=='_') {
				value.kind=CONTROL;value.control=code-'@';
			}
		}
	}
	else {
		while (value.length<length) {
			uint8_t next=bytes[value.length];
			if (next==0 || next=='$' || next=='^' || next=='`' || (pass==2 && next=='\\')) break;
			value.length++;
		}
	}
	*result=value;return true;
}

static void
diagnose(struct st_rip3_query *query, enum st_rip3_host_diagnostic kind)
{
	if (query->sink->diagnostic!=NULL) query->sink->diagnostic(query->sink->context,kind);
}

static void
append(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_query *query=context;
	if (query->allocation_failed || length==0) return;
	if (length>SIZE_MAX-query->used) { query->allocation_failed=true;return; }
	size_t needed=query->used+length;
	if (needed>query->capacity) {
		size_t capacity=query->capacity==0 ? 64 : query->capacity;
		while (capacity<needed) {
			if (capacity>SIZE_MAX/2) { capacity=needed;break; }
			capacity*=2;
		}
		uint8_t *replacement=realloc(query->output,capacity);
		if (replacement==NULL) { query->allocation_failed=true;return; }
		query->output=replacement;query->capacity=capacity;
	}
	memcpy(query->output+query->used,bytes,length);query->used=needed;
}

static void
forward_diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	diagnose(context,kind);
}

int
st_rip3_query_create(struct st_rip3_query **result,
    const struct st_rip3_host_variables *variables, const struct st_rip3_host_sink *sink)
{
	if (result==NULL || variables==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_query *query=calloc(1,sizeof(*query));
	if (query==NULL) return IO_ERROR_OUT_OF_MEMORY;
	query->variables=variables;query->sink=sink;
	*result=query;return 0;
}

void
st_rip3_query_cancel(struct st_rip3_query *query)
{
	if (query==NULL) return;
	if (query->active_user!=NULL) {
		st_rip3_user_variables_abort(query->active_user);query->active_user=NULL;
	}
	free(query->bytes);free(query->output);
	query->bytes=query->output=NULL;
	query->length=query->cursor=query->used=query->capacity=0;
	query->pass=0;query->allocation_failed=false;
}

void
st_rip3_query_destroy(struct st_rip3_query *query)
{
	st_rip3_query_cancel(query);free(query);
}

bool
st_rip3_query_pending(const struct st_rip3_query *query)
{
	return query!=NULL && query->pass!=0;
}

static bool
user_result(struct st_rip3_query *query)
{
	if (st_rip3_user_variables_state(query->active_user)==ST_RIP3_USER_VARIABLE_PROMPT) return false;
	const struct st_rip3_variable_value *result=st_rip3_user_variables_result(query->active_user);
	if (result!=NULL) append(query,result->bytes,result->length);
	st_rip3_user_variables_abort(query->active_user);query->active_user=NULL;
	return true;
}

/* Pass boundaries do not rescan inserted text in its producing pass. Take the
 * completed buffer as the next immutable input, then emit only after pass two.
 * Even a final active delay suspends this transition and later source reads. */
static void
settle(struct st_rip3_query *query)
{
	if (query->allocation_failed) {
		st_rip3_query_cancel(query);diagnose(query,ST_RIP3_HOST_RESOURCE_FAILURE);return;
	}
	if (query->active_user!=NULL || st_rip3_input_wait_ns(query->variables->input)!=0) return;
	while (query->cursor==query->length && query->pass!=0) {
		if (query->pass==2) {
			if (query->used!=0) query->sink->reply(query->sink->context,query->output,query->used);
			st_rip3_query_cancel(query);return;
		}
		free(query->bytes);query->bytes=query->output;query->length=query->used;query->cursor=0;
		query->output=NULL;query->used=query->capacity=0;query->pass=2;
	}
}

void
st_rip3_query_process(struct st_rip3_query *query, size_t budget)
{
	if (query==NULL || budget==0) return;
	for (size_t i=0; i<budget && st_rip3_query_pending(query); i++) {
		if (st_rip3_input_wait_ns(query->variables->input)!=0) return;
		settle(query);
		if (!st_rip3_query_pending(query)) return;
		if (query->active_user!=NULL) {
			if (!user_result(query)) return;
			settle(query);continue;
		}
		const uint8_t *bytes=query->bytes+query->cursor;
		struct token value;
		if (!token(bytes,query->length-query->cursor,query->variables->user_variables!=NULL,query->pass,false,&value)) {
			st_rip3_query_cancel(query);diagnose(query,ST_RIP3_HOST_UNSUPPORTED_REFERENCE);return;
		}
		query->cursor+=value.length;
		if (value.kind==REFERENCE) {
			const struct st_rip3_host_sink sink={query,append,forward_diagnostic};
			st_rip3_host_variables_process(query->variables,&sink,bytes,value.length);
		}
		else if (value.kind==USER_REFERENCE) {
			struct st_rip3_variable_reference decoded;
			if (st_rip3_variable_reference_parse(&decoded,bytes,value.length)!=0) {
				st_rip3_query_cancel(query);diagnose(query,ST_RIP3_HOST_INVALID_REFERENCE);return;
			}
			query->active_user=query->variables->user_variables;
			st_rip3_user_variables_process(query->active_user,&decoded);
			if (!user_result(query)) return;
		}
		else if (value.kind==ESCAPED) {
			if (value.length>1) append(query,bytes+1,1);
		}
		else if (value.kind==CONTROL && query->pass==2) append(query,&value.control,1);
		else append(query,bytes,value.length);
		settle(query);
	}
}

static bool
slot(uint8_t byte)
{
	return (byte>='0' && byte<='9') || (byte>='A' && byte<='Z');
}

int
st_rip3_query_start(struct st_rip3_query *query, const uint8_t *bytes, size_t length)
{
	if (query==NULL || query->sink->reply==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (st_rip3_query_pending(query)) return IO_ERROR_BUSY;
	if ((bytes==NULL && length!=0) || length==SIZE_MAX) return IO_ERROR_INVALID_ARGUMENT;
	size_t incomplete_templates=0;
	for (size_t i=0; i<length; i++) {
		if (bytes[i]==0 || (i+1<length && ((bytes[i]=='<' && bytes[i+1]=='<')
		    || (bytes[i]=='(' && bytes[i+1]=='(')))) return IO_ERROR_NOT_SUPPORTED;
		if (bytes[i]=='[') {
			if (i+1<length && bytes[i+1]==']') return IO_ERROR_NOT_SUPPORTED;
			if (i+1<length && slot(bytes[i+1])) {
				size_t end=i+1;
				while (end<length && slot(bytes[end])) end++;
				if (end<length && (bytes[end]==']' || (bytes[end]==':' && end+1<length && bytes[end+1]==']')))
					return IO_ERROR_NOT_SUPPORTED; /* Complete template stage is separate. */
				incomplete_templates++;
			}
		}
	}
	for (size_t i=0; i<length;) {
		struct token value;
		if (!token(bytes+i,length-i,query->variables->user_variables!=NULL,1,true,&value)) return IO_ERROR_NOT_SUPPORTED;
		if (value.kind==USER_REFERENCE && st_rip3_user_variables_state(query->variables->user_variables)==ST_RIP3_USER_VARIABLE_PROMPT)
			return IO_ERROR_BUSY;
		i+=value.length;
	}
	if (length==0) return 0;
	uint8_t *copy=malloc(length);
	if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy,bytes,length);
	query->bytes=copy;query->length=length;query->cursor=0;query->pass=1;
	for (size_t i=0; i<incomplete_templates; i++) diagnose(query,ST_RIP3_HOST_INVALID_REFERENCE);
	st_rip3_query_process(query,64);
	return 0;
}
