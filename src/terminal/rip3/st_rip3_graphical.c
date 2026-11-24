/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_graphical.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_playback.h"
#include "st_rip3_angle.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_templates.h"
#include "st_rip3_user_variables.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_graphical {
	const struct st_rip3_host_variables *variables;
	const struct st_rip3_host_sink *sink;
	struct st_rip3_angle *angle;
	struct st_rip3_user_variables *user;
	enum st_rip3_graphical_state state;
	enum st_rip3_graphical_consumer consumer;
	uint8_t bytes[4*4095+1];
	bool converted;
	size_t length,cursor,consumed,limit;
	struct st_rip3_condition_bytes result;
};

static void
failed(struct st_rip3_graphical *w, enum st_rip3_host_diagnostic kind)
{
	st_rip3_graphical_cancel(w);w->state=ST_RIP3_GRAPHICAL_FAILED;
	if (w->sink->diagnostic!=NULL) w->sink->diagnostic(w->sink->context,kind);
}

int
st_rip3_graphical_create(struct st_rip3_graphical **result,
    const struct st_rip3_host_variables *variables, const struct st_rip3_host_sink *sink, bool converted_codepage)
{
	if (result==NULL || variables==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_graphical *w=calloc(1,sizeof(*w));
	if (w==NULL) return IO_ERROR_OUT_OF_MEMORY;
	w->variables=variables;w->sink=sink;w->converted=converted_codepage;*result=w;return 0;
}

void
st_rip3_graphical_cancel(struct st_rip3_graphical *w)
{
	if (w==NULL) return;
	if (w->user!=NULL) st_rip3_user_variables_abort(w->user);
	w->user=NULL;st_rip3_angle_destroy(w->angle);w->angle=NULL;
	w->state=ST_RIP3_GRAPHICAL_IDLE;w->length=w->cursor=0;
}

void
st_rip3_graphical_destroy(struct st_rip3_graphical *w)
{
	st_rip3_graphical_cancel(w);free(w);
}

enum st_rip3_graphical_state
st_rip3_graphical_state(const struct st_rip3_graphical *w)
{
	return w==NULL ? ST_RIP3_GRAPHICAL_IDLE : w->state;
}

bool
st_rip3_graphical_pending(const struct st_rip3_graphical *w)
{
	return w!=NULL && (w->state==ST_RIP3_GRAPHICAL_RUNNING || w->state==ST_RIP3_GRAPHICAL_PROMPT);
}

const struct st_rip3_condition_bytes *
st_rip3_graphical_result(const struct st_rip3_graphical *w)
{
	return w!=NULL && w->state==ST_RIP3_GRAPHICAL_COMPLETE ? &w->result : NULL;
}

void
st_rip3_graphical_start(struct st_rip3_graphical *w, enum st_rip3_graphical_consumer consumer,
    const uint8_t *bytes, size_t length)
{
	if (w==NULL) return;
	if (st_rip3_graphical_pending(w)) {
		if (w->sink->diagnostic!=NULL) w->sink->diagnostic(w->sink->context,ST_RIP3_HOST_OPERATION_FAILED);
		return;
	}
	st_rip3_graphical_cancel(w);
	if ((bytes==NULL && length!=0) || consumer<ST_RIP3_GRAPHICAL_FILENAME || consumer>ST_RIP3_GRAPHICAL_WAVE) {
		failed(w,ST_RIP3_HOST_INVALID_REFERENCE);return;
	}
	w->consumer=consumer;
	switch (consumer) {
	case ST_RIP3_GRAPHICAL_FILENAME:
	case ST_RIP3_GRAPHICAL_LABEL:
		w->limit = 255;
		break;
	case ST_RIP3_GRAPHICAL_TEXT:
	case ST_RIP3_GRAPHICAL_METRIC:
		w->limit = 1022;
		break;
	case ST_RIP3_GRAPHICAL_IMAGE:
		w->limit = 1024;
		break;
	case ST_RIP3_GRAPHICAL_FONT:
	case ST_RIP3_GRAPHICAL_SCENE:
	case ST_RIP3_GRAPHICAL_WAVE:
		w->limit = 4095;
		break;
	}
	length=st_rip3_graphical_prefix(bytes,length,w->limit,w->converted);
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	bool angles=false;
	for (size_t i=1;i<length;i++) if (bytes[i-1]=='<' && bytes[i]=='<') {angles=true;break;}
	if (!angles) {
		if (length!=0) memcpy(w->bytes,bytes,length);
		w->length=length;w->bytes[length]=0;w->state=ST_RIP3_GRAPHICAL_RUNNING;return;
	}
	int status=st_rip3_angle_create_graphical(&w->angle,bytes,length,w->limit,w->converted);
	if (status!=0) {failed(w,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_HOST_RESOURCE_FAILURE : ST_RIP3_HOST_INVALID_REFERENCE);return;}
	w->state=ST_RIP3_GRAPHICAL_RUNNING;
}

/* Splicing advances over inserted bytes: there is no second ordinary pass. */
static void
replace(struct st_rip3_graphical *w, const uint8_t *bytes, size_t length)
{
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	size_t remaining=w->limit-st_rip3_graphical_units(w->bytes,w->cursor,w->converted);
	length=st_rip3_graphical_prefix(bytes,length,remaining,w->converted);
	remaining-=st_rip3_graphical_units(bytes,length,w->converted);
	size_t suffix=st_rip3_graphical_prefix(w->bytes+w->cursor+w->consumed,
	    w->length-w->cursor-w->consumed,remaining,w->converted);
	memmove(w->bytes+w->cursor+length,w->bytes+w->cursor+w->consumed,suffix);
	if (length!=0) memcpy(w->bytes+w->cursor,bytes,length);
	w->length=w->cursor+length+suffix;w->cursor+=length;w->bytes[w->length]=0;
}

struct value {uint8_t bytes[4*1022+1];size_t length;bool ended;int status;};
static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct value *value=context;
	if (value->ended || value->status!=0) return;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) {length=(size_t)(end-bytes);value->ended=true;}
	if (length>sizeof(value->bytes)-1-value->length) length=sizeof(value->bytes)-1-value->length;
	if (length!=0) memcpy(value->bytes+value->length,bytes,length);
	value->length+=length;
}
static void
rejected(void *context, enum st_rip3_host_diagnostic kind)
{
	struct value *value=context;
	value->status=kind==ST_RIP3_HOST_ABORTED ? IO_ERROR_CANCELLED
	    : kind==ST_RIP3_HOST_RESOURCE_FAILURE ? IO_ERROR_OUT_OF_MEMORY
	    : kind==ST_RIP3_HOST_INVALID_REFERENCE ? IO_ERROR_INVALID_FORMAT : IO_ERROR_NOT_SUPPORTED;
}

