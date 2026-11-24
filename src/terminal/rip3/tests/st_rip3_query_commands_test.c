/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_stream.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_templates.h"
#include "st_rip3_local_variables.h"
#include "st_rip3_mouse.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host host;
	struct st_rip3_host_variables variables;
	struct st_rip3_host_sink query_sink;
	struct st_rip3_frame frame;
	struct st_rip3_variables *session,*persistent;
	struct st_rip3_template_host templates;
	unsigned required_calls,required_group;
	unsigned user_errors;
	enum st_rip3_user_variable_diagnostic user_kind;
	struct st_rip3_stream *stream;
	uint64_t now;
	uint8_t queue[2048], text[2048], reply[8192];
	size_t length, read, text_length, reply_length, replies;
	unsigned reads, errors, host_errors;
	enum st_rip3_command_diagnostic kind;
	enum st_rip3_host_diagnostic host_kind;
	bool overflow;
};

static uint64_t
now(void *context)
{
	return ((struct fixture *)context)->now;
}

static bool
next(void *context, uint8_t *byte)
{
	struct fixture *f = context;
	f->reads++;
	if (f->read == f->length) return false;
	*byte = f->queue[f->read++]; return true;
}

static void
consume(void *context, uint8_t byte)
{
	struct fixture *f = context;
	st_rip3_stream_process(f->stream,&byte,1);
}

static void
text(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	if (length > sizeof(f->text)-f->text_length) { f->overflow = true; return; }
	memcpy(f->text+f->text_length,bytes,length); f->text_length += length;
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	if (length > sizeof(f->reply)-f->reply_length) { f->overflow = true; return; }
	memcpy(f->reply+f->reply_length,bytes,length); f->reply_length += length; f->replies++;
}

static void
host_diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct fixture *f = context;
	f->host_errors++; f->host_kind = kind;
}

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context;
	f->errors++; f->kind = kind;
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	const struct st_rip3_command_sink sink = {f,diagnostic};
	st_rip3_commands_process(&f->host,&sink,bytes,length);
}

static bool
prepare(void *context)
{
	struct fixture *f = context;
	st_rip3_query_process(f->host.query,64);
	return !st_rip3_query_pending(f->host.query);
}

static int required_group(struct st_rip3_templates *t,unsigned n,bool required)
{
	uint64_t redraw[128];size_t count;
	if (required) return st_rip3_templates_button_add(t,1000+n,n,ST_RIP3_GROUP_RADIO,false,redraw,&count);
	st_rip3_templates_button_remove(t,1000+n);return 0;
}
static int
create(struct fixture *f, enum st_rip3_canvas canvas)
{
	static const struct st_rip3_terminal_identity identity = {"Fixture","Tests","0"};
	const struct st_rip3_input_source source = {.context=f,.now_ns=now,.next=next,.consume=consume,.prepare=prepare};
	const struct st_rip3_stream_sink sink = {.context=f,.text=text,.command=command};
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_input_create(&f->host.input,&source) == 0);
	CHECK(st_rip3_stream_create(&f->stream,&sink) == 0);
	CHECK(st_rip3_frame_init(&f->frame,canvas) == 0);
	f->variables.frame = &f->frame; f->variables.identity = &identity; f->variables.input = f->host.input;
	f->query_sink = (struct st_rip3_host_sink){f,reply,host_diagnostic};
	CHECK(st_rip3_query_create(&f->host.query,&f->variables,&f->query_sink) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_query_destroy(f->host.query);
	st_rip3_user_variables_destroy(f->variables.user_variables);
	st_rip3_templates_destroy(f->templates.slots);
	st_rip3_variables_destroy(f->session);st_rip3_variables_destroy(f->persistent);
	st_rip3_input_destroy(f->host.input); st_rip3_stream_destroy(f->stream);
	st_rip3_environments_destroy(f->host.environments);
}

static int
append(struct fixture *f, const char *bytes)
{
	size_t length = strlen(bytes);
	CHECK(length <= sizeof(f->queue)-f->length);
	memcpy(f->queue+f->length,bytes,length); f->length += length;
	return 0;
}

static int
process(struct fixture *f, const char *bytes, int error)
{
	unsigned previous = f->errors;
	command(f,(const uint8_t *)bytes,strlen(bytes));
	CHECK(f->errors == previous+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->kind == error);
	CHECK(!f->overflow);
	return 0;
}

static int
reference(struct fixture *f, const char *bytes, int error)
{
	unsigned previous = f->host_errors;
	st_rip3_host_variables_process(&f->variables,&f->query_sink,(const uint8_t *)bytes,strlen(bytes));
	CHECK(f->host_errors == previous+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->host_kind == error);
	CHECK(!f->overflow);
	return 0;
}

static int
pipeline(enum st_rip3_canvas canvas, const char *width)
{
	struct fixture f = {0};
	struct st_rip3_environment environment;
	const char *first = "start\r\n!|1\0330000$D(1)$|";
	CHECK(create(&f,canvas) == 0);
	CHECK(append(&f,first) == 0 && append(&f,"J1S|1\0330000$RESX$|1\0330000$D(60)$|1\0330000$TERMINFO(NAME)$\r") == 0);
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == strlen(first) && f.reply_length == 0 && st_rip3_input_wait_ns(f.host.input) == 16666667);
	CHECK(append(&f,"raw \xb3\r\n\xe2\x98\x83\r\n") == 0);
	unsigned reads = f.reads;
	f.now = 16666666; st_rip3_input_process(f.host.input,2048);
	CHECK(f.reads == reads && f.reply_length == 0);
	f.now++;
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length == strlen(width) && memcmp(f.reply,width,f.reply_length) == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 1000000000);
	f.now += 999999999; st_rip3_input_process(f.host.input,2048);
	CHECK(f.replies == 1 && st_rip3_input_wait_ns(f.host.input) == 1);
	f.now++; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == f.length && f.replies == 2 && f.reply_length == strlen(width)+7);
	CHECK(memcmp(f.reply+strlen(width),"Fixture",7) == 0);
	const char expected[] = "start\r\nraw \xb3\r\n\xe2\x98\x83\r\n";
	CHECK(f.text_length == sizeof(expected)-1 && memcmp(f.text,expected,sizeof(expected)-1) == 0);
	CHECK(f.errors == 0 && f.host_errors == 0 && !f.overflow);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 64);
	destroy(&f);
	return 0;
}

