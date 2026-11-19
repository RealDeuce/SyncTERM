/* Conditional operand lexer; independent of dollar-argument escape rules. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_condition_internal.h"
#include "st_rip3_host_internal.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

struct st_rip3_condition_program {
	size_t count;
	struct st_rip3_condition_token tokens[];
};

struct st_rip3_condition_evaluation {
	size_t count, cursor;
	enum st_rip3_condition_evaluation_state state;
	bool result;
	struct st_rip3_condition_token tokens[];
};

struct scanner {
	const uint8_t *source;
	size_t length, at, count, payload;
	struct st_rip3_condition_token *tokens;
	uint8_t *storage;
};

static bool
space(uint8_t byte)
{
	return (byte>=9 && byte<=13) || byte==32;
}

static bool
alpha(uint8_t byte)
{
	return (byte>='A' && byte<='Z') || (byte>='a' && byte<='z');
}

static uint8_t
upper(uint8_t byte)
{
	return byte>='a' && byte<='z' ? (uint8_t)(byte-'a'+'A') : byte;
}

static void
whitespace(struct scanner *scanner)
{
	while (scanner->at<scanner->length && space(scanner->source[scanner->at])) scanner->at++;
}

static bool
keyword(struct scanner *scanner, const char *word)
{
	size_t length=strlen(word);
	if (length>scanner->length-scanner->at) return false;
	for (size_t i=0; i<length; i++)
		if (upper(scanner->source[scanner->at+i])!=(uint8_t)word[i]) return false;
	scanner->at+=length;return true;
}

static bool
terminator(const struct scanner *scanner)
{
	return scanner->length-scanner->at>=2 && scanner->source[scanner->at]=='>' && scanner->source[scanner->at+1]=='>';
}

static int
append(uint8_t bytes[255], size_t *length, uint8_t byte)
{
	if (*length==255) return IO_ERROR_OUT_OF_RANGE;
	bytes[(*length)++]=byte;return 0;
}

static int
left(struct scanner *scanner, uint8_t result[258], size_t *length)
{
	if (scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
	if (scanner->source[scanner->at]=='?') {
		if (++scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
		uint8_t code=upper(scanner->source[scanner->at++]);
		if (!((code>='0' && code<='9') || (code>='A' && code<='Z'))) return IO_ERROR_INVALID_FORMAT;
		/* Private typed-provider bridge: template tag plus numeric slot. */
		result[0]='?';result[1]=(uint8_t)(code<='9' ? code-'0' : code-'A'+10);*length=2;
		return 0;
	}
	bool delimited=scanner->source[scanner->at]=='$';
	if (delimited) scanner->at++;
	if (scanner->at==scanner->length || !alpha(scanner->source[scanner->at])) return IO_ERROR_INVALID_FORMAT;
	uint8_t body[255];size_t used=0;
	while (scanner->at<scanner->length) {
		uint8_t byte=scanner->source[scanner->at];
		if (byte=='\\') {
			if (++scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
			byte=scanner->source[scanner->at++];
		}
		else if (byte=='$') {
			if (!delimited) return IO_ERROR_INVALID_FORMAT;
			scanner->at++;delimited=false;break;
		}
		else if (!delimited && (space(byte) || byte=='=' || byte=='!' || byte=='<' || byte=='>')) break;
		else scanner->at++;
		int status=append(body,&used,byte);
		if (status!=0) return status;
	}
	if (delimited || scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
	result[0]='$';result[1]='&';memcpy(result+2,body,used);result[used+2]='$';*length=used+3;
	return 0;
}

static int
operation(struct scanner *scanner, enum st_rip3_condition_operator *result)
{
	whitespace(scanner);
	if (scanner->at==scanner->length || terminator(scanner)) return IO_ERROR_INVALID_FORMAT;
	uint8_t byte=scanner->source[scanner->at++];
	bool equal=scanner->at<scanner->length && scanner->source[scanner->at]=='=';
	switch (byte) {
	case '=':*result=ST_RIP3_CONDITION_EQUAL;return 0;
	case '!':if (!equal) return IO_ERROR_INVALID_FORMAT;*result=ST_RIP3_CONDITION_NOT_EQUAL;break;
	case '<':*result=equal ? ST_RIP3_CONDITION_LESS_EQUAL : ST_RIP3_CONDITION_LESS;break;
	case '>':*result=equal ? ST_RIP3_CONDITION_GREATER_EQUAL : ST_RIP3_CONDITION_GREATER;break;
	default:return IO_ERROR_INVALID_FORMAT;
	}
	if (equal) scanner->at++;
	return 0;
}

static int
right(struct scanner *scanner, uint8_t result[255], size_t *length)
{
	whitespace(scanner);
	if (scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
	bool quoted=scanner->source[scanner->at]=='"',closed=false;
	if (quoted) scanner->at++;
	size_t used=0;
	while (scanner->at<scanner->length) {
		uint8_t byte=scanner->source[scanner->at];
		if (!quoted && (space(byte) || byte==')')) break;
		if (byte=='\\') {
			if (++scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
			if (terminator(scanner)) {
				int status=append(result,&used,'>');
				if (status==0) status=append(result,&used,'>');
				if (status!=0) return status;
				scanner->at+=2;continue;
			}
			byte=scanner->source[scanner->at++];
		}
		else if (terminator(scanner)) break;
		else if (quoted && byte=='"') { scanner->at++;closed=true;break; }
		else scanner->at++;
		int status=append(result,&used,byte);
		if (status!=0) return status;
	}
	if (quoted && !closed) return IO_ERROR_INVALID_FORMAT;
	if (!quoted) while (used!=0 && space(result[used-1])) used--;
	*length=used;return 0;
}

static int
emit(struct scanner *scanner, enum st_rip3_condition_kind kind,
    enum st_rip3_condition_operator op, const uint8_t *reference, size_t reference_length,
    const uint8_t *literal, size_t literal_length)
{
	if (scanner->count==SIZE_MAX || reference_length>SIZE_MAX-scanner->payload
	    || literal_length>SIZE_MAX-scanner->payload-reference_length) return IO_ERROR_OUT_OF_RANGE;
	if (scanner->tokens!=NULL) {
		struct st_rip3_condition_token *token=&scanner->tokens[scanner->count];token->kind=kind;
		if (kind==ST_RIP3_CONDITION_COMPARISON) {
			token->value.comparison.operation=op;
			token->value.comparison.reference=(struct st_rip3_condition_bytes){scanner->storage,reference_length};
			memcpy(scanner->storage,reference,reference_length);scanner->storage+=reference_length;
			token->value.comparison.literal=(struct st_rip3_condition_bytes){scanner->storage,literal_length};
			if (literal_length!=0) memcpy(scanner->storage,literal,literal_length);
			scanner->storage+=literal_length;
		}
	}
	scanner->count++;scanner->payload+=reference_length+literal_length;return 0;
}

static int
scan(struct scanner *scanner)
{
	bool operand=true;size_t depth=0;
	for (;;) {
		whitespace(scanner);
		if (scanner->at==scanner->length) return IO_ERROR_INVALID_FORMAT;
		enum st_rip3_condition_kind kind;int status;
		if (operand) {
			if (keyword(scanner,"NOT ")) kind=ST_RIP3_CONDITION_NOT;
			else if (scanner->source[scanner->at]=='(') { scanner->at++;depth++;kind=ST_RIP3_CONDITION_OPEN; }
			else {
				uint8_t reference[258],literal[255];size_t reference_length,literal_length;
				enum st_rip3_condition_operator op;
				status=left(scanner,reference,&reference_length);
				if (status==0) status=operation(scanner,&op);
				if (status==0) status=right(scanner,literal,&literal_length);
				if (status!=0) return status;
				status=emit(scanner,ST_RIP3_CONDITION_COMPARISON,op,reference,reference_length,literal,literal_length);
				if (status!=0) return status;
				operand=false;continue;
			}
		}
		else if (terminator(scanner)) {
			if (depth!=0) return IO_ERROR_INVALID_FORMAT;
			scanner->at+=2;return 0;
		}
		else if (scanner->source[scanner->at]==')') {
			if (depth==0) return IO_ERROR_INVALID_FORMAT;
			depth--;scanner->at++;kind=ST_RIP3_CONDITION_CLOSE;
		}
		else if (keyword(scanner,"AND ")) { kind=ST_RIP3_CONDITION_AND;operand=true; }
		else if (keyword(scanner,"OR ")) { kind=ST_RIP3_CONDITION_OR;operand=true; }
		else return IO_ERROR_INVALID_FORMAT;
		status=emit(scanner,kind,ST_RIP3_CONDITION_EQUAL,NULL,0,NULL,0);
		if (status!=0) return status;
	}
}

static int
initialize(struct scanner *scanner, const uint8_t *source, size_t length)
{
	if (source==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (length==SIZE_MAX) return IO_ERROR_OUT_OF_RANGE;
	const uint8_t *end=memchr(source,0,length);
	*scanner=(struct scanner){.source=source,.length=end==NULL ? length : (size_t)(end-source)};
	return 0;
}

int
st_rip3_condition_source_validate(const uint8_t *source, size_t length, size_t *consumed)
{
	struct scanner scanner;int status=initialize(&scanner,source,length);
	if (status==0) status=scan(&scanner);
	if (status==0 && consumed!=NULL) *consumed=scanner.at;
	return status;
}

int
st_rip3_condition_program_parse(struct st_rip3_condition_program **result,
    const uint8_t *source, size_t length, size_t *consumed)
{
	if (result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct scanner scanner;int status=initialize(&scanner,source,length);
	if (status==0) status=scan(&scanner);
	if (status!=0) return status;
	size_t count=scanner.count,payload=scanner.payload;
	if (count>(SIZE_MAX-sizeof(struct st_rip3_condition_program))/sizeof(struct st_rip3_condition_token))
		return IO_ERROR_OUT_OF_RANGE;
	size_t size=sizeof(struct st_rip3_condition_program)+count*sizeof(struct st_rip3_condition_token);
	if (payload>SIZE_MAX-size) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_condition_program *program=calloc(1,size+payload);
	if (program==NULL) return IO_ERROR_OUT_OF_MEMORY;
	program->count=count;
	scanner.at=scanner.count=scanner.payload=0;scanner.tokens=program->tokens;
	scanner.storage=(uint8_t *)(program->tokens+count);
	status=scan(&scanner);
	if (status==0) status=st_rip3_condition_validate(program->tokens,count);
	if (status!=0) { free(program);return status; }
	*result=program;if (consumed!=NULL) *consumed=scanner.at;return 0;
}

void
st_rip3_condition_program_destroy(struct st_rip3_condition_program *program)
{
	free(program);
}

static bool
provider_reference_valid(const struct st_rip3_condition_bytes *reference)
{
	if (reference->length==2 && reference->bytes[0]=='?') return reference->bytes[1]<36;
	/* Validate the complete synthesized reference without executing a
	 * provider on an unintended prefix. The source lexer stays broader. */
	uint8_t ordinary[257];size_t size=reference->length-1;
	ordinary[0]='$';memcpy(ordinary+1,reference->bytes+2,reference->length-2);
	struct st_rip3_builtin_reference builtin;
	struct st_rip3_variable_reference user;
	return st_rip3_builtin_reference_parse(&builtin,ordinary,size)==0
	    || st_rip3_variable_reference_parse(&user,reference->bytes,reference->length)==0;
}

static int
provider_value(void *context, const struct st_rip3_condition_bytes *reference,
    struct st_rip3_condition_bytes *result)
{
	const struct st_rip3_condition_provider *provider=context;
	if (provider==NULL) return IO_ERROR_NOT_FOUND;
	int status;
	if (reference->length==2 && reference->bytes[0]=='?') {
		if (provider->template==NULL) return IO_ERROR_NOT_FOUND;
		status=provider->template(provider->context,reference->bytes[1],result);
	}
	else {
		if (!provider_reference_valid(reference)) return IO_ERROR_INVALID_FORMAT;
		if (provider->reference==NULL) return IO_ERROR_NOT_FOUND;
		status=provider->reference(provider->context,reference,result);
	}
	if (status==0 && (result->length>255 || (result->length!=0 && result->bytes==NULL)))
		status=IO_ERROR_OUT_OF_RANGE;
	return status;
}

int
st_rip3_condition_program_evaluate(bool *result, const struct st_rip3_condition_program *program,
    const struct st_rip3_condition_provider *provider)
{
	if (result==NULL || program==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_condition_evaluation *evaluation=NULL;
	int status=st_rip3_condition_evaluation_create(&evaluation,program);
	if (status!=0) return status;
	for (;;) {
		st_rip3_condition_evaluation_process(evaluation,SIZE_MAX);
		const struct st_rip3_condition_bytes *request=st_rip3_condition_evaluation_request(evaluation);
		if (request==NULL) break;
		struct st_rip3_condition_bytes value={0};
		status=provider_value((void *)provider,request,&value);
		st_rip3_condition_evaluation_supply(evaluation,&value,status);
	}
	status=st_rip3_condition_evaluation_result(evaluation,result) ? 0 : IO_ERROR_INVALID_FORMAT;
	st_rip3_condition_evaluation_destroy(evaluation);return status;
}

int
st_rip3_condition_evaluation_create(struct st_rip3_condition_evaluation **result,
    const struct st_rip3_condition_program *program)
{
	if (result==NULL || program==NULL) return IO_ERROR_INVALID_ARGUMENT;
	size_t count=program->count,payload=0;
	if (count>(SIZE_MAX-sizeof(struct st_rip3_condition_evaluation))/sizeof(struct st_rip3_condition_token))
		return IO_ERROR_OUT_OF_RANGE;
	size_t size=sizeof(struct st_rip3_condition_evaluation)+count*sizeof(struct st_rip3_condition_token);
	for (size_t i=0; i<count; i++) {
		if (program->tokens[i].kind!=ST_RIP3_CONDITION_COMPARISON) continue;
		const struct st_rip3_condition_comparison *comparison=&program->tokens[i].value.comparison;
		if (comparison->reference.length>SIZE_MAX-payload) return IO_ERROR_OUT_OF_RANGE;
		payload+=comparison->reference.length;
		if (comparison->literal.length>SIZE_MAX-payload) return IO_ERROR_OUT_OF_RANGE;
		payload+=comparison->literal.length;
	}
	if (payload>SIZE_MAX-size) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_condition_evaluation *evaluation=calloc(1,size+payload);
	if (evaluation==NULL) return IO_ERROR_OUT_OF_MEMORY;
	evaluation->count=count;
	memcpy(evaluation->tokens,program->tokens,count*sizeof(*program->tokens));
	uint8_t *storage=(uint8_t *)(evaluation->tokens+count);
	for (size_t i=0; i<count; i++) {
		if (evaluation->tokens[i].kind!=ST_RIP3_CONDITION_COMPARISON) continue;
		struct st_rip3_condition_comparison *comparison=&evaluation->tokens[i].value.comparison;
		memcpy(storage,comparison->reference.bytes,comparison->reference.length);
		comparison->reference.bytes=storage;storage+=comparison->reference.length;
		if (comparison->literal.length!=0) memcpy(storage,comparison->literal.bytes,comparison->literal.length);
		comparison->literal.bytes=storage;storage+=comparison->literal.length;
	}
	*result=evaluation;return 0;
}

void
st_rip3_condition_evaluation_destroy(struct st_rip3_condition_evaluation *evaluation)
{
	free(evaluation);
}

enum st_rip3_condition_evaluation_state
st_rip3_condition_evaluation_state(const struct st_rip3_condition_evaluation *evaluation)
{
	return evaluation==NULL ? ST_RIP3_CONDITION_FAILED : evaluation->state;
}

const struct st_rip3_condition_bytes *
st_rip3_condition_evaluation_request(const struct st_rip3_condition_evaluation *evaluation)
{
	return st_rip3_condition_evaluation_state(evaluation)==ST_RIP3_CONDITION_VALUE
	    ? &evaluation->tokens[evaluation->cursor].value.comparison.reference : NULL;
}

void
st_rip3_condition_evaluation_supply(struct st_rip3_condition_evaluation *evaluation,
    const struct st_rip3_condition_bytes *value, int status)
{
	if (st_rip3_condition_evaluation_state(evaluation)!=ST_RIP3_CONDITION_VALUE) return;
	struct st_rip3_condition_token *token=&evaluation->tokens[evaluation->cursor];
	bool answer=false;
	if (status==0 && value!=NULL && value->length<=255
	    && (value->length==0 || value->bytes!=NULL))
		(void)st_rip3_condition_compare(&answer,token->value.comparison.operation,value,&token->value.comparison.literal);
	*token=(struct st_rip3_condition_token){.kind=ST_RIP3_CONDITION_BOOLEAN,.value.boolean=answer};
	evaluation->cursor++;evaluation->state=ST_RIP3_CONDITION_RUNNING;
}

void
st_rip3_condition_evaluation_process(struct st_rip3_condition_evaluation *evaluation, size_t budget)
{
	if (st_rip3_condition_evaluation_state(evaluation)!=ST_RIP3_CONDITION_RUNNING || budget==0) return;
	for (size_t i=0; i<budget && evaluation->cursor<evaluation->count; i++) {
		struct st_rip3_condition_token *token=&evaluation->tokens[evaluation->cursor];
		if (token->kind==ST_RIP3_CONDITION_COMPARISON) {
			evaluation->state=ST_RIP3_CONDITION_VALUE;
			if (provider_reference_valid(&token->value.comparison.reference)) return;
			st_rip3_condition_evaluation_supply(evaluation,NULL,IO_ERROR_INVALID_FORMAT);
		}
		else evaluation->cursor++;
	}
	if (evaluation->cursor==evaluation->count) {
		int status=st_rip3_condition_reduce(&evaluation->result,evaluation->tokens,evaluation->count);
		evaluation->state=status==0 ? ST_RIP3_CONDITION_COMPLETE : ST_RIP3_CONDITION_FAILED;
	}
}

bool
st_rip3_condition_evaluation_result(const struct st_rip3_condition_evaluation *evaluation, bool *result)
{
	if (st_rip3_condition_evaluation_state(evaluation)!=ST_RIP3_CONDITION_COMPLETE || result==NULL) return false;
	*result=evaluation->result;return true;
}
