/* User-variable prompt/effect matrix, separate from macro scanning and UI. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_user_variables.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_user_variables {
	struct st_rip3_user_variable_host host;
	struct st_rip3_variable_reference reference;
	struct st_rip3_variable_value initial, result;
	enum st_rip3_user_variable_state state;
};

static void
diagnose(struct st_rip3_user_variables *variables,
    enum st_rip3_user_variable_diagnostic kind)
{
	if (variables!=NULL && variables->host.diagnostic!=NULL)
		variables->host.diagnostic(variables->host.context,kind);
}

static void
failed(struct st_rip3_user_variables *variables, int status)
{
	st_rip3_user_variables_abort(variables);
	diagnose(variables,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_USER_VARIABLE_RESOURCE_FAILED
	    : ST_RIP3_USER_VARIABLE_OPERATION_FAILED);
}

int
st_rip3_user_variables_create(struct st_rip3_user_variables **result,
    const struct st_rip3_user_variable_host *host)
{
	if (result==NULL || host==NULL || host->session==NULL || host->persistent==NULL
	    || host->session==host->persistent) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_user_variables *variables=calloc(1,sizeof(*variables));
	if (variables==NULL) return IO_ERROR_OUT_OF_MEMORY;
	variables->host=*host;
	*result=variables;
	return 0;
}

void
st_rip3_user_variables_destroy(struct st_rip3_user_variables *variables)
{
	free(variables);
}

void
st_rip3_user_variables_abort(struct st_rip3_user_variables *variables)
{
	if (variables==NULL) return;
	variables->reference=(struct st_rip3_variable_reference){0};
	variables->initial=(struct st_rip3_variable_value){0};
	variables->result=(struct st_rip3_variable_value){0};
	variables->state=ST_RIP3_USER_VARIABLE_IDLE;
}

static bool
valid_reference(const struct st_rip3_variable_reference *reference)
{
	if (reference==NULL || reference->directives>63
	    || reference->mode<ST_RIP3_VARIABLE_ANY || reference->mode>ST_RIP3_VARIABLE_ALPHANUM
	    || reference->conversion<ST_RIP3_VARIABLE_UNCONVERTED || reference->conversion>ST_RIP3_VARIABLE_TOLOWER
	    || reference->question.length>100 || reference->default_value.length>100
	    || memchr(reference->name,0,sizeof(reference->name))==NULL)
		return false;
	/* Use the same ASCII identifier contract as the owned tables. */
	for (size_t i=0; ; i++) {
		unsigned char c=(unsigned char)reference->name[i];
		if (c==0) return i!=0;
		bool letter=(c>='A' && c<='Z') || (c>='a' && c<='z');
		if (!letter && (i==0 || (c!='_' && !(c>='0' && c<='9')))) return false;
	}
}

static void
finish(struct st_rip3_user_variables *variables, const struct st_rip3_variable_value *input,
    enum st_rip3_user_variable_source source)
{
	struct st_rip3_variable_value value={0};
	const struct st_rip3_variable_reference *reference=&variables->reference;
	int status=0;
	if (variables->host.format!=NULL)
		status=variables->host.format(variables->host.context,reference,source,input,&value);
	else if (reference->mode!=ST_RIP3_VARIABLE_ANY || reference->conversion!=ST_RIP3_VARIABLE_UNCONVERTED)
		status=IO_ERROR_NOT_SUPPORTED;
	else value=*input;
	if (status==0 && value.length>255) status=IO_ERROR_OUT_OF_RANGE;
	if (status!=0) { failed(variables,status); return; }
	unsigned flags=reference->directives;
	struct st_rip3_variables *destination=(flags&ST_RIP3_VARIABLE_PERSISTENT)!=0 ? variables->host.persistent
	    : (flags&ST_RIP3_VARIABLE_SESSION)!=0 ? variables->host.session : NULL;
	if (destination!=NULL) {
		status=st_rip3_variables_set(destination,reference->name,strlen(reference->name),value.bytes,value.length);
		if (status!=0) { failed(variables,status); return; }
	}
	struct st_rip3_variable_value result={0};
	if ((flags&ST_RIP3_VARIABLE_SET)==0 || (flags&ST_RIP3_VARIABLE_GET)!=0) {
		if (value.length==0) { result.length=4;memcpy(result.bytes,"NULL",4); }
		else { result.length=value.length;memcpy(result.bytes,value.bytes,value.length); }
	}
	variables->initial=(struct st_rip3_variable_value){0};
	variables->result=result;
	variables->state=ST_RIP3_USER_VARIABLE_COMPLETE;
}

