/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_query_internal.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_refresh.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_templates.h"
#include "st_rip3_angle.h"
#include "st_rip3_graphical_bytes.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct pick_list {
	struct st_rip3_pick_list view;
	struct { const uint8_t *bytes; size_t length; } replies[ST_RIP3_PICK_LIST_MAX_CHOICES];
};

struct st_rip3_query {
	const struct st_rip3_host_variables *variables;
	const struct st_rip3_host_sink *sink;
	void *playback_context;
	int (*play)(void *context, enum st_rip3_playback_kind kind, const char *filename);
	void (*complete)(void *context);
	uint8_t *bytes, *output, *program;
	size_t program_length, program_cursor;
	size_t length, cursor, used, used_units, capacity;
	uint64_t template_owner;
	unsigned pass; /* 0 idle; 1/2 ordinary expansion; 3 next block; 4 angles. */
	struct st_rip3_angle *angle;
	bool allocation_failed, codepage_converted;
	/* Non-NULL only for an operation begun by this query. */
	struct st_rip3_user_variables *active_user;
	struct pick_list *pick_list;
};

static int start_program(struct st_rip3_query *query, const uint8_t *bytes, size_t length);

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

enum token_kind { LITERAL, CONTROL, ESCAPED, REFERENCE, USER_REFERENCE, TEMPLATE_REFERENCE, PLAYBACK, PICK_LIST };
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
	if (!users || st_rip3_variable_reference_parse(&user,bytes,length,false)!=0)
		return false;
	size_t name_length = strlen(user.name);
	if (!st_rip3_bookmark_name(user.name, name_length) && st_rip3_builtin_known(user.name, name_length))
		return false;
	result->kind=USER_REFERENCE;return true;
}

static size_t
unquoted(const uint8_t *bytes, size_t length, uint8_t first, uint8_t second)
{
	for (size_t i=0; i<length; i++) {
		if (bytes[i]=='\\' && i+1<length) { i++;continue; }
		if (bytes[i]==first && (second==0 || (i+1<length && bytes[i+1]==second))) return i;
	}
	return length;
}

