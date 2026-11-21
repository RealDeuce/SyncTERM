/* The outer scanner deliberately has different escapes from its operand lexer. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_angle.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_condition_internal.h"
#include "st_rip3_host_internal.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct branch {bool parent,selected,seen_else,active;};

struct st_rip3_angle {
	uint8_t *bytes;size_t length,capacity,cursor,consumed,limit;
	unsigned pass,depth;bool changed,converted;
	size_t inserted;
	struct branch branches[16];
	struct st_rip3_condition_evaluation *condition;
	struct st_rip3_angle_request request;
	struct st_rip3_condition_bytes result;
	uint8_t reference[259];
	enum st_rip3_angle_state state;
	enum st_rip3_angle_diagnostic diagnostic;
};

static void
fail(struct st_rip3_angle *angle, enum st_rip3_angle_diagnostic diagnostic)
{
	st_rip3_condition_evaluation_destroy(angle->condition);angle->condition=NULL;
	angle->diagnostic=diagnostic;angle->state=ST_RIP3_ANGLE_FAILED;
}

static bool
active(const struct st_rip3_angle *angle)
{
	return angle->depth==0 || angle->branches[angle->depth-1].active;
}

static bool
splice(struct st_rip3_angle *angle, size_t count, const uint8_t *bytes, size_t length)
{
	/* The working text includes processed prefix, insertion and unread suffix.
	 * Clip before moving/scanning so a later shrink cannot reveal discarded data. */
	size_t remaining=angle->limit-st_rip3_graphical_units(angle->bytes,angle->cursor,angle->converted);
	length=st_rip3_graphical_prefix(bytes,length,remaining,angle->converted);
	remaining-=st_rip3_graphical_units(bytes,length,angle->converted);
	size_t suffix=st_rip3_graphical_prefix(angle->bytes+angle->cursor+count,
	    angle->length-angle->cursor-count,remaining,angle->converted);
	if (length>SIZE_MAX-1-angle->cursor || suffix>SIZE_MAX-1-angle->cursor-length) {
		fail(angle,ST_RIP3_ANGLE_RESOURCE);return false;
	}
	angle->inserted=length;
	size_t needed=angle->cursor+length+suffix+1;
	if (needed>angle->capacity) {
		size_t capacity=angle->capacity;
		while (capacity<needed) capacity=capacity>SIZE_MAX/2 ? needed : capacity*2;
		uint8_t *replacement=realloc(angle->bytes,capacity);
		if (replacement==NULL) {fail(angle,ST_RIP3_ANGLE_RESOURCE);return false;}
		angle->bytes=replacement;angle->capacity=capacity;
	}
	memmove(angle->bytes+angle->cursor+length,angle->bytes+angle->cursor+count,
	    suffix);
	if (length!=0) memcpy(angle->bytes+angle->cursor,bytes,length);
	angle->length=needed-1;angle->bytes[angle->length]=0;angle->changed=true;return true;
}

static size_t
pair(const struct st_rip3_angle *angle, size_t at, uint8_t byte)
{
	while (at+1<angle->length) {
		if (angle->bytes[at]==byte && angle->bytes[at+1]==byte) {
			if (angle->bytes[at-1]!='\\') return at;
			at+=2;
		}
		else at++;
	}
	return angle->length;
}

static bool
prefix(const uint8_t *bytes, size_t length, const char *word)
{
	size_t size=strlen(word);if (size>length) return false;
	for (size_t i=0; i<size; i++) {
		uint8_t byte=bytes[i];if (byte>='a' && byte<='z') byte-='a'-'A';
		if (byte!=(uint8_t)word[i]) return false;
	}
	return true;
}

static bool
reference_valid(const uint8_t *bytes, size_t length)
{
	uint8_t ordinary[259];size_t start=1;
	while (start<length && bytes[start]=='&') start++;
	ordinary[0]='$';memcpy(ordinary+1,bytes+start,length-start);
	struct st_rip3_builtin_reference builtin;struct st_rip3_variable_reference user;
	return st_rip3_builtin_reference_parse(&builtin,ordinary,length-start+1)==0
	    || st_rip3_variable_reference_parse(&user,bytes,length)==0;
}

int
st_rip3_angle_create_graphical(struct st_rip3_angle **result, const uint8_t *bytes, size_t length, size_t limit, bool converted_codepage)
{
	if (result==NULL || (bytes==NULL && length!=0) || length==SIZE_MAX || limit==SIZE_MAX) return IO_ERROR_INVALID_ARGUMENT;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	length=st_rip3_graphical_prefix(bytes,length,limit,converted_codepage);
	struct st_rip3_angle *angle=calloc(1,sizeof(*angle));
	if (angle==NULL) return IO_ERROR_OUT_OF_MEMORY;
	angle->bytes=malloc(length+1);
	if (angle->bytes==NULL) {free(angle);return IO_ERROR_OUT_OF_MEMORY;}
	if (length!=0) memcpy(angle->bytes,bytes,length);
	angle->bytes[length]=0;angle->length=length;angle->capacity=length+1;angle->pass=1;angle->limit=limit;angle->converted=converted_codepage;
	*result=angle;return 0;
}