static int
fields(void)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	const struct { const char *text; uint64_t duration; } valid[] = {
		{"$D(1)$",16666667},{"$d(2)$",33333334},{"$D(3)$",50000000},
		{"$D(60)$",1000000000},{"$D(000000000060)$",1000000000},
		{"$D(65535)$",1092250000000},
		/* Leading zeros count against the documented 20-byte argument slot,
		 * not the old decoder's artificial twelve-byte ceiling. */
		{"$D(0000000000001)$",16666667},{"$D(00000000000000000001)$",16666667},
	};
	for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); i++) {
		CHECK(reference(&f,valid[i].text,-1) == 0 && st_rip3_input_wait_ns(f.host.input) == valid[i].duration);
		CHECK(f.replies == 0);
	}
	const char *const invalid[] = {"$D$","$D()$","$D(0)$","$D(-1)$","$D(+1)$","$D(1.0)$",
	    "$D(1O)$","$D(65536)$","$D(999999999999)$","$D(000000000000000000001)$","$D(1,2)$"};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		st_rip3_input_delay(f.host.input,60);
		CHECK(reference(&f,invalid[i],ST_RIP3_HOST_INVALID_REFERENCE) == 0);
		CHECK(st_rip3_input_wait_ns(f.host.input) == 1000000000 && f.replies == 0);
	}
	f.variables.input = NULL;
	CHECK(reference(&f,"$D(1)$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 1000000000);
	f.variables.input = f.host.input;
	struct st_rip3_builtin_reference decoded;
	char output = 'K';
	CHECK(st_rip3_builtin_reference_parse(&decoded,(const uint8_t *)"$D(1)$",6) == 0);
	CHECK(st_rip3_host_variables_execute(&f.variables,&decoded,&output,0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(output == 'K' && st_rip3_input_wait_ns(f.host.input) == 1000000000);
	CHECK(reference(&f,"$D(1)$",-1) == 0 && st_rip3_input_wait_ns(f.host.input) == 16666667);
	st_rip3_input_reset(f.host.input);
	CHECK(reference(&f,"$TERMINFO(NAME)$",-1) == 0 && f.reply_length == 7 && memcmp(f.reply,"Fixture",7) == 0);
	destroy(&f);
	return 0;
}

static int
query_forms(void)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	const char *const unsupported[] = {"1\0331000$D(1)$",
	    "1\0330000literal[3]","1\0330000$UNKNOWN_NAME$"};
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		CHECK(process(&f,unsupported[i],ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(st_rip3_input_wait_ns(f.host.input) == 0 && f.replies == 0 && f.host_errors == 0);
	}
	CHECK(process(&f,"1\0330?00$D(1)$",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1\0330",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1\033000.$RESX$",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1\0330A00$RESX$",-1) == 0 && f.reply_length == 3 && memcmp(f.reply,"640",3) == 0);
	CHECK(process(&f,"1\033...$RESX$",-1) == 0 && f.reply_length == 6 && memcmp(f.reply,"640640",6) == 0);
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"1\03300#0$RESX$",-1) == 0 && f.reply_length == 9 && memcmp(f.reply,"640640640",9) == 0);
	CHECK(process(&f,"J10",-1) == 0);
	CHECK(process(&f,"1\03300#0$RESX$",ST_RIP3_COMMAND_INVALID) == 0 && f.reply_length == 9);
	struct st_rip3_query *bound = f.host.query;
	f.host.query = NULL;
	CHECK(process(&f,"1\0330000$D(1)$",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.query = bound;
	f.query_sink.reply = NULL;
	CHECK(process(&f,"1\0330000$D(1)$",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.query_sink.reply = reply;
	CHECK(process(&f,"1\0330000$D(0)$",-1) == 0 && f.host_errors == 1 && f.host_kind == ST_RIP3_HOST_INVALID_REFERENCE);
	CHECK(process(&f,"1\0330000$APP$",-1) == 0 && f.host_errors == 2 && f.host_kind == ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(process(&f,"1\0330000$GOTOURL$",-1) == 0 && f.host_errors == 3 && f.host_kind == ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 0);
	/* Results below the response limit are retained in full. */
	char name[4096]; memset(name,'x',sizeof(name)-1); name[sizeof(name)-1] = 0;
	const struct st_rip3_terminal_identity identity = {name,"Vendor","Version"};
	f.variables.identity = &identity; f.reply_length = 0; f.replies = 0;
	CHECK(process(&f,"1\0330000$TERMINFO(NAME)$",-1) == 0 && f.reply_length == sizeof(name)-1 && f.replies == 1);
	CHECK(memcmp(f.reply,name,sizeof(name)-1) == 0);
	destroy(&f);
	return 0;
}

static int
recovery(void)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(append(&f,"!|1\0330000$D(0)$|1\0330000$D(1)$tail$UNKNOWN$|1\0330000$TERMINFO(NAME)$|J1S|1\0330000$D(1)$|1\0330000$RESX$\r") == 0);
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.errors == 0);
	CHECK(f.host_errors == 1 && f.host_kind == ST_RIP3_HOST_INVALID_REFERENCE);
	CHECK(f.replies == 0 && f.reply_length == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667 && f.read < f.length);
	f.now = 16666667; st_rip3_input_process(f.host.input,2048);
	CHECK(f.errors == 0 && f.host_errors == 2 && f.host_kind == ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(f.replies == 1 && f.reply_length == 7 && memcmp(f.reply,"Fixture",7) == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667 && f.read < f.length);
	f.now += 16666667; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == f.length && f.replies == 2 && f.reply_length == 10 && memcmp(f.reply,"Fixture640",10) == 0);
	destroy(&f);
	return 0;
}

static int
compound(enum st_rip3_canvas canvas, const char *width)
{
	struct fixture f = {0};
	CHECK(create(&f,canvas) == 0);
	CHECK(append(&f,"!|1\0330000a$D(1)$b$RESX$$D(2)$c^@^m``^^`G^[[A^[[K|J1S\rraw\r\n") == 0);
	st_rip3_input_process(f.host.input,2048);
	const size_t stopped = f.read;
	CHECK(f.reply_length == 0 && st_rip3_query_pending(f.host.query));
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667);
	f.now = 16666666; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == stopped && f.reply_length == 0);
	f.now++; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == stopped && f.reply_length == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 33333334);
	f.now += 33333333; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == stopped && st_rip3_query_pending(f.host.query));
	f.now++; st_rip3_input_process(f.host.input,2048);
	const uint8_t tail[] = {'c','^','@',13,'`','^',7,27,'[','A',27,'[','K'};
	CHECK(f.read == f.length && !st_rip3_query_pending(f.host.query));
	CHECK(f.reply_length == 2+strlen(width)+sizeof(tail));
	CHECK(memcmp(f.reply+2+strlen(width),tail,sizeof(tail)) == 0);
	CHECK(f.text_length == 5 && memcmp(f.text,"raw\r\n",5) == 0);
	CHECK(f.errors == 0 && f.host_errors == 2 && f.host_kind==ST_RIP3_HOST_INVALID_REFERENCE && !f.overflow);
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 64);
	destroy(&f);
	return 0;
}

static int
owned_and_recovery(void)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	char query[] = "1\0330000$D(1)$owned \xb3 \xe2\x98\x83$D(0)$$APP$$NULL$end$D(1)$";
	CHECK(process(&f,query,-1) == 0 && st_rip3_query_pending(f.host.query));
	memset(query,'?',sizeof(query)-1); /* The caller's body can be reused immediately. */
	CHECK(process(&f,"1\0330000replace",ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(f.reply_length == 0 && st_rip3_input_wait_ns(f.host.input) == 16666667);
	CHECK(append(&f,"later\r\n") == 0);
	f.now = 16666667; st_rip3_input_process(f.host.input,2048);
	const char expected[] = "owned \xb3 \xe2\x98\x83" "end";
	CHECK(f.reply_length==0 && f.host_errors==2 && f.host_kind==ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(st_rip3_query_pending(f.host.query) && f.read==0);
	CHECK(st_rip3_input_wait_ns(f.host.input)==16666667); /* Final delay precedes transmission too. */
	f.now+=16666667;st_rip3_input_process(f.host.input,2048);
	CHECK(f.read==f.length && f.text_length==7 && !st_rip3_query_pending(f.host.query));
	CHECK(f.reply_length==sizeof(expected)-1 && memcmp(f.reply,expected,sizeof(expected)-1)==0);
	CHECK(process(&f,"1\0330000$D(1)$cancelled",-1) == 0);
	st_rip3_query_cancel(f.host.query);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667);
	st_rip3_input_reset(f.host.input);
	f.reply_length = 0;
	CHECK(process(&f,"1\0330000after",-1) == 0 && f.reply_length == 5 && memcmp(f.reply,"after",5) == 0);
	/* Earlier structural stages are still checked before any ordinary effect. */
	unsigned errors=f.host_errors;
	CHECK(process(&f,"1\0330000no$D(1)$<<macro>>",-1)==0 && f.host_errors==errors+1
	    && f.host_kind==ST_RIP3_HOST_OPERATION_FAILED && st_rip3_input_wait_ns(f.host.input)==0 && f.reply_length==5);
	CHECK(process(&f,"1\0330000no$D(1)$[0]",ST_RIP3_COMMAND_UNSUPPORTED)==0);
	CHECK(f.reply_length==5 && st_rip3_input_wait_ns(f.host.input)==0 && !st_rip3_query_pending(f.host.query));
	CHECK(process(&f,"1\0330000",-1) == 0); /* Empty immediate query. */
	CHECK(process(&f,"1\0330000$D(1)$destroyed",-1) == 0);
	destroy(&f); /* Pending owned text is released without a callback. */
	return 0;
}

static int
active_order(void)
{
	struct fixture f = {0};
	struct st_rip3_table_host tables = {0};
	struct st_rip3_environment value;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	tables.tables[ST_RIP3_TABLE_ENV].live = f.host.environments;
	f.variables.tables = &tables;
	CHECK(append(&f,"!|1\0330000$BASEMATH(CUR,64)$$D(1)$$BASEMATH$$BASEMATH(CUR,36)$$BASEMATH$|J1S\r") == 0);
	st_rip3_input_process(f.host.input,2048);
	CHECK(st_rip3_environments_get(f.host.environments,0,&value) == 0 && value.base_math == 64);
	CHECK(f.reply_length == 0 && st_rip3_input_wait_ns(f.host.input) == 16666667);
	f.now = 16666667; st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length == 4 && memcmp(f.reply,"6436",4) == 0 && f.read == f.length);
	CHECK(st_rip3_environments_get(f.host.environments,0,&value) == 0 && value.base_math == 64);
	CHECK(f.errors == 0 && f.host_errors == 0);
	destroy(&f);
	return 0;
}

static int
controls(void)
{
	struct fixture f = {0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	/* Explicit ASCII control values, independent of the evaluator's scanner. */
	const struct { const char *text; uint8_t byte; } codes[] = {
		{"^A",1},{"^B",2},{"^C",3},{"^D",4},{"^E",5},{"^F",6},{"^G",7},
		{"^H",8},{"^I",9},{"^J",10},{"^K",11},{"^L",12},{"^M",13},{"^N",14},{"^O",15},
		{"^P",16},{"^Q",17},{"^R",18},{"^S",19},{"^T",20},{"^U",21},{"^V",22},{"^W",23},
		{"^X",24},{"^Y",25},{"^Z",26},{"^[",27},{"^\\",28},{"^]",29},{"`^",30},{"^_",31},
		{"^^",'^'},{"``",'`'},
	};
	for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
		char body[10];
		(void)snprintf(body,sizeof(body),"1\0330000%s",codes[i].text);
		f.reply_length = 0;
		CHECK(process(&f,body,-1) == 0 && f.reply_length == 1 && f.reply[0] == codes[i].byte);
	}
	for (unsigned i = 0; i < 26; i++) {
		char body[] = "1\0330000`a";
		body[7] = (char)('a'+i); f.reply_length = 0;
		CHECK(process(&f,body,-1) == 0 && f.reply_length == 1 && f.reply[0] == i+1);
	}
	const struct {const char *source,*expected;} preserved[]={
		{"^@","^@"},{"^?","^?"},{"`?","`?"},{"^0","^0"},{"^~","^~"},
		{"^","^"},{"`","`"},{"^^M","^M"},{"``M","`M"},
	};
	for (size_t i=0; i<sizeof(preserved)/sizeof(preserved[0]); i++) {
		char body[40];(void)snprintf(body,sizeof(body),"1\0330000%s",preserved[i].source);f.reply_length=0;
		CHECK(process(&f,body,-1)==0 && f.reply_length==strlen(preserved[i].expected)
		    && memcmp(f.reply,preserved[i].expected,f.reply_length)==0);
	}
	const char *const keys[] = {"^[[A","^[[B","^[[C","^[[D","^[[H","^[[K","^[[L","^[[Z"};
	const uint8_t expected[][3] = {{27,'[','A'},{27,'[','B'},{27,'[','C'},{27,'[','D'},
	    {27,'[','H'},{27,'[','K'},{27,'[','L'},{27,'[','Z'}};
	for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		char body[12];
		(void)snprintf(body,sizeof(body),"1\0330000%s",keys[i]); f.reply_length = 0;
		CHECK(process(&f,body,-1) == 0 && f.reply_length == 3 && memcmp(f.reply,expected[i],3) == 0);
		body[6] = '`'; f.reply_length = 0;
		CHECK(process(&f,body,-1) == 0 && f.reply_length == 3 && memcmp(f.reply,expected[i],3) == 0);
	}
	destroy(&f);
	return 0;
}

static void
user_diagnostic(void *context, enum st_rip3_user_variable_diagnostic kind)
{
	struct fixture *f=context;
	f->user_errors++;f->user_kind=kind;
}

static int
bind_users(struct fixture *f)
{
	CHECK(st_rip3_variables_create(&f->session)==0 && st_rip3_variables_create(&f->persistent)==0);
	struct st_rip3_user_variable_host host={.session=f->session,.persistent=f->persistent,
	    .context=f,.diagnostic=user_diagnostic};
	CHECK(st_rip3_user_variables_create(&f->variables.user_variables,&host)==0);
	return 0;
}

static int
user_value(struct st_rip3_variables *store, const char *name, const char *expected)
{
	struct st_rip3_variable_value value;
	int status=st_rip3_variables_get(store,name,strlen(name),&value);
	if (expected==NULL) CHECK(status==IO_ERROR_NOT_FOUND);
	else CHECK(status==0 && value.length==strlen(expected) && memcmp(value.bytes,expected,value.length)==0);
	return 0;
}