static bool
known(const uint8_t *bytes, size_t length)
{
	size_t at=1;
	while (at<length && (bytes[at]=='&' || bytes[at]=='-' || bytes[at]=='=' || bytes[at]=='+' || bytes[at]=='*' || bytes[at]=='!')) at++;
	size_t begin=at;
	while (at<length && ((bytes[at]>='a' && bytes[at]<='z') || (bytes[at]>='A' && bytes[at]<='Z')
	    || (bytes[at]>='0' && bytes[at]<='9') || bytes[at]=='_')) at++;
	return st_rip3_builtin_known((const char *)bytes+begin,at-begin);
}

static void
reference(struct st_rip3_graphical *w, const uint8_t *bytes, size_t length, bool angle)
{
	const uint8_t *source=bytes;size_t source_length=length;
	uint8_t ordinary[259];
	if (angle) {
		size_t start=1;
		while (start<length && bytes[start]=='&') start++;
		ordinary[0]='$';memcpy(ordinary+1,bytes+start,length-start);
		length=length-start+1;bytes=ordinary;
	}
	struct st_rip3_builtin_reference builtin;
	if (st_rip3_builtin_reference_parse(&builtin,bytes,length)==0
	    && st_rip3_builtin_known(builtin.name,strlen(builtin.name))) {
		struct value value={0};
		size_t name_length = strlen(builtin.name);
		if (!st_rip3_bookmark_name(builtin.name, name_length)
		    && !st_rip3_builtin_graphical(builtin.name, name_length)) value.status=IO_ERROR_NOT_SUPPORTED;
		else {
			const struct st_rip3_host_sink sink={&value,reply,rejected};
			st_rip3_host_variables_process(w->variables,&sink,bytes,length);
		}
		if (value.status==IO_ERROR_CANCELLED) failed(w,ST_RIP3_HOST_ABORTED);
		else if (angle) {
			const struct st_rip3_condition_bytes result={value.bytes,value.length};
			st_rip3_angle_supply(w->angle,&result,value.status);
		}
		else if (value.status==0) replace(w,value.bytes,value.length);
		else {
			if (value.status == IO_ERROR_INVALID_FORMAT && strcmp(builtin.name, "FILEEXISTS") == 0
			    && w->sink->diagnostic != NULL)
				w->sink->diagnostic(w->sink->context, ST_RIP3_HOST_INVALID_REFERENCE);
			w->cursor += w->consumed; /* Keep the rejected reference literal. */
		}
		return;
	}
	struct st_rip3_variable_reference user;
	bool parsed = st_rip3_variable_reference_parse(&user, source, source_length) == 0;
	bool bookmark = parsed && st_rip3_bookmark_name(user.name, strlen(user.name));
	if (!bookmark && (known(bytes,length) || (!angle && w->consumer==ST_RIP3_GRAPHICAL_METRIC))) {
		if (angle) st_rip3_angle_supply(w->angle,NULL,IO_ERROR_NOT_SUPPORTED);
		else w->cursor+=w->consumed;
		return;
	}
	if (w->variables->user_variables==NULL || !parsed
	    || st_rip3_user_variables_state(w->variables->user_variables)==ST_RIP3_USER_VARIABLE_PROMPT) {
		failed(w,ST_RIP3_HOST_INVALID_REFERENCE);return;
	}
	w->user=w->variables->user_variables;st_rip3_user_variables_process(w->user,&user);
}

