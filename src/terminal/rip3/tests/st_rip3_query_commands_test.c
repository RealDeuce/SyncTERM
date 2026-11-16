/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_stream.h"
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
	};
	for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); i++) {
		CHECK(reference(&f,valid[i].text,-1) == 0 && st_rip3_input_wait_ns(f.host.input) == valid[i].duration);
		CHECK(f.replies == 0);
	}
	const char *const invalid[] = {"$D$","$D()$","$D(0)$","$D(-1)$","$D(+1)$","$D(1.0)$",
	    "$D(1O)$","$D(65536)$","$D(999999999999)$","$D(0000000000001)$","$D(1,2)$"};
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
	const char *const unsupported[] = {"1\0331000$D(1)$","1\0330001$D(1)$","1\0330000$D(1)$tail$UNKNOWN$",
	    "1\0330000literal[template]","1\0330000((question::one,two))","1\0330000$>FILE.RIP$","1\0330000$UNKNOWN_NAME$"};
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		CHECK(process(&f,unsupported[i],ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(st_rip3_input_wait_ns(f.host.input) == 0 && f.replies == 0 && f.host_errors == 0);
	}
	CHECK(process(&f,"1\0330?00$D(1)$",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1\0330",ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&f,"1\0330A00$RESX$",-1) == 0 && f.reply_length == 3 && memcmp(f.reply,"640",3) == 0);
	CHECK(process(&f,"J1S",-1) == 0);
	CHECK(process(&f,"1\03300#0$RESX$",ST_RIP3_COMMAND_UNSUPPORTED) == 0 && f.reply_length == 3);
	CHECK(process(&f,"J10",-1) == 0);
	CHECK(process(&f,"1\03300#0$RESX$",ST_RIP3_COMMAND_INVALID) == 0 && f.reply_length == 3);
	struct st_rip3_query *bound = f.host.query;
	f.host.query = NULL;
	CHECK(process(&f,"1\0330000$D(1)$",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.query = bound;
	f.query_sink.reply = NULL;
	CHECK(process(&f,"1\0330000$D(1)$",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.query_sink.reply = reply;
	CHECK(process(&f,"1\0330000$D(0)$",-1) == 0 && f.host_errors == 1 && f.host_kind == ST_RIP3_HOST_INVALID_REFERENCE);
	CHECK(process(&f,"1\0330000$APP$",-1) == 0 && f.host_errors == 2 && f.host_kind == ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(process(&f,"1\0330000$GOTOURL$",-1) == 0 && f.host_errors == 3 && f.host_kind == ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 0);
	/* No reply truncation or artificial fixed buffer in the query bridge. */
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
	CHECK(f.errors == 1 && f.kind == ST_RIP3_COMMAND_UNSUPPORTED);
	CHECK(f.host_errors == 1 && f.host_kind == ST_RIP3_HOST_INVALID_REFERENCE);
	CHECK(f.replies == 1 && f.reply_length == 7 && memcmp(f.reply,"Fixture",7) == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667 && f.read < f.length);
	f.now = 16666667; st_rip3_input_process(f.host.input,2048);
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
	CHECK(f.reply_length == 1 && f.reply[0] == 'a' && st_rip3_query_pending(f.host.query));
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667);
	f.now = 16666666; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == stopped && f.reply_length == 1);
	f.now++; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == stopped && f.reply_length == 2+strlen(width));
	CHECK(memcmp(f.reply,"ab",2) == 0 && memcmp(f.reply+2,width,strlen(width)) == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 33333334);
	f.now += 33333333; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == stopped && st_rip3_query_pending(f.host.query));
	f.now++; st_rip3_input_process(f.host.input,2048);
	const uint8_t tail[] = {'c',0,13,'`','^',7,27,'[','A',27,'[','K'};
	CHECK(f.read == f.length && !st_rip3_query_pending(f.host.query));
	CHECK(f.reply_length == 2+strlen(width)+sizeof(tail));
	CHECK(memcmp(f.reply+2+strlen(width),tail,sizeof(tail)) == 0);
	CHECK(f.text_length == 5 && memcmp(f.text,"raw\r\n",5) == 0);
	CHECK(f.errors == 0 && f.host_errors == 0 && !f.overflow);
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
	CHECK(f.reply_length == sizeof(expected)-1 && memcmp(f.reply,expected,sizeof(expected)-1) == 0);
	CHECK(f.host_errors == 2 && f.host_kind == ST_RIP3_HOST_UNSUPPORTED_REFERENCE);
	CHECK(!st_rip3_query_pending(f.host.query) && f.read == 0);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667); /* Even a final delay blocks later input. */
	f.now += 16666667; st_rip3_input_process(f.host.input,2048);
	CHECK(f.read == f.length && f.text_length == 7);
	CHECK(process(&f,"1\0330000$D(1)$cancelled",-1) == 0);
	st_rip3_query_cancel(f.host.query);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667);
	st_rip3_input_reset(f.host.input);
	f.reply_length = 0;
	CHECK(process(&f,"1\0330000after",-1) == 0 && f.reply_length == 5 && memcmp(f.reply,"after",5) == 0);
	/* Every unsupported tail is preflighted before any literal or D effect. */
	const char *const tails[] = {"$UNKNOWN$","$>FILE.RIP$","<<macro>>","((x::a,b))","[0]","$D(1)","^?","^","`","\\!"};
	for (size_t i = 0; i < sizeof(tails)/sizeof(tails[0]); i++) {
		char body[160];
		(void)snprintf(body,sizeof(body),"1\0330000no$D(1)$%s",tails[i]);
		CHECK(process(&f,body,ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(f.reply_length == 5 && st_rip3_input_wait_ns(f.host.input) == 0 && !st_rip3_query_pending(f.host.query));
	}
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
		{"^@",0},{"^A",1},{"^B",2},{"^C",3},{"^D",4},{"^E",5},{"^F",6},{"^G",7},
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
	const char *const keys[] = {"^[[A","^[[B","^[[C","^[[D","^[[H","^[[K","^[[L"};
	const uint8_t expected[][3] = {{27,'[','A'},{27,'[','B'},{27,'[','C'},{27,'[','D'},
	    {27,'[','H'},{27,'[','K'},{27,'[','L'}};
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

extern unsigned st_rip3_query_test_allocation_countdown;

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
	/* Bounded continuation yields before taking bytes, even without a delay. */
	char body[6+260+1];
	memcpy(body,"1\0330000",6);
	for (size_t i = 0; i < 130; i++) { body[6+2*i] = '^'; body[7+2*i] = 'M'; }
	body[sizeof(body)-1] = 0;
	CHECK(process(&f,body,-1) == 0 && f.reply_length == 64 && st_rip3_query_pending(f.host.query));
	CHECK(append(&f,"later\r\n") == 0);
	st_rip3_input_process(f.host.input,0);
	CHECK(f.reply_length == 64 && f.reads == 0);
	st_rip3_query_test_allocation_countdown = 1;
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length == 128 && f.reads == 0 && st_rip3_query_pending(f.host.query));
	st_rip3_input_process(f.host.input,2048);
	CHECK(f.reply_length == 130 && f.read == f.length && !st_rip3_query_pending(f.host.query));
	CHECK(st_rip3_query_test_allocation_countdown == 1);
	st_rip3_query_test_allocation_countdown = 0;
	for (size_t i = 0; i < 130; i++) CHECK(f.reply[i] == 13);
	CHECK(f.text_length == 7 && memcmp(f.text,"later\r\n",7) == 0);
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

int
main(void)
{
	CHECK(compound(ST_RIP3_CANVAS_640_480,"640") == 0);
	CHECK(compound(ST_RIP3_CANVAS_800_600,"800") == 0);
	CHECK(compound(ST_RIP3_CANVAS_1024_768,"1024") == 0);
	CHECK(owned_and_recovery() == 0);
	CHECK(active_order() == 0);
	CHECK(controls() == 0);
	CHECK(allocation_and_budget() == 0);
	CHECK(fields() == 0);
	CHECK(query_forms() == 0);
	CHECK(recovery() == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_640_480,"640") == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_800_600,"800") == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_1024_768,"1024") == 0);
	return 0;
}