static int
reset_text_variables(void)
{
	struct fixture f={0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0 && bind_users(&f)==0);
	CHECK(st_rip3_variables_define(f.persistent,NULL,"KEEP",4,(const uint8_t *)"saved",5,true)==0);
	CHECK(st_rip3_variables_set(f.session,"KEEP",4,(const uint8_t *)"live",4)==0);
	CHECK(process(&f,"1\0330000$RESET(TV)$x$RESET(TV,)$y$RESET(TV,A B)$z"
	    "$RESET(TV,X1234567890123456789)$$RESET(TV,KEEP,OTHER)$$&KEEP$",-1)==0);
	CHECK(f.host_errors==5 && f.host_kind==ST_RIP3_HOST_INVALID_REFERENCE
	    && f.reply_length==7 && memcmp(f.reply,"xyzlive",7)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000a$RESET(tv,,keep,)$$&KEEP$b$RESET(TV,Missing)$",-1)==0);
	CHECK(f.host_errors==5 && f.reply_length==7 && memcmp(f.reply,"asavedb",7)==0
	    && user_value(f.session,"KEEP",NULL)==0 && user_value(f.persistent,"KEEP","saved")==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000<<RESET(TV,KEEP)>>$&KEEP=fallback$",-1)==0);
	CHECK(st_rip3_query_prompt_pending(f.host.query) && f.reply_length==0
	    && user_value(f.persistent,"KEEP","saved")==0);
	st_rip3_query_cancel(f.host.query);
	CHECK(user_value(f.persistent,"KEEP","saved")==0);
	CHECK(process(&f,"1\0330000<<RESET(TV,KEEP)>>$&KEEP=fallback$",-1)==0);
	CHECK(st_rip3_query_prompt_pending(f.host.query) && f.reply_length==0);
	st_rip3_user_variables_respond(f.variables.user_variables,NULL,0,false);
	st_rip3_query_process(f.host.query,64);
	CHECK(!st_rip3_query_pending(f.host.query) && user_value(f.persistent,"KEEP",NULL)==0
	    && f.reply_length==8 && memcmp(f.reply,"fallback",8)==0);
	destroy(&f);
	return 0;
}

static int
user_queries(enum st_rip3_canvas canvas, const char *width)
{
	struct fixture f={0};
	CHECK(create(&f,canvas)==0 && bind_users(&f)==0);
	CHECK(process(&f,"1\0330000$-=NAME=Ada$<$&name$>",-1)==0);
	CHECK(f.reply_length==5 && memcmp(f.reply,"<Ada>",5)==0 && user_value(f.session,"NAME","Ada")==0
	    && !st_rip3_query_pending(f.host.query) && st_rip3_user_variables_state(f.variables.user_variables)==ST_RIP3_USER_VARIABLE_IDLE);
	f.reply_length=0;
	/* Earlier providers run normally, but the response waits for selection. */
	CHECK(process(&f,"1\0330000pre$-=OTHER=changed$((picklist))",-1)==0);
	CHECK(user_value(f.session,"OTHER","changed")==0 && f.reply_length==0);
	CHECK(st_rip3_query_choose(f.host.query,0));
	st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==11 && memcmp(f.reply,"prepicklist",11)==0);
	f.reply_length=0;
	/* Known invalid/unsupported built-ins never prompt, including excluded
	 * external-launch names. P has no built-in registration and does prompt. */
	CHECK(st_rip3_variables_set(f.session,"D",1,(const uint8_t *)"fake",4)==0);
	CHECK(process(&f,"1\0330000$D(0)$$APP$$GOTOURL$end",-1)==0);
	CHECK(f.host_errors==3 && f.reply_length==3 && memcmp(f.reply,"end",3)==0
	    && st_rip3_user_variables_prompt(f.variables.user_variables)==NULL);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000$P$",-1)==0 && st_rip3_query_pending(f.host.query));
	CHECK(strcmp(st_rip3_user_variables_prompt(f.variables.user_variables)->name,"P")==0);
	st_rip3_user_variables_respond(f.variables.user_variables,NULL,0,true);
	st_rip3_query_process(f.host.query,1);
	CHECK(st_rip3_query_pending(f.host.query) && f.reply_length==0); /* First-pass result only. */
	st_rip3_query_process(f.host.query,1);
	CHECK(!st_rip3_query_pending(f.host.query) && f.reply_length==4 && memcmp(f.reply,"NONE",4)==0);
	f.reply_length=0;
	/* The final token itself may suspend. Accepted replacement bytes are
	 * skipped by this ordinary pass, even if they resemble controls/callbacks. */
	char body[]="1\0330000$=NAME@Please enter=default$";
	CHECK(process(&f,body,-1)==0 && st_rip3_query_pending(f.host.query));
	memset(body,'?',sizeof(body)-1);
	CHECK(st_rip3_user_variables_initial(f.variables.user_variables)->length==3);
	CHECK(process(&f,"1\0330000other",ST_RIP3_COMMAND_OPERATION_FAILED)==0);
	const uint8_t literal[]="^m$D(1)$";
	st_rip3_user_variables_respond(f.variables.user_variables,literal,sizeof(literal)-1,false);
	st_rip3_query_process(f.host.query,0);
	CHECK(f.reply_length==0 && st_rip3_query_pending(f.host.query));
	st_rip3_query_process(f.host.query,1);
	CHECK(st_rip3_query_pending(f.host.query) && f.reply_length==0);
	st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==0 && st_rip3_input_wait_ns(f.host.input)==16666667);
	f.now+=16666667;st_rip3_query_process(f.host.query,64);
	CHECK(!st_rip3_query_pending(f.host.query) && f.reply_length==1 && f.reply[0]==13);
	f.reply_length=0;
	/* Delays, prompts and subsequent source bytes share one ordered pump. */
	CHECK(append(&f,"!|1\0330000a$D(1)$$*=&NEW$z$D(1)$|J1S|1\0330000$RESX$\rraw\r\n")==0);
	st_rip3_input_process(f.host.input,2048);
	size_t stopped=f.read;unsigned reads=f.reads;
	CHECK(f.reply_length==0 && st_rip3_query_pending(f.host.query));
	CHECK(st_rip3_user_variables_prompt(f.variables.user_variables)==NULL);
	f.now+=16666667;st_rip3_input_process(f.host.input,2048);
	CHECK(f.read==stopped && f.reads==reads && strcmp(st_rip3_user_variables_prompt(f.variables.user_variables)->name,"NEW")==0);
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.reads==reads && f.reply_length==0);
	st_rip3_user_variables_respond(f.variables.user_variables,NULL,0,true);
	CHECK(f.user_errors==1 && f.user_kind==ST_RIP3_USER_VARIABLE_CANCELLATION_DISABLED);
	st_rip3_input_process(f.host.input,2048);CHECK(f.reads==reads);
	st_rip3_user_variables_respond(f.variables.user_variables,NULL,0,false);
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.read==stopped && f.reads==reads && f.reply_length==0
	    && user_value(f.session,"NEW","")==0 && st_rip3_input_wait_ns(f.host.input)==16666667);
	f.now+=16666667;st_rip3_input_process(f.host.input,2048);
	CHECK(f.read==f.length && f.reply_length==6+strlen(width) && memcmp(f.reply,"aNULLz",6)==0 && memcmp(f.reply+6,width,strlen(width))==0
	    && f.text_length==5 && memcmp(f.text,"raw\r\n",5)==0 && !st_rip3_query_pending(f.host.query));
	struct st_rip3_environment environment;
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment)==0 && environment.base_math==64);
	destroy(&f);return 0;
}

static int
user_recovery(void)
{
	struct fixture f={0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0 && bind_users(&f)==0);
	/* Formatting without its required adapter fails nonfatally, and following
	 * user/built-in/literal tokens still execute. No prompt or write occurs. */
	CHECK(process(&f,"1\0330000$=NAME(ALPHA)$$-=NAME=ok$$&NAME$$NULL$tail",-1)==0);
	CHECK(f.user_errors==1 && f.user_kind==ST_RIP3_USER_VARIABLE_OPERATION_FAILED && f.reply_length==6
	    && memcmp(f.reply,"oktail",6)==0 && user_value(f.session,"NAME","ok")==0);
	f.reply_length=0;
	/* Recognized built-in names with user-only syntax cannot become prompts. */
	CHECK(process(&f,"1\0330000before$DATE,20@question$",ST_RIP3_COMMAND_UNSUPPORTED)==0 && f.reply_length==0);
	CHECK(process(&f,"1\0330000$=P$discarded",-1)==0 && st_rip3_query_pending(f.host.query));
	st_rip3_query_cancel(f.host.query);
	CHECK(!st_rip3_query_pending(f.host.query) && st_rip3_user_variables_state(f.variables.user_variables)==ST_RIP3_USER_VARIABLE_IDLE
	    && user_value(f.session,"P",NULL)==0);
	/* Cancelling a query which has not used the engine cannot cancel another
	 * owner's pending prompt. Busy user preflight causes no earlier output. */
	struct st_rip3_variable_reference reference;
	CHECK(st_rip3_variable_reference_parse(&reference,(const uint8_t *)"$P$",3)==0);
	st_rip3_user_variables_process(f.variables.user_variables,&reference);
	CHECK(process(&f,"1\0330000before$&NAME$",ST_RIP3_COMMAND_OPERATION_FAILED)==0 && f.reply_length==0);
	st_rip3_query_cancel(f.host.query);
	CHECK(st_rip3_user_variables_state(f.variables.user_variables)==ST_RIP3_USER_VARIABLE_PROMPT);
	st_rip3_user_variables_abort(f.variables.user_variables);
	/* Transparent cancellation emits nothing; normal cancellation emits NONE. */
	CHECK(process(&f,"1\0330000$=&MISSING$tail",-1)==0);
	st_rip3_user_variables_respond(f.variables.user_variables,NULL,0,true);
	st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==4 && memcmp(f.reply,"tail",4)==0 && user_value(f.session,"MISSING",NULL)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000$=MISSING$tail",-1)==0);
	st_rip3_user_variables_respond(f.variables.user_variables,NULL,0,true);
	st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==8 && memcmp(f.reply,"NONEtail",8)==0);
	/* Abort/EOF of a suspended last token still releases owned prompt data. */
	CHECK(process(&f,"1\0330000$*+=FINAL$",-1)==0 && st_rip3_query_pending(f.host.query));
	st_rip3_query_cancel(f.host.query);
	CHECK(st_rip3_user_variables_state(f.variables.user_variables)==ST_RIP3_USER_VARIABLE_IDLE && user_value(f.persistent,"FINAL",NULL)==0);
	CHECK(process(&f,"1\0330000$*+=FINAL$",-1)==0);
	destroy(&f);
	return 0;
}