int
st_rip3_angle_create_bounded(struct st_rip3_angle **result, const uint8_t *bytes, size_t length, size_t limit)
{
	return st_rip3_angle_create_graphical(result,bytes,length,limit,false);
}

int
st_rip3_angle_create(struct st_rip3_angle **result, const uint8_t *bytes, size_t length)
{
	return st_rip3_angle_create_bounded(result,bytes,length,SIZE_MAX-1);
}

void
st_rip3_angle_destroy(struct st_rip3_angle *angle)
{
	if (angle==NULL) return;
	st_rip3_condition_evaluation_destroy(angle->condition);free(angle->bytes);free(angle);
}

enum st_rip3_angle_state
st_rip3_angle_state(const struct st_rip3_angle *angle)
{
	return angle==NULL ? ST_RIP3_ANGLE_FAILED : angle->state;
}

enum st_rip3_angle_diagnostic
st_rip3_angle_diagnostic(const struct st_rip3_angle *angle)
{
	return angle==NULL ? ST_RIP3_ANGLE_INVALID : angle->diagnostic;
}

const struct st_rip3_angle_request *
st_rip3_angle_request(const struct st_rip3_angle *angle)
{
	return st_rip3_angle_state(angle)==ST_RIP3_ANGLE_VALUE ? &angle->request : NULL;
}

const struct st_rip3_condition_bytes *
st_rip3_angle_result(const struct st_rip3_angle *angle)
{
	return st_rip3_angle_state(angle)==ST_RIP3_ANGLE_COMPLETE ? &angle->result : NULL;
}

void
st_rip3_angle_supply(struct st_rip3_angle *angle, const struct st_rip3_condition_bytes *value, int status)
{
	if (st_rip3_angle_state(angle)!=ST_RIP3_ANGLE_VALUE) return;
	angle->state=ST_RIP3_ANGLE_RUNNING;
	if (angle->request.comparison) {st_rip3_condition_evaluation_supply(angle->condition,value,status);return;}
	if (status!=0 || value==NULL || (value->length!=0 && value->bytes==NULL)) {
		fail(angle,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_ANGLE_RESOURCE : ST_RIP3_ANGLE_OPERATION);return;
	}
	size_t length=value->length;
	const uint8_t *end=length==0 ? NULL : memchr(value->bytes,0,length);
	if (end!=NULL) length=(size_t)(end-value->bytes);
	bool deferred=angle->request.kind==ST_RIP3_ANGLE_TEMPLATE;
	if (deferred && !active(angle)) length=0;
	if (splice(angle,angle->consumed,value->bytes,length) && deferred) angle->cursor+=angle->inserted;
}

static void
condition_step(struct st_rip3_angle *angle)
{
	st_rip3_condition_evaluation_process(angle->condition,1);
	enum st_rip3_condition_evaluation_state state=st_rip3_condition_evaluation_state(angle->condition);
	if (state==ST_RIP3_CONDITION_VALUE) {
		const struct st_rip3_condition_bytes *reference=st_rip3_condition_evaluation_request(angle->condition);
		bool template=reference->length==2 && reference->bytes[0]=='?';
		angle->request=(struct st_rip3_angle_request){.comparison=true,
		    .kind=template ? ST_RIP3_ANGLE_TEMPLATE : ST_RIP3_ANGLE_REFERENCE,
		    .number=template ? reference->bytes[1] : 0,.reference=*reference};
		angle->state=ST_RIP3_ANGLE_VALUE;
	}
	else if (state==ST_RIP3_CONDITION_FAILED) fail(angle,ST_RIP3_ANGLE_INVALID);
	else if (state==ST_RIP3_CONDITION_COMPLETE) {
		bool answer=false;(void)st_rip3_condition_evaluation_result(angle->condition,&answer);
		struct branch *frame=&angle->branches[angle->depth-1];
		frame->active=frame->parent && answer;frame->selected|=frame->active;
		st_rip3_condition_evaluation_destroy(angle->condition);angle->condition=NULL;
	}
}

