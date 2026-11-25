/* User-variable prompt/effect matrix, separate from macro scanning and UI. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_user_variables.h"
#include "st_rip3_builtin_reference.h"
#include "st_rip3_graphical_bytes.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_user_variables {
	struct st_rip3_user_variable_host host;
	struct st_rip3_variable_reference reference;
	struct st_rip3_variable_value initial, result;
	enum st_rip3_user_variable_state state;
	struct st_rip3_variables *deleting_from;
	bool definition, require_confirmation;
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
	variables->deleting_from = NULL;
	variables->definition = variables->require_confirmation = false;
}

static bool
valid_reference(const struct st_rip3_variable_reference *reference, size_t limit)
{
	if (reference==NULL || reference->directives>63
	    || reference->mode<ST_RIP3_VARIABLE_ANY || reference->mode>ST_RIP3_VARIABLE_ALPHANUM
	    || reference->conversion<ST_RIP3_VARIABLE_UNCONVERTED || reference->conversion>ST_RIP3_VARIABLE_TOLOWER
	    || reference->question.length > limit || reference->default_value.length > limit
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

static int
lookup(const struct st_rip3_user_variables *variables, const char *name, size_t length,
    struct st_rip3_variable_entry *entry, struct st_rip3_variables **owner)
{
	if (st_rip3_bookmark_name(name, length))
		return IO_ERROR_NOT_FOUND;
	struct st_rip3_variables *store = variables->host.session;
	int status = st_rip3_variables_get_entry(store, name, length, entry);
	if (status == IO_ERROR_NOT_FOUND) {
		store = variables->host.persistent;
		status = st_rip3_variables_get_entry(store, name, length, entry);
	}
	if (status == 0 && owner != NULL)
		*owner = store;
	return status;
}

int
st_rip3_user_variables_inuse(const struct st_rip3_user_variables *variables,
    const char *name, size_t length, bool *defined)
{
	if (variables == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_variable_entry entry;
	int status = lookup(variables, name, length, &entry, NULL);
	if (status != 0 && status != IO_ERROR_NOT_FOUND)
		return status;
	*defined = status == 0;
	return 0;
}

static void
convert_value(struct st_rip3_variable_value *value, enum st_rip3_variable_conversion conversion)
{
	if (conversion == ST_RIP3_VARIABLE_UNCONVERTED)
		return;
	bool word_start = true;
	for (size_t i = 0; i < value->length; ++i) {
		uint8_t byte = value->bytes[i];
		bool upper = conversion == ST_RIP3_VARIABLE_TOUPPER
		    || (conversion == ST_RIP3_VARIABLE_TONAME && word_start);
		if (upper && byte >= 'a' && byte <= 'z')
			value->bytes[i] = (uint8_t)(byte - ('a' - 'A'));
		else if (!upper && byte >= 'A' && byte <= 'Z')
			value->bytes[i] = (uint8_t)(byte + ('a' - 'A'));
		word_start = byte == ' ' || (byte >= '\t' && byte <= '\r');
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
	else {
		value = *input;
		convert_value(&value, reference->conversion);
	}
	if (status==0 && value.length>255) status=IO_ERROR_OUT_OF_RANGE;
	if (status!=0) { failed(variables,status); return; }
	unsigned flags=reference->directives;
	struct st_rip3_variables *destination=(flags&ST_RIP3_VARIABLE_PERSISTENT)!=0 ? variables->host.persistent
	    : (flags&ST_RIP3_VARIABLE_SESSION)!=0 ? variables->host.session : NULL;
	if (variables->definition) {
		status = st_rip3_variables_define(variables->host.session,
		    (flags & ST_RIP3_VARIABLE_PERSISTENT) != 0 ? variables->host.persistent : NULL,
		    reference->name, strlen(reference->name), value.bytes, value.length, variables->require_confirmation);
	}
	else if (destination!=NULL)
		status=st_rip3_variables_set(destination,reference->name,strlen(reference->name),value.bytes,value.length);
	if (status!=0) { failed(variables,status); return; }
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
	if (!valid_reference(reference==NULL ? NULL : &copy, 100)) {
		diagnose(variables,ST_RIP3_USER_VARIABLE_INVALID);return;
	}
	if (st_rip3_bookmark_name(copy.name, strlen(copy.name))) {
		/* A reserved name expands to zero bytes, even with a supplied default
		 * or a directive that would otherwise open an editor or write a value. */
		variables->state = ST_RIP3_USER_VARIABLE_COMPLETE;
		return;
	}
	variables->reference=copy;
	struct st_rip3_variable_entry existing={0};
	struct st_rip3_variable_value candidate={0};
	int status=lookup(variables,copy.name,strlen(copy.name),&existing,NULL);
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
	else if (!define && retrieve && found) candidate=existing.value;
	else if (!define && retrieve && copy.has_default) {
		candidate.length=copy.default_value.length;
		memcpy(candidate.bytes,copy.default_value.bytes,candidate.length);
		source=ST_RIP3_USER_VARIABLE_DEFAULT;
	}
	else {
		if (found) variables->initial=existing.value;
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
st_rip3_user_variables_define(struct st_rip3_user_variables *variables,
    const struct st_rip3_variable_reference *reference, bool interactive, bool utf8, bool converted)
{
	if (variables == NULL)
		return;
	if (variables->state == ST_RIP3_USER_VARIABLE_PROMPT) {
		diagnose(variables, ST_RIP3_USER_VARIABLE_BUSY);
		return;
	}
	st_rip3_user_variables_abort(variables);
	if (!valid_reference(reference, sizeof(reference->question.bytes) - 1)) {
		diagnose(variables, ST_RIP3_USER_VARIABLE_INVALID);
		return;
	}
	if (st_rip3_bookmark_name(reference->name, strlen(reference->name))) {
		failed(variables, IO_ERROR_PERMISSION_DENIED);
		return;
	}
	variables->reference = *reference;
	variables->definition = true;
	struct st_rip3_variable_entry existing = {0};
	int status = lookup(variables, reference->name, strlen(reference->name), &existing, NULL);
	if (status != 0 && status != IO_ERROR_NOT_FOUND) {
		failed(variables, status);
		return;
	}
	variables->require_confirmation = existing.require_confirmation;
	struct st_rip3_variable_value value = {0};
	enum st_rip3_user_variable_source source = ST_RIP3_USER_VARIABLE_DEFAULT;
	if (reference->has_default) {
		value.length = reference->default_value.length;
		memcpy(value.bytes, reference->default_value.bytes, value.length);
	}
	else if (status == 0) {
		value = existing.value;
		source = ST_RIP3_USER_VARIABLE_EXISTING;
		const uint8_t *end = memchr(value.bytes, 0, value.length);
		if (end != NULL)
			value.length = (size_t)(end - value.bytes);
		size_t length = st_rip3_graphical_prefix(value.bytes, value.length, 60, converted);
		if (utf8 && length < value.length) {
			while (length != 0 && (value.bytes[length] & 0xc0) == 0x80)
				length--;
		}
		value.length = length;
		value.bytes[length] = 0;
	}
	if (interactive || variables->require_confirmation) {
		variables->initial = value;
		variables->state = ST_RIP3_USER_VARIABLE_PROMPT;
	}
	else
		finish(variables, &value, source);
}

int
st_rip3_user_variables_delete(struct st_rip3_user_variables *variables,
    const char *name, size_t length)
{
	if (length == 0 || length >= 20)
		return IO_ERROR_INVALID_FORMAT;
	if (st_rip3_bookmark_name(name, length))
		return IO_ERROR_PERMISSION_DENIED;
	if (variables == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (variables->state == ST_RIP3_USER_VARIABLE_PROMPT)
		return IO_ERROR_BUSY;
	struct st_rip3_variable_entry entry;
	struct st_rip3_variables *store = NULL;
	int status = lookup(variables, name, length, &entry, &store);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status != 0)
		return status;
	if (!entry.require_confirmation)
		return st_rip3_variables_remove(store, name, length);
	st_rip3_user_variables_abort(variables);
	memcpy(variables->reference.name, entry.name, sizeof(entry.name));
	variables->deleting_from = store;
	variables->state = ST_RIP3_USER_VARIABLE_PROMPT;
	return 0;
}

bool
st_rip3_user_variables_confirming_delete(const struct st_rip3_user_variables *variables)
{
	return st_rip3_user_variables_state(variables) == ST_RIP3_USER_VARIABLE_PROMPT
	    && variables->deleting_from != NULL;
}

void
st_rip3_user_variables_respond(struct st_rip3_user_variables *variables,
    const uint8_t *value, size_t length, bool cancelled)
{
	if (variables==NULL) return;
	if (variables->state!=ST_RIP3_USER_VARIABLE_PROMPT) {
		diagnose(variables,ST_RIP3_USER_VARIABLE_INVALID);return;
	}
	if (variables->deleting_from != NULL) {
		const char *name = variables->reference.name;
		int status = cancelled ? 0 : st_rip3_variables_remove(variables->deleting_from, name, strlen(name));
		if (status != 0)
			failed(variables, status);
		else {
			st_rip3_user_variables_abort(variables);
			variables->state = ST_RIP3_USER_VARIABLE_COMPLETE;
		}
		return;
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

void
st_rip3_user_variables_accept_definition(struct st_rip3_user_variables *variables,
    const uint8_t *value, size_t length, const bool *require_confirmation)
{
	if (st_rip3_user_variables_state(variables) != ST_RIP3_USER_VARIABLE_PROMPT || !variables->definition)
		return;
	if (require_confirmation != NULL)
		variables->require_confirmation = *require_confirmation;
	st_rip3_user_variables_respond(variables, value, length, false);
}

bool
st_rip3_user_variables_definition_confirmation(const struct st_rip3_user_variables *variables)
{
	return st_rip3_user_variables_state(variables) == ST_RIP3_USER_VARIABLE_PROMPT
	    && variables->definition && variables->require_confirmation;
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