static int
two_passes(void)
{
	struct fixture f={0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0 && bind_users(&f)==0);
	CHECK(st_rip3_variables_set(f.session,"A",1,(const uint8_t *)"$&B$",4)==0);
	const char *indirect="$TERMINFO(NAME)$";
	CHECK(st_rip3_variables_set(f.session,"B",1,(const uint8_t *)indirect,strlen(indirect))==0);
	CHECK(process(&f,"1\0330000$&A$",-1)==0 && f.reply_length==strlen(indirect)
	    && memcmp(f.reply,indirect,f.reply_length)==0 && f.host_errors==0);
	const struct {const char *stored,*expected;} values[]={
		{"$TERMINFO(NAME)$","Fixture"},{"<<N>>","<<N>>"},{"[3]","[3]"},
		{"\\$&MISSING\\$","$&MISSING$"},
	};
	for (size_t i=0; i<sizeof(values)/sizeof(values[0]); i++) {
		CHECK(st_rip3_variables_set(f.session,"A",1,(const uint8_t *)values[i].stored,strlen(values[i].stored))==0);
		f.reply_length=0;
		CHECK(process(&f,"1\0330000$&A$",-1)==0 && f.reply_length==strlen(values[i].expected)
		    && memcmp(f.reply,values[i].expected,f.reply_length)==0 && f.host_errors==0 && f.user_errors==0);
	}
	const struct {const char *source,*expected;} forms[]={
		{"\\$TERMINFO(NAME)$","Fixture"},
		{"\\$TERMINFO(NAME)\\$","$TERMINFO(NAME)$"},
		{"$TERMINFO(NA\\ME)$","Fixture"},
		{"$TERMINFO(A\\$B)$","NONE"},
		{"^$RESX$","^640"},{"[","["},{"[?]","[?]"},{"\\[?]","[?]"},
	};
	for (size_t i=0; i<sizeof(forms)/sizeof(forms[0]); i++) {
		char source[100];(void)snprintf(source,sizeof(source),"1\0330000%s",forms[i].source);f.reply_length=0;
		CHECK(process(&f,source,-1)==0 && f.reply_length==strlen(forms[i].expected)
		    && memcmp(f.reply,forms[i].expected,f.reply_length)==0 && f.host_errors==0);
	}
	/* A callback introduced by pass one can prompt in pass two. Its accepted
	 * response is transmitted literally, with no third ordinary pass. */
	CHECK(st_rip3_variables_set(f.session,"A",1,(const uint8_t *)"$=NEW$",6)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000$&A$",-1)==0 && st_rip3_query_pending(f.host.query) && f.reply_length==0
	    && strcmp(st_rip3_user_variables_prompt(f.variables.user_variables)->name,"NEW")==0);
	st_rip3_user_variables_respond(f.variables.user_variables,(const uint8_t *)"$RESX$",6,false);
	st_rip3_query_process(f.host.query,64);
	CHECK(!st_rip3_query_pending(f.host.query) && f.reply_length==6 && memcmp(f.reply,"$RESX$",6)==0
	    && user_value(f.session,"NEW","$RESX$")==0);
	f.reply_length=0;unsigned errors=f.host_errors;
	CHECK(process(&f,"1\0330000[3",-1)==0 && f.reply_length==2 && memcmp(f.reply,"[3",2)==0
	    && f.host_errors==errors+1 && f.host_kind==ST_RIP3_HOST_INVALID_REFERENCE);
	destroy(&f);return 0;
}

extern bool st_rip3_templates_test_fail;

static void
selection_required(void *context, unsigned group)
{
	struct fixture *f=context;
	f->required_calls++;f->required_group=group;
}

static int
template_value(struct fixture *f, unsigned number, const char *expected)
{
	struct st_rip3_template_bytes value;
	CHECK(st_rip3_templates_resolve(f->templates.slots,number,&value)==0 && value.length==strlen(expected)
	    && memcmp(value.bytes,expected,value.length)==0);return 0;
}

static int
template_queries(enum st_rip3_canvas canvas)
{
	struct fixture f={0};
	CHECK(create(&f,canvas)==0 && bind_users(&f)==0 && st_rip3_templates_create(&f.templates.slots)==0);
	f.templates.context=&f;f.templates.selection_required=selection_required;f.variables.templates=&f.templates;
	/* Compose the complete chain before callbacks. The unused feed and the
	 * first template's D are discarded by the marker-free final template. */
	CHECK(process(&f,"1\0330000[3:]$D(1)$[4:]FOUR[34]$-=FLAG=bad$",-1)==0);
	CHECK(f.reply_length==4 && memcmp(f.reply,"FOUR",4)==0 && st_rip3_input_wait_ns(f.host.input)==0
	    && user_value(f.session,"FLAG",NULL)==0 && template_value(&f,3,"$D(1)$")==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000[1:]red green $?$ blue[E:]LOUD QUIET $?$ YELL[5:]soft $?$ hard smooth gritty[1E5](host command)",-1)==0);
	const char *composed="soft LOUD QUIET red green (host command) blue YELL hard smooth gritty";
	CHECK(f.reply_length==strlen(composed) && memcmp(f.reply,composed,f.reply_length)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000[4:]print(\"$?$\")^m[44]HELLO",-1)==0);
	const char *nested="print(\"print(\"HELLO\")\r\")\r";
	CHECK(f.reply_length==strlen(nested) && memcmp(f.reply,nested,f.reply_length)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000prefix[]next[3:]first[3:]last[3]ignored[]tail",-1)==0);
	CHECK(f.reply_length==18 && memcmp(f.reply,"prefixnextlasttail",18)==0 && template_value(&f,3,"last")==0);
	/* Segmentation sees complete directives even after ESC or backslash. */
	CHECK(process(&f,"1\0330000[a:]TEN[3:]THREE",-1)==0);
	const struct {const char *source,*expected;} forms[]={
		{"[a]","TEN"},{"^[[A]","\033TEN"},{"\033[A]","\033TEN"},{"^[x[3]","\033xTHREE"},{"\\[3]","\\THREE"},
	};
	for (size_t i=0; i<sizeof(forms)/sizeof(forms[0]); i++) {
		char source[100];(void)snprintf(source,sizeof(source),"1\0330000%s",forms[i].source);f.reply_length=0;
		CHECK(process(&f,source,-1)==0 && f.reply_length==strlen(forms[i].expected)
		    && memcmp(f.reply,forms[i].expected,f.reply_length)==0);
	}
	f.reply_length=0;
	CHECK(process(&f,"1\0330000[0]discarded",-1)==0 && f.reply_length==0 && !st_rip3_query_pending(f.host.query));
	CHECK(required_group(f.templates.slots,2,true)==0);
	unsigned errors=f.host_errors;
	CHECK(process(&f,"1\0330000[2]feed[]unreached",-1)==0 && f.reply_length==0 && f.required_calls==1
	    && f.required_group==2 && f.host_errors==errors+1 && f.host_kind==ST_RIP3_HOST_OPERATION_FAILED
	    && !st_rip3_query_pending(f.host.query));
	CHECK(process(&f,"1\0330000[2:]selected[2]feed",-1)==0 && f.reply_length==0);
	uint64_t redraw[128];size_t changed;
	CHECK(st_rip3_templates_button_activate(f.templates.slots,1002,redraw,&changed)==0);
	CHECK(process(&f,"1\0330000[2]feed",-1)==0 && f.reply_length==8 && memcmp(f.reply,"selected",8)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000[Z:]\xb3 \xe2\x98\x83[Z]",-1)==0 && f.reply_length==5
	    && memcmp(f.reply,"\xb3 \xe2\x98\x83",5)==0);
	/* Definition storage is capped before composition, so a callback beyond
	 * its stored boundary cannot reach either ordinary pass. */
	uint8_t large[4102];memset(large,'X',4096);memcpy(large+4096,"$D(1)$",6);
	CHECK(st_rip3_templates_set(f.templates.slots,35,large,sizeof(large))==0);f.reply_length=0;
	CHECK(process(&f,"1\0330000[Z]feed",-1)==0 && f.reply_length==4096 && st_rip3_input_wait_ns(f.host.input)==0);
	for (size_t i=0; i<4096; i++) CHECK(f.reply[i]=='X');
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"$?$",3)==0);
	char levels[60]="1\0330000[";memset(levels+7,'3',36);memcpy(levels+43,"]KEEP",6);f.reply_length=0;
	CHECK(process(&f,levels,-1)==0 && f.reply_length==4 && memcmp(f.reply,"KEEP",4)==0);
	memset(levels+7,'3',37);memcpy(levels+44,"]KEEP",6);f.reply_length=0;
	CHECK(process(&f,levels,ST_RIP3_COMMAND_INVALID)==0 && f.reply_length==0 && template_value(&f,3,"$?$")==0);
	/* Suspended source owns the later definition as well as its current reply. */
	char owned[]="1\0330000[3:]before[]$D(1)$[3:]after[3]ignored";
	CHECK(process(&f,owned,-1)==0 && st_rip3_query_pending(f.host.query) && template_value(&f,3,"before")==0);
	memset(owned,'?',sizeof(owned)-1);
	CHECK(append(&f,"post\r\n")==0);unsigned reads=f.reads;
	st_rip3_input_process(f.host.input,2048);CHECK(f.reads==reads && f.reply_length==0);
	f.now+=16666667;st_rip3_input_process(f.host.input,2048);
	CHECK(!st_rip3_query_pending(f.host.query) && f.reply_length==5 && memcmp(f.reply,"after",5)==0
	    && template_value(&f,3,"after")==0 && f.text_length==6 && memcmp(f.text,"post\r\n",6)==0);
	f.reply_length=0;errors=f.host_errors;st_rip3_templates_test_fail=true;
	CHECK(process(&f,"1\0330000[3:]lost[3]unused",-1)==0 && f.host_errors==errors+1
	    && f.host_kind==ST_RIP3_HOST_RESOURCE_FAILURE && f.reply_length==0 && template_value(&f,3,"after")==0);
	st_rip3_templates_test_fail=false;
	CHECK(process(&f,"1\0330000[3]X",-1)==0 && f.reply_length==5 && memcmp(f.reply,"after",5)==0);
	destroy(&f);return 0;
}

static int
expect_x(struct fixture *f, size_t length, int first, int last)
{
	CHECK(f->reply_length==length);
	for (size_t i=0; i<length; i++) {
		int expected=i==0 && first>=0 ? first : i+1==length && last>=0 ? last : 'X';
		CHECK(f->reply[i]==expected);
	}
	return 0;
}

static int
capped_query(struct fixture *f, const uint8_t *bytes, size_t length)
{
	f->reply_length=0;
	/* Exercise the existing void command parser with explicit byte spans. */
	uint8_t command_bytes[8300];CHECK(length<=sizeof(command_bytes)-6);
	memcpy(command_bytes,"1\0330000",6);memcpy(command_bytes+6,bytes,length);
	unsigned errors=f->errors;
	command(f,command_bytes,length+6);
	CHECK(f->errors==errors && !f->overflow);
	while (st_rip3_query_pending(f->host.query) && st_rip3_input_wait_ns(f->host.input)==0
	    && st_rip3_user_variables_prompt(f->variables.user_variables)==NULL)
		st_rip3_query_process(f->host.query,64);
	return 0;
}