void
st_rip3_user_variables_process(struct st_rip3_user_variables *variables,
    const struct st_rip3_variable_reference *reference)
{
	if (variables==NULL) return;
	if (variables->state==ST_RIP3_USER_VARIABLE_PROMPT) {
		diagnose(variables,ST_RIP3_USER_VARIABLE_BUSY);return;
	}
	/* Copy before resetting, permitting reuse of a borrowed internal reference. */
	struct st_rip3_variable_reference copy;
	if (reference!=NULL) copy=*reference;
	st_rip3_user_variables_abort(variables);
	if (!valid_reference(reference==NULL ? NULL : &copy)) {
		diagnose(variables,ST_RIP3_USER_VARIABLE_INVALID);return;
	}
	if (variables->host.format==NULL && (copy.mode!=ST_RIP3_VARIABLE_ANY
	    || copy.conversion!=ST_RIP3_VARIABLE_UNCONVERTED)) {
		failed(variables,IO_ERROR_NOT_SUPPORTED);return;
	}
	variables->reference=copy;
	struct st_rip3_variable_value existing={0},candidate={0};
	size_t length=strlen(copy.name);
	int status=st_rip3_variables_get(variables->host.session,copy.name,length,&existing);
	if (status==IO_ERROR_NOT_FOUND)
		status=st_rip3_variables_get(variables->host.persistent,copy.name,length,&existing);
	if (status!=0 && status!=IO_ERROR_NOT_FOUND) { failed(variables,status);return; }
	bool found=status==0, define=(copy.directives&ST_RIP3_VARIABLE_SET)!=0;
	bool retrieve=(copy.directives&ST_RIP3_VARIABLE_GET)!=0;
	enum st_rip3_user_variable_source source=ST_RIP3_USER_VARIABLE_EXISTING;
	/* '-' selects its supplied default even when an old value exists. '&'
	 * alone prefers an existing value. All other cases need a prompt. */
	if (define && copy.has_default) {
		candidate.length=copy.default_value.length;
		memcpy(candidate.bytes,copy.default_value.bytes,candidate.length);
		source=ST_RIP3_USER_VARIABLE_DEFAULT;
	}
	else if (!define && retrieve && found) candidate=existing;
	else if (!define && retrieve && copy.has_default) {
		candidate.length=copy.default_value.length;
		memcpy(candidate.bytes,copy.default_value.bytes,candidate.length);
		source=ST_RIP3_USER_VARIABLE_DEFAULT;
	}
	else {
		if (found) variables->initial=existing;
		else if (copy.has_default) {
			variables->initial.length=copy.default_value.length;
			memcpy(variables->initial.bytes,copy.default_value.bytes,copy.default_value.length);
		}
		variables->state=ST_RIP3_USER_VARIABLE_PROMPT;
		return;
	}
	finish(variables,&candidate,source);
}

void
st_rip3_user_variables_respond(struct st_rip3_user_variables *variables,
    const uint8_t *value, size_t length, bool cancelled)
{
	if (variables==NULL) return;
	if (variables->state!=ST_RIP3_USER_VARIABLE_PROMPT) {
		diagnose(variables,ST_RIP3_USER_VARIABLE_INVALID);return;
	}
	if (cancelled) {
		unsigned flags=variables->reference.directives;
		if ((flags&ST_RIP3_VARIABLE_REQUIRED)!=0) {
			diagnose(variables,ST_RIP3_USER_VARIABLE_CANCELLATION_DISABLED);return;
		}
		variables->result=(struct st_rip3_variable_value){0};
		if ((flags&(ST_RIP3_VARIABLE_SET|ST_RIP3_VARIABLE_GET))==0) {
			variables->result.length=4;memcpy(variables->result.bytes,"NONE",4);
		}
		variables->initial=(struct st_rip3_variable_value){0};
		variables->state=ST_RIP3_USER_VARIABLE_COMPLETE;
		return;
	}
	if (length!=0 && value==NULL) {
		st_rip3_user_variables_abort(variables);
		diagnose(variables,ST_RIP3_USER_VARIABLE_INVALID);return;
	}
	struct st_rip3_variable_value accepted={0};
	accepted.length=length>255 ? 255 : length;
	if (accepted.length!=0) memcpy(accepted.bytes,value,accepted.length);
	finish(variables,&accepted,ST_RIP3_USER_VARIABLE_RESPONSE);
}

enum st_rip3_user_variable_state
st_rip3_user_variables_state(const struct st_rip3_user_variables *variables)
{
	return variables==NULL ? ST_RIP3_USER_VARIABLE_IDLE : variables->state;
}

const struct st_rip3_variable_reference *
st_rip3_user_variables_prompt(const struct st_rip3_user_variables *variables)
{
	return st_rip3_user_variables_state(variables)==ST_RIP3_USER_VARIABLE_PROMPT ? &variables->reference : NULL;
}

const struct st_rip3_variable_value *
st_rip3_user_variables_initial(const struct st_rip3_user_variables *variables)
{
	return st_rip3_user_variables_state(variables)==ST_RIP3_USER_VARIABLE_PROMPT ? &variables->initial : NULL;
}

const struct st_rip3_variable_value *
st_rip3_user_variables_result(const struct st_rip3_user_variables *variables)
{
	return st_rip3_user_variables_state(variables)==ST_RIP3_USER_VARIABLE_COMPLETE ? &variables->result : NULL;
}
