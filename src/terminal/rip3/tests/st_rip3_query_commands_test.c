/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_input.h"
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

static int
create(struct fixture *f, enum st_rip3_canvas canvas)
{
	static const struct st_rip3_terminal_identity identity = {"Fixture","Tests","0"};
	const struct st_rip3_input_source source = {f,now,next,consume};
	const struct st_rip3_stream_sink sink = {.context=f,.text=text,.command=command};
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_input_create(&f->host.input,&source) == 0);
	CHECK(st_rip3_stream_create(&f->stream,&sink) == 0);
	CHECK(st_rip3_frame_init(&f->frame,canvas) == 0);
	f->variables.frame = &f->frame; f->variables.identity = &identity; f->variables.input = f->host.input;
	f->query_sink = (struct st_rip3_host_sink){f,reply,host_diagnostic};
	f->host.host_variables = &f->variables; f->host.query_sink = &f->query_sink;
	return 0;
}

static void
destroy(struct fixture *f)
{
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
	const char *const unsupported[] = {"1\0331000$D(1)$","1\0330001$D(1)$","1\0330000$D(1)$tail",
	    "1\0330000$D(1)$$NULL$","1\0330000literal","1\0330000$>FILE.RIP$","1\0330000$UNKNOWN_NAME$"};
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
	const struct st_rip3_host_variables *bound = f.host.host_variables;
	f.host.host_variables = NULL;
	CHECK(process(&f,"1\0330000$D(1)$",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.host_variables = bound;
	f.host.query_sink = NULL;
	CHECK(process(&f,"1\0330000$D(1)$",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.query_sink = &f.query_sink;
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
	CHECK(append(&f,"!|1\0330000$D(0)$|1\0330000$D(1)$tail|1\0330000$TERMINFO(NAME)$|J1S|1\0330000$D(1)$|1\0330000$RESX$\r") == 0);
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

int
main(void)
{
	CHECK(fields() == 0);
	CHECK(query_forms() == 0);
	CHECK(recovery() == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_640_480,"640") == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_800_600,"800") == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_1024_768,"1024") == 0);
	return 0;
}