static int
embedding_limits(enum st_rip3_canvas canvas)
{
	struct fixture f={0};
	CHECK(create(&f,canvas)==0 && bind_users(&f)==0 && st_rip3_templates_create(&f.templates.slots)==0);
	f.templates=(struct st_rip3_template_host){f.templates.slots,&f,selection_required};f.variables.templates=&f.templates;
	for (unsigned slot=0; slot<36; slot++) {
		uint8_t name=(uint8_t)(slot<10 ? '0'+slot : 'A'+slot-10);
		uint8_t value[]={'V',name},reference[]={'$','?',name,'$'};
		CHECK(st_rip3_templates_set(f.templates.slots,slot,value,sizeof(value))==0);
		CHECK(capped_query(&f,reference,sizeof(reference))==0 && f.reply_length==sizeof(value) && memcmp(f.reply,value,sizeof(value))==0);
		if (slot>=10) {
			reference[2]=(uint8_t)(name+'a'-'A');
			CHECK(capped_query(&f,reference,sizeof(reference))==0 && f.reply_length==sizeof(value) && memcmp(f.reply,value,sizeof(value))==0);
		}
		CHECK(st_rip3_templates_set(f.templates.slots,slot,NULL,0)==0);
	}
	uint8_t bytes[8300];memset(bytes,'X',sizeof(bytes));
	for (size_t length=4095; length<=4097; length++) {
		CHECK(capped_query(&f,bytes,length)==0 && expect_x(&f,length>4096 ? 4096 : length,-1,-1)==0);
		CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,length)==0);
		struct st_rip3_template_bytes stored;
		CHECK(st_rip3_templates_resolve(f.templates.slots,3,&stored)==0 && stored.length==(length>4096 ? 4096 : length));
	}
	memcpy(bytes,"[3:]",4);memset(bytes+4,'X',4097);memcpy(bytes+4101,"[]$?3$",6);
	CHECK(capped_query(&f,bytes,4107)==0 && expect_x(&f,4096,-1,-1)==0);
	memset(bytes,'X',sizeof(bytes));
	CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4095)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$Y",5)==0 && expect_x(&f,4096,-1,'Y')==0);
	CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4096)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$Y",5)==0 && expect_x(&f,4096,-1,-1)==0);
	CHECK(st_rip3_variables_set(f.session,"FLAG",4,(const uint8_t *)"old",3)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$$D(1)$$-=FLAG=changed$",strlen("$?3$$D(1)$$-=FLAG=changed$"))==0
	    && expect_x(&f,4096,-1,-1)==0 && st_rip3_input_wait_ns(f.host.input)==0 && user_value(f.session,"FLAG","old")==0);
	/* Retained suffix actions still execute. Growing insertion drops exactly
	 * one final X before D later frees its own six source bytes. */
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"ABCDE",5)==0);
	memcpy(bytes,"$?3$$D(1)$",10);memset(bytes+10,'X',4086);
	CHECK(capped_query(&f,bytes,4096)==0 && f.reply_length==0 && st_rip3_input_wait_ns(f.host.input)==16666667);
	f.now+=16666667;st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==4090 && memcmp(f.reply,"ABCDE",5)==0);
	for (size_t i=5; i<f.reply_length; i++) CHECK(f.reply[i]=='X');
	/* Second-pass insertion, and permanent loss after later shrinking. */
	memset(bytes,'X',4096);CHECK(st_rip3_templates_set(f.templates.slots,4,bytes,4096)==0);
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"$?4$",4)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$Y",5)==0 && expect_x(&f,4096,-1,-1)==0);
	memset(bytes,'X',4092);memcpy(bytes+4092,"$?4$",4);
	CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4096)==0 && st_rip3_templates_set(f.templates.slots,4,(const uint8_t *)"Z",1)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$Y",5)==0 && expect_x(&f,4093,-1,'Z')==0);
	/* Raw notation is clipped before decoding; no early CR or backslash pair. */
	memset(bytes,'X',4094);memcpy(bytes+4094,"^m",2);
	CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4096)==0);
	CHECK(capped_query(&f,(const uint8_t *)"A$?3$",5)==0 && expect_x(&f,4096,'A','^')==0);
	memcpy(bytes+4094,"\\Z",2);CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4096)==0);
	CHECK(capped_query(&f,(const uint8_t *)"A$?3$",5)==0 && expect_x(&f,4096,'A','\\')==0);
	CHECK(capped_query(&f,(const uint8_t *)"\\",1)==0 && f.reply_length==1 && f.reply[0]=='\\');
	/* Earlier decoding can free capacity for a later replacement; it cannot
	 * restore the original tail. A pass-two inserted value is not decoded. */
	memset(bytes,'X',4094);memcpy(bytes+4094,"^m",2);CHECK(st_rip3_templates_set(f.templates.slots,4,bytes,4096)==0);
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"^m$?4$",6)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$",4)==0 && expect_x(&f,4096,13,'^')==0);
	/* Template insertion in pass one defers effects to pass two. Two nested
	 * embeddings are finite; the second inserted reference gets no third pass. */
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"$?4$",4)==0);
	CHECK(st_rip3_templates_set(f.templates.slots,4,(const uint8_t *)"$?3$",4)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$",4)==0 && f.reply_length==4 && memcmp(f.reply,"$?3$",4)==0);
	/* Blank groups insert empty, while a required missing slot requests selection
	 * and aborts the host program. Later blocks are never run on that failure. */
	CHECK(capped_query(&f,(const uint8_t *)"a$?0$b",6)==0 && f.reply_length==2 && memcmp(f.reply,"ab",2)==0);
	CHECK(required_group(f.templates.slots,0,true)==0);unsigned required=f.required_calls,errors=f.host_errors;
	CHECK(capped_query(&f,(const uint8_t *)"$?0$[]unreached",strlen("$?0$[]unreached"))==0 && f.reply_length==0 && f.required_calls==required+1
	    && f.required_group==0 && f.host_errors==errors+1 && !st_rip3_query_pending(f.host.query));
	CHECK(required_group(f.templates.slots,0,false)==0);
	/* A full replacement discards even unsupported or malformed ordinary
	 * references before they are validated, without requiring their service. */
	memset(bytes,'X',4096);CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4096)==0);
	struct st_rip3_user_variables *users=f.variables.user_variables;f.variables.user_variables=NULL;
	errors=f.host_errors;
	const char *const discarded[]={"$?3$$UNKNOWN$","$?3$$D(+1)$","$?3$$>FILE.RIP$","$?3$$D(1)"};
	for (size_t i=0; i<sizeof(discarded)/sizeof(discarded[0]); i++) {
		CHECK(capped_query(&f,(const uint8_t *)discarded[i],strlen(discarded[i]))==0
		    && expect_x(&f,4096,-1,-1)==0 && f.host_errors==errors && st_rip3_input_wait_ns(f.host.input)==0);
	}
	f.variables.user_variables=users;
	/* Entry clipping ignores discarded ordinary references, even when their
	 * service is absent, and does not borrow a clipped terminator. */
	memset(bytes,'X',4096);memcpy(bytes+4096,"$*MISSING$",10);
	CHECK(capped_query(&f,bytes,4106)==0 && expect_x(&f,4096,-1,-1)==0
	    && st_rip3_user_variables_prompt(f.variables.user_variables)==NULL);
	memset(bytes,'X',4094);memcpy(bytes+4094,"$D(1)$",6);
	uint8_t source[4110];memcpy(source,"1\0330000",6);memcpy(source+6,bytes,4100);
	errors=f.errors;f.reply_length=0;command(&f,source,4106);
	CHECK(f.errors==errors+1 && f.reply_length==0 && st_rip3_input_wait_ns(f.host.input)==0 && !st_rip3_query_pending(f.host.query));
	memset(bytes,'X',4093);CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4093)==0);errors=f.host_errors;
	CHECK(capped_query(&f,(const uint8_t *)"$?3$$D(1)$",10)==0 && f.host_errors==errors+1
	    && f.reply_length==0 && st_rip3_input_wait_ns(f.host.input)==0 && !st_rip3_query_pending(f.host.query));
	memset(bytes,'X',4095);CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4095)==0);errors=f.host_errors;
	CHECK(capped_query(&f,(const uint8_t *)"$?3$$D(1)$",10)==0 && f.host_errors==errors+1
	    && f.reply_length==0 && st_rip3_input_wait_ns(f.host.input)==0 && !st_rip3_query_pending(f.host.query));
	/* An incomplete inserted reference is rejected when the next pass scans
	 * it. The same byte inserted in the final pass has no later scan. */
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"$",1)==0);errors=f.host_errors;
	CHECK(capped_query(&f,(const uint8_t *)"$?3$",4)==0 && f.host_errors==errors+1 && f.reply_length==0);
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"$?4$",4)==0
	    && st_rip3_templates_set(f.templates.slots,4,(const uint8_t *)"$",1)==0);errors=f.host_errors;
	CHECK(capped_query(&f,(const uint8_t *)"$?3$",4)==0 && f.host_errors==errors && f.reply_length==1 && f.reply[0]=='$');
	CHECK(capped_query(&f,(const uint8_t *)"recovered",9)==0 && f.reply_length==9 && memcmp(f.reply,"recovered",9)==0);
	/* Limits are per block; the whole source and multiple responses are larger. */
	memset(bytes,'X',4096);memcpy(bytes+4096,"[][3:]value",11);
	CHECK(capped_query(&f,bytes,4107)==0 && expect_x(&f,4096,-1,-1)==0 && template_value(&f,3,"value")==0);
	memset(bytes,'X',4096);memcpy(bytes+4096,"[]",2);memset(bytes+4098,'Y',4096);
	CHECK(capped_query(&f,bytes,8194)==0 && f.reply_length==8192);
	for (size_t i=0; i<8192; i++) CHECK(f.reply[i]==(i<4096 ? 'X' : 'Y'));
	/* Raw composition may itself grow beyond the cap even when its stored
	 * template is short. Its D suffix is discarded before ordinary expansion. */
	CHECK(st_rip3_templates_set(f.templates.slots,3,(const uint8_t *)"Q$?$$D(1)$",10)==0);
	memcpy(bytes,"[3]",3);memset(bytes+3,'X',4096);
	CHECK(capped_query(&f,bytes,4099)==0 && expect_x(&f,4096,'Q',-1)==0 && st_rip3_input_wait_ns(f.host.input)==0);
	/* Generic dollar results share the exact same cap and discarded-suffix rule. */
	char identity_name[5001];memset(identity_name,'X',5000);identity_name[5000]=0;
	const struct st_rip3_terminal_identity identity={identity_name,"Tests","0"};f.variables.identity=&identity;
	CHECK(capped_query(&f,(const uint8_t *)"$TERMINFO(NAME)$$D(1)$$-=FLAG=bad$",strlen("$TERMINFO(NAME)$$D(1)$$-=FLAG=bad$"))==0
	    && expect_x(&f,4096,-1,-1)==0 && st_rip3_input_wait_ns(f.host.input)==0 && user_value(f.session,"FLAG","old")==0);
	/* Successful assignment which itself causes clipping remains committed. */
	memset(bytes,'X',4089);memcpy(bytes+4089,"$=NAME$",7);
	CHECK(capped_query(&f,bytes,4096)==0 && st_rip3_query_pending(f.host.query));
	st_rip3_user_variables_respond(f.variables.user_variables,(const uint8_t *)"Ada Lovelace",12,false);
	st_rip3_query_process(f.host.query,64);
	CHECK(user_value(f.session,"NAME","Ada Lovelace")==0 && f.reply_length==4096 && memcmp(f.reply+4089,"Ada Lov",7)==0);
	/* The cap counts raw UTF-8 bytes without repairing a split scalar. */
	memset(bytes,'X',4094);memcpy(bytes+4094,"\xe2\x98\x83",3);
	CHECK(st_rip3_templates_set(f.templates.slots,3,bytes,4097)==0);
	CHECK(capped_query(&f,(const uint8_t *)"$?3$",4)==0 && f.reply_length==4096
	    && f.reply[4094]==0xe2 && f.reply[4095]==0x98);
	destroy(&f);return 0;
}