static void
conditional(struct st_rip3_angle *angle, size_t end, bool elseif)
{
	size_t begin=angle->cursor+(elseif ? 9 : 5),length=end+2-begin,consumed=0;
	int status=st_rip3_condition_source_validate(angle->bytes+begin,length,&consumed);
	if (status!=0 || consumed!=length || (elseif && (angle->depth==0 || angle->branches[angle->depth-1].seen_else))
	    || (!elseif && angle->depth==16)) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
	bool parent=elseif ? angle->branches[angle->depth-1].parent : active(angle);
	bool eligible=parent && (!elseif || !angle->branches[angle->depth-1].selected);
	if (eligible) {
		struct st_rip3_condition_program *program=NULL;
		status=st_rip3_condition_program_parse(&program,angle->bytes+begin,length,NULL);
		if (status==0) status=st_rip3_condition_evaluation_create(&angle->condition,program);
		st_rip3_condition_program_destroy(program);
		if (status!=0) {fail(angle,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_ANGLE_RESOURCE : ST_RIP3_ANGLE_INVALID);return;}
	}
	if (!elseif) angle->branches[angle->depth++]=(struct branch){.parent=parent};
	else angle->branches[angle->depth-1].active=false;
	(void)splice(angle,end+2-angle->cursor,NULL,0);
}

static void
header(struct st_rip3_angle *angle)
{
	size_t begin=angle->cursor+2,end=pair(angle,begin,'>');
	if (end==angle->length || pair(angle,begin,'<')<end) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
	const uint8_t *body=angle->bytes+begin;size_t length=end-begin;
	if (prefix(body,length,"IF ")) {conditional(angle,end,false);return;}
	if (prefix(body,length,"ELSEIF ")) {conditional(angle,end,true);return;}
	if (length==4 && prefix(body,length,"ELSE")) {
		if (angle->depth==0 || angle->branches[angle->depth-1].seen_else) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
		struct branch *frame=&angle->branches[angle->depth-1];frame->seen_else=true;
		frame->active=frame->parent && !frame->selected;frame->selected|=frame->active;
	}
	else if (length==5 && prefix(body,length,"ENDIF")) {
		if (angle->depth==0) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}angle->depth--;
	}
	else {
		angle->request=(struct st_rip3_angle_request){.kind=ST_RIP3_ANGLE_REFERENCE};
		if (length!=0 && body[0]=='?') {
			uint8_t code=length==2 ? body[1] : 0;if (code>='a' && code<='z') code-='a'-'A';
			if (!((code>='0' && code<='9') || (code>='A' && code<='Z'))) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
			angle->request.kind=ST_RIP3_ANGLE_TEMPLATE;angle->request.number=(unsigned)(code<='9' ? code-'0' : code-'A'+10);
		}
		else {
			if (length==0 || length>255) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
			angle->reference[0]='$';angle->reference[1]='&';memcpy(angle->reference+2,body,length);angle->reference[length+2]='$';
			angle->reference[length+3]=0;
			if (!reference_valid(angle->reference,length+3)) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
			angle->request.reference=(struct st_rip3_condition_bytes){angle->reference,length+3};
		}
		angle->consumed=end+2-angle->cursor;angle->state=ST_RIP3_ANGLE_VALUE;return;
	}
	(void)splice(angle,end+2-angle->cursor,NULL,0);
}

void
st_rip3_angle_process(struct st_rip3_angle *angle, size_t budget)
{
	if (st_rip3_angle_state(angle)!=ST_RIP3_ANGLE_RUNNING) return;
	for (size_t step=0; step<budget && angle->state==ST_RIP3_ANGLE_RUNNING; step++) {
		if (angle->condition!=NULL) {condition_step(angle);continue;}
		if (angle->cursor==angle->length) {
			if (angle->depth!=0) {fail(angle,ST_RIP3_ANGLE_INVALID);return;}
			if (!angle->changed || angle->pass==8) {
				angle->state=ST_RIP3_ANGLE_COMPLETE;angle->result=(struct st_rip3_condition_bytes){angle->bytes,angle->length};return;
			}
			angle->pass++;angle->cursor=0;angle->changed=false;continue;
		}
		size_t at=angle->cursor;
		if (angle->length-at>=3 && angle->bytes[at]=='\\' && angle->bytes[at+1]=='<' && angle->bytes[at+2]=='<') {
			if (active(angle)) {if (splice(angle,1,NULL,0)) angle->cursor+=2;}
			else (void)splice(angle,3,NULL,0);
		}
		else if (angle->length-at>=2 && angle->bytes[at]=='<' && angle->bytes[at+1]=='<') header(angle);
		else {
			do {at++;} while (at<angle->length && !(angle->bytes[at]=='<' && at+1<angle->length && angle->bytes[at+1]=='<')
			    && !(angle->bytes[at]=='\\' && angle->length-at>=3 && angle->bytes[at+1]=='<' && angle->bytes[at+2]=='<'));
			if (active(angle)) angle->cursor=at;
			else (void)splice(angle,at-angle->cursor,NULL,0);
		}
	}
}
