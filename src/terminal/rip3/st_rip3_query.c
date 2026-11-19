/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_query_internal.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_templates.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_query {
	const struct st_rip3_host_variables *variables;
	const struct st_rip3_host_sink *sink;
	uint8_t *bytes, *output, *program;
	size_t program_length, program_cursor;
	size_t length, cursor, used, capacity;
	unsigned pass; /* 0 idle; 1/2 ordinary expansion; 3 prepare the next block. */
	bool allocation_failed;
	/* Non-NULL only for an operation begun by this query. */
	struct st_rip3_user_variables *active_user;
};

static bool
slot(uint8_t byte)
{
	return (byte>='0' && byte<='9') || (byte>='A' && byte<='Z') || (byte>='a' && byte<='z');
}

static uint8_t
slot_number(uint8_t byte)
{
	if (byte>='a' && byte<='z') byte-='a'-'A';
	return (uint8_t)(byte<='9' ? byte-'0' : byte-'A'+10);
}

enum token_kind { LITERAL, CONTROL, ESCAPED, REFERENCE, USER_REFERENCE, TEMPLATE_REFERENCE };
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
token(const uint8_t *bytes, size_t length, bool users, bool templates, unsigned pass,
    struct token *result)
{
	struct token value={.kind=LITERAL,.length=1};
	if (bytes[0]==0) return false;
	if (bytes[0]=='$') {
		size_t literal;
		value.length=reference_length(bytes,length,&literal);
		if (value.length==0) {
			/* The documented host quote-both-dollars form has no unquoted
			 * terminator in pass one. Preserve it until outer unquoting. */
			if (literal==0) return false;
			value.length=pass==1 && literal!=0 ? literal : 1;
		}
		else if (value.length==4 && bytes[1]=='?' && slot(bytes[2])) {
			if (!templates) return false;
			value.kind=TEMPLATE_REFERENCE;value.control=slot_number(bytes[2]);
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

enum block_kind { DIRECT, DEFINITION, CHAIN };
struct delimiter {
	enum block_kind kind;
	size_t length, count;
	uint8_t numbers[36];
};

enum delimiter_kind { NO_DELIMITER, VALID_DELIMITER, LONG_DELIMITER };

static enum delimiter_kind
delimiter(const uint8_t *bytes, size_t length, struct delimiter *result)
{
	if (length<2 || bytes[0]!='[') return NO_DELIMITER;
	struct delimiter value={.kind=DIRECT,.length=2};
	if (bytes[1]==']') { *result=value;return VALID_DELIMITER; }
	size_t at=1;
	while (at<length && slot(bytes[at])) {
		if (value.count<36) value.numbers[value.count]=slot_number(bytes[at]);
		value.count++;at++;
	}
	if (value.count==0 || at==length) return NO_DELIMITER;
	if (bytes[at]==']') {
		if (value.count>36) return LONG_DELIMITER;
		value.kind=CHAIN;value.length=at+1;
	}
	else if (value.count==1 && bytes[at]==':' && at+1<length && bytes[at+1]==']') {
		value.kind=DEFINITION;value.length=at+2;
	}
	else return NO_DELIMITER;
	*result=value;return VALID_DELIMITER;
}

struct block { struct delimiter directive; size_t begin, end; };

static struct block
block(const uint8_t *bytes, size_t length, size_t start)
{
	struct block result={.directive={.kind=DIRECT},.begin=start,.end=length};
	struct delimiter value;
	if (delimiter(bytes+start,length-start,&value)==VALID_DELIMITER) {
		result.directive=value;result.begin+=value.length;
	}
	for (size_t at=result.begin; at<length; at++) {
		if (delimiter(bytes+at,length-at,&value)==VALID_DELIMITER) { result.end=at;break; }
	}
	return result;
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
	if (query->allocation_failed) return;
	if (length>4096-query->used) length=4096-query->used;
	if (length==0) return;
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

/* Logical working text is the processed prefix plus this unread source suffix.
 * Every replacement clips that suffix permanently before the next token; later
 * shrinking/control decoding can never re-expose discarded source bytes. */
static void
replace(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_query *query=context;
	append(query,bytes,length);
	if (query->allocation_failed) return;
	size_t retained=4096-query->used;
	if (query->length-query->cursor>retained) query->length=query->cursor+retained;
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
	free(query->bytes);free(query->output);free(query->program);
	query->bytes=query->output=query->program=NULL;
	query->length=query->cursor=query->used=query->capacity=0;
	query->program_length=query->program_cursor=0;
	query->pass=0;query->allocation_failed=false;
}

static void
operation_failed(struct st_rip3_query *query, int status, unsigned missing)
{
	const struct st_rip3_template_host *host=query->variables->templates;
	st_rip3_query_cancel(query);
	if (status==IO_ERROR_NOT_FOUND && host!=NULL && host->selection_required!=NULL)
		host->selection_required(host->context,missing);
	diagnose(query,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_HOST_RESOURCE_FAILURE : ST_RIP3_HOST_OPERATION_FAILED);
}

/* The source was structurally preflighted. Composition/definitions precede any
 * ordinary expansion, and each resulting direct response gets its own passes. */
static void
prepare_block(struct st_rip3_query *query)
{
	struct block value=block(query->program,query->program_length,query->program_cursor);
	query->program_cursor=value.end;
	const uint8_t *feed=query->program+value.begin;
	size_t length=value.end-value.begin;
	const struct st_rip3_template_host *host=query->variables->templates;
	int status=0;
	if (value.directive.kind==DEFINITION) {
		status=st_rip3_templates_set(host->slots,value.directive.numbers[0],feed,length);
		if (status!=0) operation_failed(query,status,0);
		return;
	}
	uint8_t composed[4096];unsigned missing=0;
	if (value.directive.kind==CHAIN) {
		status=st_rip3_templates_chain(host->slots,value.directive.numbers,value.directive.count,
		    feed,length,composed,sizeof(composed),&length,&missing);
		if (status!=0) { operation_failed(query,status,missing);return; }
		feed=composed;
	}
	if (length>4096) length=4096;
	uint8_t *copy=length==0 ? NULL : malloc(length);
	if (length!=0 && copy==NULL) { operation_failed(query,IO_ERROR_OUT_OF_MEMORY,0);return; }
	if (length!=0) memcpy(copy,feed,length);
	query->bytes=copy;query->length=length;query->cursor=0;query->pass=1;
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
	if (result!=NULL) replace(query,result->bytes,result->length);
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
	if (query->pass==3 && query->program_cursor==query->program_length) { st_rip3_query_cancel(query);return; }
	while (query->cursor==query->length && (query->pass==1 || query->pass==2)) {
		if (query->pass==2) {
			if (query->used!=0) query->sink->reply(query->sink->context,query->output,query->used);
			free(query->bytes);free(query->output);query->bytes=query->output=NULL;
			query->length=query->cursor=query->used=query->capacity=0;query->pass=3;
			if (query->program_cursor==query->program_length) st_rip3_query_cancel(query);
			return;
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
		if (query->pass==3) { prepare_block(query);settle(query);continue; }
		if (query->active_user!=NULL) {
			if (!user_result(query)) return;
			settle(query);continue;
		}
		const uint8_t *bytes=query->bytes+query->cursor;
		struct token value;
		if (!token(bytes,query->length-query->cursor,query->variables->user_variables!=NULL,
		    query->variables->templates!=NULL && query->variables->templates->slots!=NULL,query->pass,&value)) {
			st_rip3_query_cancel(query);diagnose(query,ST_RIP3_HOST_UNSUPPORTED_REFERENCE);return;
		}
		query->cursor+=value.length;
		if (value.kind==REFERENCE) {
			const struct st_rip3_host_sink sink={query,replace,forward_diagnostic};
			st_rip3_host_variables_process(query->variables,&sink,bytes,value.length);
		}
		else if (value.kind==USER_REFERENCE) {
			struct st_rip3_variable_reference decoded;
			if (st_rip3_variable_reference_parse(&decoded,bytes,value.length)!=0) {
				st_rip3_query_cancel(query);diagnose(query,ST_RIP3_HOST_INVALID_REFERENCE);return;
			}
			if (st_rip3_user_variables_state(query->variables->user_variables)==ST_RIP3_USER_VARIABLE_PROMPT) {
				operation_failed(query,IO_ERROR_BUSY,0);return;
			}
			query->active_user=query->variables->user_variables;
			st_rip3_user_variables_process(query->active_user,&decoded);
			if (!user_result(query)) return;
		}
		else if (value.kind==TEMPLATE_REFERENCE) {
			struct st_rip3_template_bytes template;
			int status=st_rip3_templates_resolve(query->variables->templates->slots,value.control,&template);
			if (status!=0) { operation_failed(query,status,value.control);return; }
			replace(query,template.bytes,template.length);
		}
		else if (value.kind==ESCAPED) {
			append(query,bytes+(value.length>1 ? 1 : 0),1);
		}
		else if (value.kind==CONTROL && query->pass==2) append(query,&value.control,1);
		else append(query,bytes,value.length);
		settle(query);
	}
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
			struct delimiter value;
			enum delimiter_kind kind=delimiter(bytes+i,length-i,&value);
			if (kind==LONG_DELIMITER) return IO_ERROR_OUT_OF_RANGE;
			if (kind==VALID_DELIMITER) {
				if (value.kind!=DIRECT && (query->variables->templates==NULL || query->variables->templates->slots==NULL))
					return IO_ERROR_NOT_SUPPORTED;
			}
			else if (i+1<length && slot(bytes[i+1])) incomplete_templates++;
		}
	}
	for (size_t offset=0; offset<length;) {
		struct block value=block(bytes,length,offset);
		size_t working_end=value.begin+(value.end-value.begin>4096 ? 4096 : value.end-value.begin);
		if (value.directive.kind==DIRECT) for (size_t at=value.begin; at<working_end;) {
			struct token parsed;
			if (!token(bytes+at,working_end-at,query->variables->user_variables!=NULL,
			    query->variables->templates!=NULL && query->variables->templates->slots!=NULL,1,&parsed)) return IO_ERROR_NOT_SUPPORTED;
			if (parsed.kind==USER_REFERENCE && st_rip3_user_variables_state(query->variables->user_variables)==ST_RIP3_USER_VARIABLE_PROMPT)
				return IO_ERROR_BUSY;
			/* A replacement can discard the remaining ordinary source. Its
			 * syntax must only be examined if the bounded scanner reaches it. */
			if (parsed.kind==REFERENCE || parsed.kind==USER_REFERENCE || parsed.kind==TEMPLATE_REFERENCE) break;
			at+=parsed.length;
		}
		offset=value.end;
	}
	if (length==0) return 0;
	uint8_t *copy=malloc(length);
	if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(copy,bytes,length);
	query->program=copy;query->program_length=length;query->program_cursor=0;query->pass=3;
	for (size_t i=0; i<incomplete_templates; i++) diagnose(query,ST_RIP3_HOST_INVALID_REFERENCE);
	st_rip3_query_process(query,64);
	return 0;
}