extern unsigned st_rip3_query_test_allocation_countdown;

struct local_modifiers {bool shift,ctrl,mouse;unsigned calls;};
static int
local_modifiers(void *context, unsigned *down)
{
	struct local_modifiers *value=context;value->calls++;
	*down=(value->shift ? ST_RIP3_MODIFIER_SHIFT : 0) | (value->ctrl ? ST_RIP3_MODIFIER_CTRL : 0);
	return 0;
}

static int
local_mouse_present(void *context, bool *present)
{
	*present = ((const struct local_modifiers *)context)->mouse;
	return 0;
}

static int
local_queries(void)
{
	struct fixture f={0};CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0);
	struct local_modifiers keys={.shift=true,.mouse=true};unsigned colors=256;
	struct st_rip3_point position={0,0};
	const struct st_rip3_local_queries local={.context=&keys,.modifiers=local_modifiers,.mouse_position=&position,
	    .mouse_present=local_mouse_present,.colors=&colors};f.variables.local=&local;
	CHECK(st_rip3_frame_world(&f.frame,1280,960)==0);
	CHECK(process(&f,"1\0330000$SHIFT$/$D(1)$$SHIFT$/$CTRL$/$MSTAT$/$COLORS$/$XY$",-1)==0
	    && keys.calls==1 && f.reply_length==0 && st_rip3_input_wait_ns(f.host.input)==16666667);
	CHECK(append(&f,"raw\r\n")==0);st_rip3_input_process(f.host.input,2048);CHECK(f.read==0);
	keys.shift=false;keys.ctrl=true;keys.mouse=false;colors=16777216;position=(struct st_rip3_point){297,321};
	f.now=16666667;st_rip3_input_process(f.host.input,2048);
	const char *expected="1/0/1/NO/16777216/0594:0642";
	CHECK(keys.calls==3 && f.reply_length==strlen(expected) && memcmp(f.reply,expected,strlen(expected))==0
	    && f.errors==0 && f.host_errors==0 && f.read==f.length && f.text_length==5 && memcmp(f.text,"raw\r\n",5)==0);
	f.reply_length=0;
	CHECK(process(&f,"1\0330000$SHIFT(BAD)$$XY(BAD)$ok",-1)==0 && f.host_errors==2 && f.reply_length==2
	    && memcmp(f.reply,"ok",2)==0 && keys.calls==3);
	CHECK(process(&f,"1\0330000$CTRL$",-1)==0 && keys.calls==4 && f.reply_length==3 && f.reply[2]=='1');
	destroy(&f);return 0;
}

struct playback_name_seen {
	unsigned count;
	enum st_rip3_playback_kind kind;
	char name[ST_RIP3_PLAYBACK_NAME_SIZE];
};

static int
record_playback_name(void *context, enum st_rip3_playback_kind kind, const char *name)
{
	struct playback_name_seen *seen=context;
	seen->count++;
	seen->kind=kind;
	memcpy(seen->name,name,strlen(name)+1);
	return 0;
}

static int
playback_names(void)
{
	struct fixture f={0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0);
	struct local_modifiers keys={.shift=true};
	const struct st_rip3_local_queries local={.context=&keys,.modifiers=local_modifiers};
	struct st_rip3_field_context field={.active=true,.identity=7};
	f.variables.local=&local;f.variables.field_context=&field;
	struct playback_name_seen seen={0};
	st_rip3_query_bind_playback(f.host.query,&seen,record_playback_name);
	static const struct {
		const char *source, *name;
		enum st_rip3_playback_kind kind;
	} examples[]={
	    {"$>f*?[]$OK","F7010.RIP",ST_RIP3_PLAYBACK_SCENE},
	    {"$)f*?[]$OK","F7010.wav",ST_RIP3_PLAYBACK_WAVE},
	    {"$<f*?[]$OK","F7010.BMP",ST_RIP3_PLAYBACK_BITMAP},
	    {"$(f*?[]$OK","F7010.JPG",ST_RIP3_PLAYBACK_JPEG},
	    {"$>**??[[]]$OK","77001100.RIP",ST_RIP3_PLAYBACK_SCENE},
	    {"$>a\\$b.fn$OK","A$B.FN",ST_RIP3_PLAYBACK_SCENE},
	    {"$>a\\\\$b.fn$OK","A\\$B.FN",ST_RIP3_PLAYBACK_SCENE},
	    {"$)\\$OFF\\$$OK","$OFF$.wav",ST_RIP3_PLAYBACK_WAVE},
	    {"$<stamp.icn$OK","STAMP.BMP",ST_RIP3_PLAYBACK_BITMAP},
	    {"$<stamp.hic$OK","STAMP.BMH",ST_RIP3_PLAYBACK_BITMAP},
	    {"$<stamp.msk$OK","STAMP.BMM",ST_RIP3_PLAYBACK_BITMAP},
	    {"$>a\\*^m(,)$OK","A\\7^M(,).RIP",ST_RIP3_PLAYBACK_SCENE},
	    {"$(abcdefghijklmnopqrs\\$$OK","ABCDEFGHIJKLMNOPQRS$.JPG",ST_RIP3_PLAYBACK_JPEG},
	};
	for (size_t i=0; i<sizeof(examples)/sizeof(examples[0]); ++i) {
		f.reply_length=0;seen.count=0;
		CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)examples[i].source,strlen(examples[i].source))==0);
		CHECK(seen.count==1 && seen.kind==examples[i].kind && strcmp(seen.name,examples[i].name)==0);
		CHECK(!st_rip3_query_pending(f.host.query) && f.reply_length==2 && memcmp(f.reply,"OK",2)==0);
	}
	CHECK(keys.calls==5);
	field.identity=-1;
	f.reply_length=0;seen.count=0;
	static const char unidentified[]="$>f*?[]$";
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)unidentified,sizeof(unidentified)-1)==0);
	CHECK(seen.count==1 && strcmp(seen.name,"F65535010.RIP")==0);
	seen.count=0;
	static const char max[]="$>********************$";
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)max,sizeof(max)-1)==0 && seen.count==1);
	CHECK(strlen(seen.name)==104 && strcmp(seen.name+100,".RIP")==0);
	for (size_t i=0;i<100;i+=5) CHECK(memcmp(seen.name+i,"65535",5)==0);

	field.active=false;
	const struct st_rip3_mouse_sink mouse_sink={0};
	CHECK(st_rip3_mouse_create(&f.variables.mouse,&mouse_sink)==0);
	struct st_rip3_rect area={10,10,20,20};
	CHECK(st_rip3_mouse_define(f.variables.mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,
	    false,false,-1,NULL,0,NULL)==0);
	static const char captured[]="$(f*?[]$";
	CHECK(!st_rip3_mouse_event(f.variables.mouse,0,0,true));
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)captured,sizeof(captured)-1)==0);
	CHECK(strcmp(seen.name,"F010.JPG")==0);
	CHECK(!st_rip3_mouse_event(f.variables.mouse,0,0,false));
	CHECK(st_rip3_mouse_event(f.variables.mouse,13,13,true));
	CHECK(st_rip3_mouse_event(f.variables.mouse,0,0,true));
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)captured,sizeof(captured)-1)==0);
	CHECK(strcmp(seen.name,"F110.JPG")==0);
	field.active=true;field.identity=12;keys.shift=false;keys.ctrl=true;
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)unidentified,sizeof(unidentified)-1)==0);
	CHECK(strcmp(seen.name,"F12101.RIP")==0);
	field.active=false;
	CHECK(st_rip3_mouse_event(f.variables.mouse,0,0,false));
	st_rip3_mouse_destroy(f.variables.mouse);f.variables.mouse=NULL;

	const char *const literals[]={"$>abcdefghijklmnopqrstu$","$>ABC\\$","$<unclosed^m"};
	const char *const expected[]={"$>abcdefghijklmnopqrstu$","$>ABC$","$<unclosed\r"};
	for (size_t i=0;i<sizeof(literals)/sizeof(literals[0]);++i) {
		f.reply_length=0;seen.count=0;
		CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)literals[i],strlen(literals[i]))==0);
		CHECK(seen.count==0 && f.reply_length==strlen(expected[i])
		    && memcmp(f.reply,expected[i],f.reply_length)==0);
	}
	f.reply_length=0;seen.count=0;
	static const char empty[]="$>*$$<$OK";
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)empty,sizeof(empty)-1)==0);
	CHECK(seen.count==0 && f.reply_length==2 && memcmp(f.reply,"OK",2)==0);
	/* Expanded UTF-8 codepage bytes still consume one original filename
	 * unit. A real UTF-8 connection retains the raw byte bound. */
	uint8_t encoded[43]={'$','('};
	for (size_t i=2;i<42;i+=2) { encoded[i]=0xc3;encoded[i+1]=0xa9; }
	encoded[42]='$';
	f.reply_length=0;
	CHECK(st_rip3_query_start(f.host.query,encoded,sizeof(encoded))==0);
	CHECK(seen.count==0 && f.reply_length==sizeof(encoded) && memcmp(f.reply,encoded,sizeof(encoded))==0);
	st_rip3_query_set_codepage_converted(f.host.query,true);
	f.reply_length=0;
	CHECK(st_rip3_query_start(f.host.query,encoded,sizeof(encoded))==0);
	CHECK(seen.count==1 && f.reply_length==0 && strlen(seen.name)==44
	    && memcmp(seen.name,encoded+2,40)==0 && strcmp(seen.name+40,".JPG")==0);
	CHECK(f.errors==0 && f.host_errors==0);
	destroy(&f);
	return 0;
}