static bool
user_result(struct st_rip3_graphical *w)
{
	if (st_rip3_user_variables_state(w->user)==ST_RIP3_USER_VARIABLE_PROMPT) {
		w->state=ST_RIP3_GRAPHICAL_PROMPT;return false;
	}
	const struct st_rip3_variable_value *value=st_rip3_user_variables_result(w->user);
	if (value==NULL) {failed(w,ST_RIP3_HOST_OPERATION_FAILED);return false;}
	if (w->angle!=NULL) {
		const struct st_rip3_condition_bytes result={value->bytes,value->length};
		st_rip3_angle_supply(w->angle,&result,0);
	}
	else replace(w,value->bytes,value->length);
	st_rip3_user_variables_abort(w->user);w->user=NULL;return true;
}

static void
angle_step(struct st_rip3_graphical *w)
{
	st_rip3_angle_process(w->angle,1);
	const struct st_rip3_angle_request *request=st_rip3_angle_request(w->angle);
	if (request!=NULL) {
		if (request->kind==ST_RIP3_ANGLE_REFERENCE) reference(w,request->reference.bytes,request->reference.length,true);
		else {
			struct st_rip3_template_bytes value;
			const struct st_rip3_template_host *host=w->variables->templates;
			int status=host==NULL || host->slots==NULL ? IO_ERROR_NOT_SUPPORTED : st_rip3_templates_resolve(host->slots,request->number,&value);
			if (status==IO_ERROR_NOT_FOUND && host->selection_required!=NULL) host->selection_required(host->context,request->number);
			const struct st_rip3_condition_bytes bytes={status==0 ? value.bytes : NULL,status==0 ? value.length : 0};
			st_rip3_angle_supply(w->angle,&bytes,status);
		}
	}
	if (w->state==ST_RIP3_GRAPHICAL_FAILED) return;
	if (st_rip3_angle_state(w->angle)==ST_RIP3_ANGLE_FAILED) {
		enum st_rip3_angle_diagnostic kind=st_rip3_angle_diagnostic(w->angle);
		failed(w,kind==ST_RIP3_ANGLE_RESOURCE ? ST_RIP3_HOST_RESOURCE_FAILURE
		    : kind==ST_RIP3_ANGLE_INVALID ? ST_RIP3_HOST_INVALID_REFERENCE : ST_RIP3_HOST_OPERATION_FAILED);return;
	}
	const struct st_rip3_condition_bytes *result=st_rip3_angle_result(w->angle);
	if (result!=NULL) {
		memcpy(w->bytes,result->bytes,result->length);w->length=result->length;w->bytes[w->length]=0;
		st_rip3_angle_destroy(w->angle);w->angle=NULL;w->cursor=0;
	}
}

static size_t
reference_length(const uint8_t *bytes, size_t length)
{
	bool arguments=false;
	for (size_t i=1;i<length;i++) {
		if (bytes[i]=='\\' && i+1<length) {
			if (!arguments && bytes[i+1]=='$') return 0;
			i++;continue;
		}
		if (bytes[i]=='(') arguments=true;
		else if (bytes[i]==')') arguments=false;
		if (bytes[i]=='$') return i+1;
	}
	return 0;
}

void
st_rip3_graphical_process(struct st_rip3_graphical *w, size_t budget)
{
	if (w==NULL) return;
	for (size_t step=0;step<budget && st_rip3_graphical_pending(w);step++) {
		w->state=ST_RIP3_GRAPHICAL_RUNNING;
		if (w->user!=NULL) {if (!user_result(w)) return;continue;}
		if (w->angle!=NULL) {angle_step(w);continue;}
		if (w->cursor==w->length) {
			w->result=(struct st_rip3_condition_bytes){w->bytes,w->length};w->state=ST_RIP3_GRAPHICAL_COMPLETE;return;
		}
		uint8_t *bytes=w->bytes+w->cursor;size_t left=w->length-w->cursor;
		if (bytes[0]=='\\') {
			size_t count=left<=1 ? 0 : w->converted ? st_rip3_graphical_character(bytes+1,left-1) : 1;
			uint8_t quoted[4];if (count!=0) memcpy(quoted,bytes+1,count);
			w->consumed=1+count;replace(w,quoted,count);continue;
		}
		if (bytes[0]=='$') {
			if (left>1 && st_rip3_playback_prefix(bytes[1])) {
				size_t span=st_rip3_playback_token(bytes,left);
				w->cursor+=span!=0 ? span : 1;
				continue;
			}
			w->consumed=reference_length(bytes,left);
			if (w->consumed!=0) {
				if (bytes[1]=='?')
					w->cursor+=w->consumed;
				else reference(w,bytes,w->consumed,false);
				continue;
			}
		}
		do {w->cursor++;} while (w->cursor<w->length && w->bytes[w->cursor]!='$' && w->bytes[w->cursor]!='\\');
	}
}