static bool
token(const uint8_t *bytes, size_t length, bool users, bool templates, unsigned pass,
    bool converted, bool outer_quoted, struct token *result)
{
	struct token value={.kind=LITERAL,.length=1};
	if (bytes[0]==0) return false;
	if (bytes[0]=='$' && length>1 && st_rip3_playback_prefix(bytes[1])) {
		size_t span=st_rip3_playback_token(bytes,length);
		if (span!=0) {
			value.length=span;
			value.kind=PLAYBACK;
		}
	}
	else if (bytes[0]=='$') {
		size_t literal;
		value.length=reference_length(bytes,length,&literal);
		if (value.length==0) {
			/* The documented host quote-both-dollars form has no unquoted
			 * terminator in pass one. Preserve it until outer unquoting. */
			/* An isolated quoted dollar also survives pass one so pass
			 * two can unquote it after an unclosed playback opener. */
			if (literal==0 && !(pass==1 && outer_quoted)) return false;
			value.length=pass==1 && literal!=0 ? literal : 1;
		}
		else if (value.length==4 && bytes[1]=='?' && slot(bytes[2])) {
			if (!templates) return false;
			value.kind=TEMPLATE_REFERENCE;value.control=slot_number(bytes[2]);
		}
		else if (!reference(bytes,value.length,users,&value)) return false;
	}
	else if (bytes[0]=='\\' && pass==2) {
		value.kind=ESCAPED;
		if (length>1) value.length=1+(converted ? st_rip3_graphical_character(bytes+1,length-1) : 1);
	}
	else if (bytes[0]=='^' || bytes[0]=='`') {
		value.length=length>1 && !(pass==1 && bytes[1]=='$') && !(converted && bytes[1]>=0x80) ? 2 : 1;
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
		if (length>=2 && bytes[0]=='(' && bytes[1]=='(') {
			size_t close=unquoted(bytes+2,length-2,')',')');
			if (close<length-2) {
				*result=(struct token){.kind=PICK_LIST,.length=close+4};
				return true;
			}
		}
		while (value.length<length) {
			uint8_t next=bytes[value.length];
			if (next==0 || next=='$' || next=='^' || next=='`' || (pass==2 && next=='\\')) break;
			if (next=='(' && value.length+1<length && bytes[value.length+1]=='(') break;
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

/* Complete playback tokens own their bracket bytes. Track an ordinary
 * reference's closing dollar so it cannot be mistaken for a media opener.
 * Brackets in the rest of the program still participate in segmentation. */
static size_t
block_playback_span(const uint8_t *bytes, size_t length, size_t at, size_t *ordinary_end)
{
	if (bytes[at]!='$' || length-at<2 || at<*ordinary_end) return 0;
	if (st_rip3_playback_prefix(bytes[at+1]))
		return st_rip3_playback_token(bytes+at,length-at);
	size_t quoted;
	*ordinary_end=at+reference_length(bytes+at,length-at,&quoted);
	return 0;
}

static struct block
block(const uint8_t *bytes, size_t length, size_t start)
{
	struct block result={.directive={.kind=DIRECT},.begin=start,.end=length};
	struct delimiter value;
	if (delimiter(bytes+start,length-start,&value)==VALID_DELIMITER) {
		result.directive=value;result.begin+=value.length;
	}
	size_t ordinary_end=result.begin;
	for (size_t at=result.begin; at<length; at++) {
		size_t reference=block_playback_span(bytes,length,at,&ordinary_end);
		if (reference!=0) { at+=reference-1;continue; }
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
	length=st_rip3_graphical_prefix(bytes,length,4096-query->used_units,query->codepage_converted);
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
	query->used_units+=st_rip3_graphical_units(bytes,length,query->codepage_converted);
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
	size_t retained=st_rip3_graphical_prefix(query->bytes+query->cursor,query->length-query->cursor,
	    4096-query->used_units,query->codepage_converted);
	query->length=query->cursor+retained;
}

static void
forward_diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	if (kind==ST_RIP3_HOST_ABORTED) st_rip3_query_cancel(context);
	else diagnose(context,kind);
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

static bool
position_axis(const uint8_t *bytes, size_t length, size_t *at, uint8_t delimiter,
    bool *present, uint16_t *value)
{
	size_t start=*at;
	uint32_t number=0;
	while (*at<length && bytes[*at]>='0' && bytes[*at]<='9') {
		number=number*10+bytes[(*at)++]-'0';
		if (number>UINT16_MAX) return false;
	}
	if (*at==length || bytes[*at]!=delimiter) return false;
	*present=*at!=start;
	*value=(uint16_t)number;
	++*at;
	return true;
}

static void
pick_list_text(struct st_rip3_pick_list_text *result, const uint8_t *bytes,
    size_t length, bool markup, bool converted)
{
	length=st_rip3_graphical_prefix(bytes,length,ST_RIP3_PICK_LIST_DISPLAY_LIMIT,converted);
	size_t used=0, character=0;
	bool highlight=false;
	for (size_t at=0; at<length;) {
		if (bytes[at]=='\\' && ++at==length) break;
		if (markup && (bytes[at]=='_' || bytes[at]=='~')) {
			highlight=!highlight;
			++at;
			continue;
		}
		size_t size=st_rip3_graphical_character(bytes+at,length-at);
		if (highlight) result->highlighted|=UINT64_C(1)<<character;
		memcpy(result->bytes+used,bytes+at,size);
		used+=size;
		at+=size;
		++character;
	}
	result->bytes[used]='\0';
}

static int
pick_list_start(struct st_rip3_query *query, const uint8_t *bytes, size_t length)
{
	struct pick_list *list=calloc(1,sizeof(*list));
	if (list==NULL) return IO_ERROR_OUT_OF_MEMORY;
	struct st_rip3_pick_list *view=&list->view;
	if (length!=0 && bytes[0]=='*') { view->required=true; ++bytes; --length; }
	size_t at=0;
	bool has_x,has_y;
	uint16_t x,y;
	if (position_axis(bytes,length,&at,',',&has_x,&x)
	    && position_axis(bytes,length,&at,':',&has_y,&y)) {
		view->has_x=has_x;view->has_y=has_y;view->x=x;view->y=y;
		bytes+=at;length-=at;
	}
	size_t question=unquoted(bytes,length,':',':');
	if (question<length) {
		pick_list_text(&view->question,bytes,question,false,query->codepage_converted);
		bytes+=question+2;length-=question+2;
	}
	else strcpy(view->question.bytes,"Please enter requested value");

	while (length!=0 && view->count<ST_RIP3_PICK_LIST_MAX_CHOICES) {
		size_t entry=unquoted(bytes,length,',',0);
		size_t reply=unquoted(bytes,entry,'@',0);
		const uint8_t *label=bytes;
		size_t label_length=entry;
		if (reply<entry) {
			label=bytes+reply+1;
			label_length=entry-reply-1;
			if (unquoted(label,label_length,'@',0)!=label_length) break;
		}
		size_t index=view->count++;
		list->replies[index].bytes=bytes;
		list->replies[index].length=st_rip3_graphical_prefix(bytes,reply,1023,query->codepage_converted);
		pick_list_text(&view->labels[index],label,label_length,true,query->codepage_converted);
		size_t consumed=entry+(entry<length);
		bytes+=consumed;
		length-=consumed;
	}
	if (view->count==0 || (length!=0 && view->count<ST_RIP3_PICK_LIST_MAX_CHOICES)) {
		free(list);
		replace(query,NULL,0);
	}
	else query->pick_list=list;
	return 0;
}

const struct st_rip3_pick_list *
st_rip3_query_pick_list(const struct st_rip3_query *query)
{
	return query==NULL || query->pick_list==NULL ? NULL : &query->pick_list->view;
}

bool
st_rip3_query_choose(struct st_rip3_query *query, size_t index)
{
	if (query==NULL || query->pick_list==NULL) return false;
	struct pick_list *list=query->pick_list;
	if (index>list->view.count || (index==list->view.count && list->view.required)) return false;
	if (index==list->view.count) replace(query,NULL,0);
	else replace(query,list->replies[index].bytes,list->replies[index].length);
	free(list);
	query->pick_list=NULL;
	return true;
}

void
st_rip3_query_set_codepage_converted(struct st_rip3_query *query, bool converted)
{
	query->codepage_converted=converted;
}

void
st_rip3_query_bind_playback(struct st_rip3_query *query, void *context,
    int (*play)(void *context, enum st_rip3_playback_kind kind, const char *filename))
{
	query->playback_context=context;
	query->play=play;
}

void
st_rip3_query_cancel(struct st_rip3_query *query)
{
	if (query==NULL) return;
	if (query->active_user!=NULL) {
		st_rip3_user_variables_abort(query->active_user);query->active_user=NULL;
	}
	st_rip3_angle_destroy(query->angle);query->angle=NULL;
	free(query->pick_list);query->pick_list=NULL;
	free(query->bytes);free(query->output);free(query->program);
	query->bytes=query->output=query->program=NULL;
	query->length=query->cursor=query->used=query->used_units=query->capacity=0;
	query->program_length=query->program_cursor=0;
	query->pass=0;query->allocation_failed=false;query->template_owner=0;
	query->complete=NULL;
}

static void
finish_program(struct st_rip3_query *query)
{
	void (*complete)(void *)=query->complete;
	st_rip3_query_cancel(query);
	query->complete=complete;
}

void
st_rip3_query_run_completion(struct st_rip3_query *query)
{
	if (query==NULL || st_rip3_query_pending(query) || query->complete==NULL) return;
	void (*complete)(void *)=query->complete;
	query->complete=NULL;
	complete(query->sink->context);
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

void
st_rip3_query_pick_list_failed(struct st_rip3_query *query, int status)
{
	if (st_rip3_query_pick_list(query)==NULL) return;
	st_rip3_query_cancel(query);
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
		status=query->template_owner==0 ? st_rip3_templates_set(host->slots,value.directive.numbers[0],feed,length)
		    : st_rip3_templates_associate(host->slots,query->template_owner,value.directive.numbers[0],feed,length);
		if (status!=0) operation_failed(query,status,0);
		return;
	}
	uint8_t composed[ST_RIP3_TEMPLATE_ENCODED_CAPACITY];unsigned missing=0;
	if (value.directive.kind==CHAIN) {
		status=st_rip3_templates_chain(host->slots,value.directive.numbers,value.directive.count,
		    feed,length,composed,sizeof(composed),&length,&missing);
		if (status!=0) { operation_failed(query,status,missing);return; }
		feed=composed;
	}
	length=st_rip3_graphical_prefix(feed,length,4096,query->codepage_converted);
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

bool
st_rip3_query_prompt_pending(const struct st_rip3_query *query)
{
	return query!=NULL && query->active_user!=NULL
	    && st_rip3_user_variables_state(query->active_user)==ST_RIP3_USER_VARIABLE_PROMPT;
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

static void
angle_failure(struct st_rip3_query *query, enum st_rip3_host_diagnostic kind)
{
	st_rip3_query_cancel(query);diagnose(query,kind);
}

struct angle_value {uint8_t *bytes;size_t length,capacity;int status;};

static void
angle_reply(void *context, const uint8_t *bytes, size_t length)
{
	struct angle_value *value=context;if (value->status!=0 || length==0) return;
	if (length>SIZE_MAX-value->length) {value->status=IO_ERROR_OUT_OF_MEMORY;return;}
	size_t needed=value->length+length;
	if (needed>value->capacity) {
		size_t capacity=value->capacity==0 ? 64 : value->capacity;
		while (capacity<needed) capacity=capacity>SIZE_MAX/2 ? needed : capacity*2;
		uint8_t *replacement=realloc(value->bytes,capacity);
		if (replacement==NULL) {value->status=IO_ERROR_OUT_OF_MEMORY;return;}
		value->bytes=replacement;value->capacity=capacity;
	}
	memcpy(value->bytes+value->length,bytes,length);value->length=needed;
}

static void
angle_diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct angle_value *value=context;
	value->status=kind==ST_RIP3_HOST_ABORTED ? IO_ERROR_CANCELLED
	    : kind==ST_RIP3_HOST_RESOURCE_FAILURE ? IO_ERROR_OUT_OF_MEMORY
	    : kind==ST_RIP3_HOST_INVALID_REFERENCE ? IO_ERROR_INVALID_FORMAT
	    : kind==ST_RIP3_HOST_UNSUPPORTED_REFERENCE ? IO_ERROR_NOT_SUPPORTED : IO_ERROR_IO;
}

static bool
process_builtin(struct st_rip3_query *query, const struct st_rip3_host_sink *sink,
    const uint8_t *bytes, size_t length)
{
	struct st_rip3_user_variables *user = query->variables->user_variables;
	bool pending = st_rip3_user_variables_confirming_delete(user);
	st_rip3_host_variables_process(query->variables, sink, bytes, length);
	if (pending || !st_rip3_user_variables_confirming_delete(user))
		return false;
	query->active_user = user;
	return true;
}

static void
angle_reference(struct st_rip3_query *query, const struct st_rip3_condition_bytes *reference)
{
	/* The outer/conditional stages synthesize retrieval. Strip only repeated
	 * GET prefixes for ordinary built-in dispatch; extended syntax on a known
	 * built-in must never reach a same-named user record. */
	uint8_t ordinary[259];size_t start=1;
	while (start<reference->length && reference->bytes[start]=='&') start++;
	ordinary[0]='$';memcpy(ordinary+1,reference->bytes+start,reference->length-start);
	size_t length=reference->length-start+1;
	struct st_rip3_builtin_reference builtin;struct st_rip3_variable_reference user;
	if (st_rip3_builtin_reference_parse(&builtin,ordinary,length)==0 && st_rip3_builtin_known(builtin.name,strlen(builtin.name))) {
		struct angle_value value={0};const struct st_rip3_host_sink sink={&value,angle_reply,angle_diagnostic};
		bool pending = process_builtin(query, &sink, ordinary, length);
		const struct st_rip3_condition_bytes bytes={value.bytes,value.length};
		if (value.status==IO_ERROR_CANCELLED) st_rip3_query_cancel(query);
		else if (!pending) st_rip3_angle_supply(query->angle,&bytes,value.status);
		free(value.bytes);
	}
	else if (st_rip3_user_variables_decode(query->variables->user_variables,&user,reference->bytes,reference->length)!=0
	    || (!st_rip3_bookmark_name(user.name, strlen(user.name))
	        && st_rip3_builtin_known(user.name,strlen(user.name))))
		st_rip3_angle_supply(query->angle,NULL,IO_ERROR_NOT_SUPPORTED);
	else if (query->variables->user_variables==NULL) st_rip3_angle_supply(query->angle,NULL,IO_ERROR_NOT_SUPPORTED);
	else if (st_rip3_user_variables_state(query->variables->user_variables)==ST_RIP3_USER_VARIABLE_PROMPT)
		st_rip3_angle_supply(query->angle,NULL,IO_ERROR_BUSY);
	else {
		query->active_user=query->variables->user_variables;
		st_rip3_user_variables_process(query->active_user,&user);
	}
}

static bool
angle_step(struct st_rip3_query *query)
{
	if (query->active_user!=NULL) {
		if (st_rip3_user_variables_state(query->active_user)==ST_RIP3_USER_VARIABLE_PROMPT) return false;
		const struct st_rip3_variable_value *value=st_rip3_user_variables_result(query->active_user);
		const struct st_rip3_condition_bytes bytes={value==NULL ? NULL : value->bytes,value==NULL ? 0 : value->length};
		st_rip3_angle_supply(query->angle,&bytes,value==NULL ? IO_ERROR_IO : 0);
		st_rip3_user_variables_abort(query->active_user);query->active_user=NULL;
	}
	st_rip3_angle_process(query->angle,1);
	const struct st_rip3_angle_request *request=st_rip3_angle_request(query->angle);
	if (request!=NULL) {
		if (request->kind==ST_RIP3_ANGLE_REFERENCE) angle_reference(query,&request->reference);
		else {
			struct st_rip3_template_bytes value;const struct st_rip3_template_host *host=query->variables->templates;
			unsigned number=request->number;
			int status=host==NULL || host->slots==NULL ? IO_ERROR_NOT_SUPPORTED : st_rip3_templates_resolve(host->slots,number,&value);
			if (status==IO_ERROR_NOT_FOUND) {operation_failed(query,status,number);return false;}
			const struct st_rip3_condition_bytes bytes={status==0 ? value.bytes : NULL,status==0 ? value.length : 0};
			st_rip3_angle_supply(query->angle,&bytes,status);
		}
	}
	if (query->angle==NULL) return false;
	if (st_rip3_angle_state(query->angle)==ST_RIP3_ANGLE_FAILED) {
		enum st_rip3_angle_diagnostic kind=st_rip3_angle_diagnostic(query->angle);
		angle_failure(query,kind==ST_RIP3_ANGLE_RESOURCE ? ST_RIP3_HOST_RESOURCE_FAILURE
		    : kind==ST_RIP3_ANGLE_INVALID ? ST_RIP3_HOST_INVALID_REFERENCE : ST_RIP3_HOST_OPERATION_FAILED);return false;
	}
	const struct st_rip3_condition_bytes *result=st_rip3_angle_result(query->angle);
	if (result!=NULL) {
		struct st_rip3_angle *angle=query->angle;query->angle=NULL;query->pass=0;
		int status=start_program(query,result->bytes,result->length);st_rip3_angle_destroy(angle);
		if (status!=0) {
			angle_failure(query,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_HOST_RESOURCE_FAILURE
			    : status==IO_ERROR_INVALID_FORMAT || status==IO_ERROR_OUT_OF_RANGE ? ST_RIP3_HOST_INVALID_REFERENCE
			    : ST_RIP3_HOST_UNSUPPORTED_REFERENCE);return false;
		}
		if (!st_rip3_query_pending(query)) finish_program(query);
	}
	return true;
}

static bool
refresh_waiting(const struct st_rip3_query *query)
{
	const struct st_rip3_refresh *refresh = query->variables->refresh;
	return refresh != NULL && refresh->query != query && st_rip3_refresh_pending(refresh);
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
	if (query->active_user!=NULL || query->pick_list!=NULL
	    || st_rip3_input_wait_ns(query->variables->input)!=0 || refresh_waiting(query)) return;
	if (query->pass==3 && query->program_cursor==query->program_length) { finish_program(query);return; }
	while (query->cursor==query->length && (query->pass==1 || query->pass==2)) {
		if (query->pass==2) {
			if (query->used!=0) query->sink->reply(query->sink->context,query->output,query->used);
			free(query->bytes);free(query->output);query->bytes=query->output=NULL;
			query->length=query->cursor=query->used=query->used_units=query->capacity=0;query->pass=3;
			if (query->program_cursor==query->program_length) finish_program(query);
			return;
		}
		free(query->bytes);query->bytes=query->output;query->length=query->used;query->cursor=0;
		query->output=NULL;query->used=query->used_units=query->capacity=0;query->pass=2;
	}
}

static bool
playback(struct st_rip3_query *query, const uint8_t *bytes, size_t length)
{
	char filename[ST_RIP3_PLAYBACK_NAME_SIZE];
	if (!st_rip3_playback_name(filename,bytes,length,query->codepage_converted,query->variables)) {
		append(query,bytes,length);
		return true;
	}
	if (filename[0]==0) return true;
	enum st_rip3_playback_kind kind=(enum st_rip3_playback_kind)bytes[1];
	int status=query->play==NULL ? IO_ERROR_NOT_SUPPORTED
	    : query->play(query->playback_context,kind,filename);
	if (kind == ST_RIP3_PLAYBACK_SCENE && status == IO_ERROR_IN_PROGRESS)
		return false;
	if (status!=0) diagnose(query,status==IO_ERROR_NOT_SUPPORTED ? ST_RIP3_HOST_UNSUPPORTED_REFERENCE
	    : status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_HOST_RESOURCE_FAILURE : ST_RIP3_HOST_OPERATION_FAILED);
	return true;
}

void
st_rip3_query_process(struct st_rip3_query *query, size_t budget)
{
	if (query==NULL || budget==0) return;
	for (size_t i=0; i<budget && st_rip3_query_pending(query); i++) {
		if (st_rip3_input_wait_ns(query->variables->input)!=0 || refresh_waiting(query)) return;
		settle(query);
		if (!st_rip3_query_pending(query) || query->pick_list!=NULL) return;
		if (query->pass==4) {if (!angle_step(query)) return;continue;}
		if (query->pass==3) { prepare_block(query);settle(query);continue; }
		if (query->active_user!=NULL) {
			if (!user_result(query)) return;
			settle(query);continue;
		}
		const uint8_t *bytes=query->bytes+query->cursor;
		struct token value;
		if (!token(bytes,query->length-query->cursor,query->variables->user_variables!=NULL,
		    query->variables->templates!=NULL && query->variables->templates->slots!=NULL,query->pass,
		    query->codepage_converted,query->cursor!=0 && bytes[-1]=='\\',&value)) {
			st_rip3_query_cancel(query);diagnose(query,ST_RIP3_HOST_UNSUPPORTED_REFERENCE);return;
		}
		query->cursor+=value.length;
		if (value.kind==REFERENCE) {
			const struct st_rip3_host_sink sink={query,replace,forward_diagnostic};
			if (process_builtin(query, &sink, bytes, value.length)) return;
		}
		else if (value.kind==USER_REFERENCE) {
			struct st_rip3_variable_reference decoded;
			if (st_rip3_user_variables_decode(query->variables->user_variables,&decoded,bytes,value.length)!=0) {
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
		else if (value.kind==PLAYBACK) {
			if (!playback(query,bytes,value.length)) return;
		}
		else if (value.kind==PICK_LIST) {
			int status=pick_list_start(query,bytes+2,value.length-4);
			if (status!=0) { operation_failed(query,status,0);return; }
		}
		else if (value.kind==ESCAPED) {
			size_t skip=value.length>1;
			append(query,bytes+skip,value.length-skip);
		}
		else if (value.kind==CONTROL && query->pass==2) append(query,&value.control,1);
		else append(query,bytes,value.length);
		settle(query);
	}
}

static int
start_program(struct st_rip3_query *query, const uint8_t *bytes, size_t length)
{
	if (query==NULL || query->sink->reply==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (st_rip3_query_pending(query)) return IO_ERROR_BUSY;
	if ((bytes==NULL && length!=0) || length==SIZE_MAX) return IO_ERROR_INVALID_ARGUMENT;
	size_t incomplete_templates=0,ordinary_end=0;
	for (size_t i=0; i<length; i++) {
		if (bytes[i]==0) return IO_ERROR_NOT_SUPPORTED;
		size_t reference=block_playback_span(bytes,length,i,&ordinary_end);
		if (reference!=0) { i+=reference-1;continue; }
		if (bytes[i]=='[') {
			struct delimiter value;
			enum delimiter_kind kind=delimiter(bytes+i,length-i,&value);
			if (kind==LONG_DELIMITER) return IO_ERROR_OUT_OF_RANGE;
			if (kind==VALID_DELIMITER) {
				ordinary_end=i+value.length;
				if (value.kind!=DIRECT && (query->variables->templates==NULL || query->variables->templates->slots==NULL))
					return IO_ERROR_NOT_SUPPORTED;
			}
			else if (i+1<length && slot(bytes[i+1])) incomplete_templates++;
		}
	}
	for (size_t offset=0; offset<length;) {
		struct block value=block(bytes,length,offset);
		size_t working_end=value.begin+st_rip3_graphical_prefix(bytes+value.begin,value.end-value.begin,
		    4096,query->codepage_converted);
		if (value.directive.kind==DIRECT) for (size_t at=value.begin; at<working_end;) {
			struct token parsed;
			if (!token(bytes+at,working_end-at,query->variables->user_variables!=NULL,
			    query->variables->templates!=NULL && query->variables->templates->slots!=NULL,1,
			    query->codepage_converted,at>value.begin && bytes[at-1]=='\\',&parsed)) return IO_ERROR_NOT_SUPPORTED;
			if (parsed.kind==USER_REFERENCE && st_rip3_user_variables_state(query->variables->user_variables)==ST_RIP3_USER_VARIABLE_PROMPT)
				return IO_ERROR_BUSY;
			/* A replacement can discard the remaining ordinary source. Its
			 * syntax must only be examined if the bounded scanner reaches it. */
			if (parsed.kind==REFERENCE || parsed.kind==USER_REFERENCE || parsed.kind==TEMPLATE_REFERENCE
			    || parsed.kind==PICK_LIST) break;
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
	return 0;
}

int
st_rip3_query_start_owned(struct st_rip3_query *query, const uint8_t *bytes, size_t length,uint64_t owner,
    void (*complete)(void *context))
{
	if (query==NULL || query->sink->reply==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (st_rip3_query_pending(query)) return IO_ERROR_BUSY;
	if ((bytes==NULL && length!=0) || length==SIZE_MAX) return IO_ERROR_INVALID_ARGUMENT;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	query->template_owner=owner;
	for (size_t i=0; i+1<length; i++) if (bytes[i]=='<' && bytes[i+1]=='<') {
		int status=st_rip3_angle_create(&query->angle,bytes,length);
		if (status!=0) return status;
		query->complete=complete;
		query->pass=4;st_rip3_query_process(query,64);return 0;
	}
	int status=start_program(query,bytes,length);
	if (status==0) {
		query->complete=complete;
		if (st_rip3_query_pending(query)) st_rip3_query_process(query,64);
		else finish_program(query);
	}
	return status;
}

int st_rip3_query_start(struct st_rip3_query *query,const uint8_t *bytes,size_t length)
{
	return st_rip3_query_start_owned(query,bytes,length,0,NULL);
}