static int
allocation_and_budget(void)
{
	struct fixture f = {0};
	struct st_rip3_query *unchanged = NULL;
	struct st_rip3_host_variables variables = {0};
	struct st_rip3_host_sink sink = {0};
	CHECK(st_rip3_query_create(NULL,&variables,&sink) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_query_create(&unchanged,NULL,&sink) == IO_ERROR_INVALID_ARGUMENT && unchanged == NULL);
	CHECK(st_rip3_query_create(&unchanged,&variables,NULL) == IO_ERROR_INVALID_ARGUMENT && unchanged == NULL);
	st_rip3_query_test_allocation_countdown = 1;
	CHECK(st_rip3_query_create(&unchanged,&variables,&sink) == IO_ERROR_OUT_OF_MEMORY && unchanged == NULL);
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	st_rip3_query_test_allocation_countdown = 1;
	CHECK(process(&f,"1\0330000no$D(1)$effect",ST_RIP3_COMMAND_RESOURCE_FAILURE) == 0);
	CHECK(!st_rip3_query_pending(f.host.query) && st_rip3_input_wait_ns(f.host.input) == 0 && f.reply_length == 0);
	CHECK(st_rip3_query_start(f.host.query,NULL,1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)"x",SIZE_MAX) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_query_start(f.host.query,NULL,0) == 0);
	for (unsigned allocation=2; allocation<=4; allocation++) {
		unsigned errors=f.host_errors;
		st_rip3_query_test_allocation_countdown=allocation;
		CHECK(process(&f,"1\0330000safe",-1)==0 && f.reply_length==0 && !st_rip3_query_pending(f.host.query));
		CHECK(f.host_errors==errors+1 && f.host_kind==ST_RIP3_HOST_RESOURCE_FAILURE);
	}
	/* Bounded continuation yields before taking bytes, even without a delay. */
	char body[6+260+1];
	memcpy(body,"1\0330000",6);
	for (size_t i = 0; i < 130; i++) { body[6+2*i] = '^'; body[7+2*i] = 'M'; }
	body[sizeof(body)-1] = 0;
	CHECK(process(&f,body,-1)==0 && f.reply_length==0 && st_rip3_query_pending(f.host.query));
	CHECK(append(&f,"later\r\n")==0);
	st_rip3_input_process(f.host.input,0);CHECK(f.reply_length==0 && f.reads==0);
	for (unsigned pump=0; pump<3; pump++) {
		st_rip3_input_process(f.host.input,2048);
		CHECK(f.reply_length==0 && f.reads==0 && st_rip3_query_pending(f.host.query));
	}
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length==130 && f.read==f.length && !st_rip3_query_pending(f.host.query));
	for (size_t i=0; i<130; i++) CHECK(f.reply[i]==13);
	CHECK(f.text_length==7 && memcmp(f.text,"later\r\n",7)==0);
	/* Known operations read current state when resumed, rather than caching
	 * future values in preflight. There need not be another source byte. */
	CHECK(process(&f,"1\0330000$D(1)$$TERMINFO(NAME)$",-1) == 0);
	const struct st_rip3_terminal_identity later = {"Later","Vendor","Version"};
	f.variables.identity = &later;
	f.now = 16666667; st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length == 135 && memcmp(f.reply+130,"Later",5) == 0 && !st_rip3_query_pending(f.host.query));
	destroy(&f);
	st_rip3_query_cancel(NULL); st_rip3_query_process(NULL,1); st_rip3_query_destroy(NULL);
	CHECK(!st_rip3_query_pending(NULL));
	return 0;
}

static int
angle_queries(enum st_rip3_canvas canvas, const char *width)
{
	struct fixture f={0};CHECK(create(&f,canvas)==0 && bind_users(&f)==0 && st_rip3_templates_create(&f.templates.slots)==0);
	f.templates.context=&f;f.templates.selection_required=selection_required;f.variables.templates=&f.templates;
	struct st_rip3_table_host tables={0};tables.tables[ST_RIP3_TABLE_ENV].live=f.host.environments;f.variables.tables=&tables;
	CHECK(st_rip3_variables_set(f.session,"A",1,(const uint8_t *)"<<B>>",5)==0
	    && st_rip3_variables_set(f.session,"B",1,(const uint8_t *)"$RESX$",6)==0);
	CHECK(process(&f,"1\0330000<<A>>",-1)==0 && f.reply_length==strlen(width) && memcmp(f.reply,width,strlen(width))==0);
	char body[400];(void)snprintf(body,sizeof(body),"1\0330000<<IF RESX=%s>>yes<<ELSEIF RESX(1)=0>>bad<<ELSE>>no<<ENDIF>>",width);
	f.reply_length=0;CHECK(process(&f,body,-1)==0 && f.reply_length==3 && memcmp(f.reply,"yes",3)==0 && f.host_errors==0);
	f.reply_length=0;CHECK(process(&f,"1\0330000<<IF RESX(1)!=0>>bad<<ELSE>>ok<<ENDIF>>",-1)==0
	    && f.reply_length==2 && memcmp(f.reply,"ok",2)==0 && f.host_errors==0);
	f.reply_length=0;CHECK(process(&f,"1\0330000<<-=FLAG=before>>",-1)==0 && user_value(f.session,"FLAG","before")==0);
	f.reply_length=0;CHECK(process(&f,"1\0330000<<IF RESX=0>><<-=FLAG=after>>$-=FLAG=wrong$<<ELSE>>x<<ENDIF>>",-1)==0
	    && f.reply_length==1 && f.reply[0]=='x' && user_value(f.session,"FLAG","after")==0);
	/* Substitution precedes segmentation/definition storage, while a later
	 * ordinary insertion never starts another angle-preprocessor invocation. */
	CHECK(st_rip3_variables_set(f.session,"B",1,(const uint8_t *)"[3:]raw[]$?3$",13)==0);
	f.reply_length=0;CHECK(process(&f,"1\0330000<<B>>",-1)==0 && f.reply_length==3 && memcmp(f.reply,"raw",3)==0
	    && template_value(&f,3,"raw")==0);
	CHECK(st_rip3_variables_set(f.session,"B",1,(const uint8_t *)"Done",4)==0);
	CHECK(append(&f,"!|1\0330000<<*=ASK>>|J1S\rraw\r\n")==0);st_rip3_input_process(f.host.input,2048);
	CHECK(st_rip3_query_pending(f.host.query) && st_rip3_query_prompt_pending(f.host.query)
	    && strcmp(st_rip3_user_variables_prompt(f.variables.user_variables)->name,"ASK")==0);
	size_t stopped=f.read;unsigned reads=f.reads;f.reply_length=0;
	st_rip3_input_process(f.host.input,2048);CHECK(f.read==stopped && f.reads==reads);
	st_rip3_user_variables_respond(f.variables.user_variables,(const uint8_t *)"<<B>>",5,false);
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.read==f.length && !st_rip3_query_pending(f.host.query) && f.reply_length==4 && memcmp(f.reply,"Done",4)==0
	    && user_value(f.session,"ASK","<<B>>")==0 && f.text_length==5 && memcmp(f.text,"raw\r\n",5)==0);
	f.reply_length=0;CHECK(process(&f,"1\0330000<<*=UNFINISHED>>",-1)==0 && st_rip3_query_prompt_pending(f.host.query));
	st_rip3_query_cancel(f.host.query);
	CHECK(!st_rip3_query_pending(f.host.query) && st_rip3_user_variables_state(f.variables.user_variables)==ST_RIP3_USER_VARIABLE_IDLE
	    && user_value(f.session,"UNFINISHED",NULL)==0 && f.reply_length==0);
	f.reply_length=0;CHECK(process(&f,"1\0330000<<D(1)>><<TERMINFO(NAME)>>",-1)==0
	    && st_rip3_input_wait_ns(f.host.input)==16666667 && st_rip3_query_pending(f.host.query));
	const struct st_rip3_terminal_identity later={"Later","Tests","0"};f.variables.identity=&later;
	f.now+=16666667;st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length==5 && memcmp(f.reply,"Later",5)==0 && !st_rip3_query_pending(f.host.query));
	unsigned errors=f.host_errors;f.reply_length=0;
	CHECK(process(&f,"1\0330000$-=FLAG=wrong$<<IF N>>",-1)==0 && f.host_errors==errors+1
	    && f.host_kind==ST_RIP3_HOST_INVALID_REFERENCE && f.reply_length==0 && user_value(f.session,"FLAG","after")==0);
	CHECK(process(&f,"1\0330000<<APP>>",-1)==0 && f.host_errors==errors+2 && !st_rip3_query_pending(f.host.query));
	errors=f.host_errors;
	CHECK(process(&f,"1\0330000<<-=FLAG=kept>><<IF N>>",-1)==0 && f.host_errors==errors+1
	    && f.reply_length==0 && user_value(f.session,"FLAG","kept")==0);
	CHECK(st_rip3_templates_set(f.templates.slots,3,NULL,0)==0 && required_group(f.templates.slots,3,true)==0);
	CHECK(process(&f,"1\0330000<<IF RESX=0>><<?3>><<ENDIF>>",-1)==0 && f.required_calls==1 && f.required_group==3
	    && !st_rip3_query_pending(f.host.query) && f.reply_length==0);
	/* The eight-pass success result may still contain deferred angle syntax. */
	for (unsigned n=0; n<10; n++) {
		char text[16];(void)snprintf(text,sizeof(text),"<<?%u>>",n+1);
		CHECK(required_group(f.templates.slots,n,false)==0
		    && st_rip3_templates_set(f.templates.slots,n,(const uint8_t *)(n==9 ? "END" : text),n==9 ? 3 : strlen(text))==0);
	}
	f.reply_length=0;CHECK(process(&f,"1\0330000<<?0>>",-1)==0);
	for (unsigned i=0; i<20 && st_rip3_query_pending(f.host.query); i++) st_rip3_query_process(f.host.query,64);
	CHECK(!st_rip3_query_pending(f.host.query) && f.reply_length==6 && memcmp(f.reply,"<<?8>>",6)==0);
	/* The 4096 response cap follows preprocessing; it is not a whole-program
	 * cap that could discard a later direct response block before segmentation. */
	char large[5001];memset(large,'X',5000);large[5000]=0;
	const struct st_rip3_terminal_identity big={large,"Tests","0"};f.variables.identity=&big;
	f.reply_length=0;CHECK(process(&f,"1\0330000<<TERMINFO(NAME)>>[]tail",-1)==0);
	for (unsigned i=0; i<20 && st_rip3_query_pending(f.host.query); i++) st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==4100 && memcmp(f.reply+4096,"tail",4)==0);
	for (size_t i=0; i<4096; i++) CHECK(f.reply[i]=='X');
	f.reply_length=0;CHECK(process(&f,"1\0330000<<IF TERMINFO(NAME)!=x>>bad<<ELSE>>ok<<ENDIF>>",-1)==0
	    && f.reply_length==2 && memcmp(f.reply,"ok",2)==0);
	f.reply_length=0;errors=f.host_errors;st_rip3_query_test_allocation_countdown=1;
	CHECK(process(&f,"1\0330000<<RESX>>",-1)==0 && f.host_errors==errors+1
	    && f.host_kind==ST_RIP3_HOST_RESOURCE_FAILURE && !st_rip3_query_pending(f.host.query) && f.reply_length==0);
	const uint8_t nul_source[]={'a',0,'<','<','X','>','>'};
	CHECK(st_rip3_query_start(f.host.query,nul_source,sizeof(nul_source))==0 && f.reply_length==1 && f.reply[0]=='a');
	CHECK(!f.overflow && f.user_errors==0 && f.errors==0);destroy(&f);return 0;
}

static int
pick_list_playback(void *context, enum st_rip3_playback_kind kind, const char *filename)
{
	if (kind!=ST_RIP3_PLAYBACK_JPEG || strcmp(filename,"PICTURE.JPG")!=0)
		return IO_ERROR_INVALID_ARGUMENT;
	++*(unsigned *)context;
	return 0;
}

static int
ordinary_pick_lists(void)
{
	struct fixture f={0};
	CHECK(create(&f,ST_RIP3_CANVAS_640_480)==0 && bind_users(&f)==0);
	static const struct {
		const char *source, *question, *first, *reply;
		size_t count, choice;
	} vectors[]={
	    {"pre((A,B))post","Please enter requested value","A","preBpost",2,1},
	    {"pre((A,B))post","Please enter requested value","A","prepost",2,2},
	    {"((::A,B))","","A","A",2,0},
	    {"((Q::*A,B))","Q","*A","*A",2,0},
	    {"((,A,,B,))","Please enter requested value","","",4,2},
	    {"((A\\,B@One\\@Label,C))","Please enter requested value","One@Label","A,B",2,0},
	    {"((A\\)\\),B))","Please enter requested value","A))","A))",2,0},
	    {"((\\(\\(A\\,B\\)\\)))","Please enter requested value","((A,B))","((A,B))",1,0},
	    {"((Q::((A,B))tail","Q","((A","((Atail",2,0},
	    {"((\"A,B\"))","Please enter requested value","\"A","\"A",2,0},
	    {"((Q::A@B))((Q::C@D))","Q","B","AC",1,0},
	    {"((Q::A@D@E,B))TAIL",NULL,NULL,"TAIL",0,0},
	    {"pre((A^m",NULL,NULL,"pre((A\r",0,0},
	    {"((::))((*::))TAIL",NULL,NULL,"TAIL",0,0},
	    {"((Q::$RESX$))","Q","$RESX$","640",1,0},
	    {"((Q::^m))","Q","^m","\r",1,0},
	    {"$&LATER$","Q","$RESX$","$RESX$",1,0},
	    {"$&LATERCTRL$","Q","^m","^m",1,0},
	    {"((Q::$&INDIRECT$))","Q","$&INDIRECT$","$RESX$",1,0},
	    {"(($&CLOSE$","Please enter requested value","A","A",1,0},
	};
	CHECK(st_rip3_variables_set(f.session,"LATER",5,(const uint8_t *)"((Q::$RESX$))",13)==0);
	CHECK(st_rip3_variables_set(f.session,"LATERCTRL",9,(const uint8_t *)"((Q::^m))",9)==0);
	CHECK(st_rip3_variables_set(f.session,"INDIRECT",8,(const uint8_t *)"$RESX$",6)==0);
	CHECK(st_rip3_variables_set(f.session,"CLOSE",5,(const uint8_t *)"A))",3)==0);
	for (size_t i=0; i<sizeof(vectors)/sizeof(vectors[0]); ++i) {
		f.reply_length=0;
		CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)vectors[i].source,strlen(vectors[i].source))==0);
		const struct st_rip3_pick_list *view=st_rip3_query_pick_list(f.host.query);
		if (vectors[i].count==0) CHECK(view==NULL);
		else {
			CHECK(view!=NULL && view->count==vectors[i].count && f.reply_length==0);
			CHECK(strcmp(view->question.bytes,vectors[i].question)==0 && strcmp(view->labels[0].bytes,vectors[i].first)==0);
			st_rip3_query_process(f.host.query,64);
			CHECK(f.reply_length==0 && st_rip3_query_pick_list(f.host.query)==view);
			CHECK(st_rip3_query_choose(f.host.query,vectors[i].choice));
		}
		st_rip3_query_process(f.host.query,64);
		if (strcmp(vectors[i].reply,"AC")==0) {
			view=st_rip3_query_pick_list(f.host.query);
			CHECK(f.reply_length==0 && view!=NULL && view->count==1 && strcmp(view->labels[0].bytes,"D")==0);
			CHECK(st_rip3_query_choose(f.host.query,0));
			st_rip3_query_process(f.host.query,64);
		}
		CHECK(!st_rip3_query_pending(f.host.query));
		CHECK(f.reply_length==strlen(vectors[i].reply) && memcmp(f.reply,vectors[i].reply,f.reply_length)==0);
	}
	const char required[]="((*10,:Q::@\\_H_~I~))";
	f.reply_length=0;
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)required,sizeof(required)-1)==0);
	const struct st_rip3_pick_list *view=st_rip3_query_pick_list(f.host.query);
	CHECK(view!=NULL && view->required && view->has_x && view->x==10 && !view->has_y);
	CHECK(view->count==1 && strcmp(view->labels[0].bytes,"HI")==0 && view->labels[0].highlighted==3);
	CHECK(!st_rip3_query_choose(f.host.query,view->count) && st_rip3_query_pending(f.host.query));
	CHECK(st_rip3_query_choose(f.host.query,0));
	st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==0 && !st_rip3_query_pending(f.host.query));
	const char overflowing[]="((65536,1:Q::A))";
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)overflowing,sizeof(overflowing)-1)==0);
	view=st_rip3_query_pick_list(f.host.query);
	CHECK(view!=NULL && !view->has_x && !view->has_y && strcmp(view->question.bytes,"65536,1:Q")==0);
	st_rip3_query_pick_list_failed(f.host.query,IO_ERROR_OUT_OF_MEMORY);
	CHECK(!st_rip3_query_pending(f.host.query) && f.host_errors==1 && f.host_kind==ST_RIP3_HOST_RESOURCE_FAILURE);
	uint8_t large[4100];
	memcpy(large,"((",2);
	memset(large+2,'q',61);
	memcpy(large+63,"::",2);
	memset(large+65,'X',1024);
	large[1089]='@';
	memset(large+1090,'d',59);
	memcpy(large+1149,"\\E,",3);
	size_t used=1152;
	for (unsigned i=1; i<64; ++i) {
		memcpy(large+used,"R,",2);used+=2;
	}
	memcpy(large+used,"ignore@malformed@too))",22);used+=22;
	CHECK(st_rip3_query_start(f.host.query,large,used)==0);
	view=st_rip3_query_pick_list(f.host.query);
	CHECK(view!=NULL && view->count==64 && strlen(view->question.bytes)==60
	    && strlen(view->labels[0].bytes)==59 && strcmp(view->labels[63].bytes,"R")==0);
	CHECK(st_rip3_query_choose(f.host.query,0));
	st_rip3_query_process(f.host.query,64);
	CHECK(f.reply_length==1023 && memcmp(f.reply,large+65,1023)==0);
	f.reply_length=0;
	memset(large+2,'X',4094);memcpy(large+4096,"))",2);
	CHECK(st_rip3_query_start(f.host.query,large,4098)==0 && st_rip3_query_pick_list(f.host.query)==NULL);
	CHECK(f.reply_length==4096 && memcmp(f.reply,large,4096)==0);
	/* Only a later pass can execute a playback directive from a choice. */
	unsigned played=0;
	st_rip3_query_bind_playback(f.host.query,&played,pick_list_playback);
	const char media[]="((Q::$(PICTURE.JPG$@Picture))";
	CHECK(st_rip3_variables_set(f.session,"MEDIA",5,(const uint8_t *)media,sizeof(media)-1)==0);
	f.reply_length=0;
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)media,sizeof(media)-1)==0 && played==0);
	CHECK(st_rip3_query_choose(f.host.query,0));
	st_rip3_query_process(f.host.query,64);
	CHECK(played==1 && f.reply_length==0);
	CHECK(st_rip3_query_start(f.host.query,(const uint8_t *)"$&MEDIA$",8)==0);
	CHECK(st_rip3_query_choose(f.host.query,0));
	st_rip3_query_process(f.host.query,64);
	CHECK(played==1 && f.reply_length==14 && memcmp(f.reply,"$(PICTURE.JPG$",14)==0);
	CHECK(f.errors==0 && f.host_errors==1 && !f.overflow);
	destroy(&f);
	return 0;
}

int
main(void)
{
	CHECK(angle_queries(ST_RIP3_CANVAS_640_480,"640")==0 && angle_queries(ST_RIP3_CANVAS_800_600,"800")==0
	    && angle_queries(ST_RIP3_CANVAS_1024_768,"1024")==0);
	CHECK(compound(ST_RIP3_CANVAS_640_480,"640") == 0);
	CHECK(compound(ST_RIP3_CANVAS_800_600,"800") == 0);
	CHECK(compound(ST_RIP3_CANVAS_1024_768,"1024") == 0);
	CHECK(user_queries(ST_RIP3_CANVAS_640_480,"640")==0);
	CHECK(user_queries(ST_RIP3_CANVAS_800_600,"800")==0);
	CHECK(user_queries(ST_RIP3_CANVAS_1024_768,"1024")==0);
	CHECK(reset_text_variables()==0);
	CHECK(user_recovery()==0);
	CHECK(two_passes()==0);
	CHECK(ordinary_pick_lists()==0);
	CHECK(playback_names()==0);
	CHECK(template_queries(ST_RIP3_CANVAS_640_480)==0);
	CHECK(template_queries(ST_RIP3_CANVAS_800_600)==0);
	CHECK(template_queries(ST_RIP3_CANVAS_1024_768)==0);
	CHECK(embedding_limits(ST_RIP3_CANVAS_640_480)==0);
	CHECK(embedding_limits(ST_RIP3_CANVAS_800_600)==0);
	CHECK(embedding_limits(ST_RIP3_CANVAS_1024_768)==0);
	CHECK(owned_and_recovery() == 0);
	CHECK(active_order() == 0);
	CHECK(controls() == 0);
	CHECK(allocation_and_budget() == 0);
	CHECK(local_queries()==0);
	CHECK(fields() == 0);
	CHECK(query_forms() == 0);
	CHECK(recovery() == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_640_480,"640") == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_800_600,"800") == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_1024_768,"1024") == 0);
	return 0;
}
