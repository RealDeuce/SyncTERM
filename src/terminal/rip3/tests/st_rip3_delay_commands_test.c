/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_input.h"
#include "st_rip3_stream.h"

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
	struct st_rip3_stream *stream;
	uint64_t now;
	uint8_t queue[1024], text[1024];
	size_t length, read, text_length;
	unsigned reads, errors;
	enum st_rip3_command_diagnostic kind;
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
	memcpy(f->text+f->text_length,bytes,length); f->text_length += length;
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
	const struct st_rip3_input_source source = {.context=f,.now_ns=now,.next=next,.consume=consume};
	const struct st_rip3_stream_sink sink = {.context=f,.text=text,.command=command};
	CHECK(st_rip3_environments_create(&f->host.environments,canvas) == 0);
	CHECK(st_rip3_input_create(&f->host.input,&source) == 0);
	CHECK(st_rip3_stream_create(&f->stream,&sink) == 0);
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_input_destroy(f->host.input);
	st_rip3_stream_destroy(f->stream);
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
	if (f->errors != previous+(error < 0 ? 0u : 1u) || (error >= 0 && (int)f->kind != error))
		fprintf(stderr,"command %s: errors %u, kind %d, expected %d\n",bytes,f->errors-previous,(int)f->kind,error);
	CHECK(f->errors == previous+(error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)f->kind == error);
	return 0;
}

static int
pipeline(enum st_rip3_canvas canvas)
{
	struct fixture f = {0};
	struct st_rip3_environment environment;
	CHECK(create(&f,canvas) == 0);
	CHECK(append(&f,"before\r\n!|3D0001|J1S|3D0001|J10\r") == 0);
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.read == strlen("before\r\n!|3D0001|") && f.reads == f.read);
	CHECK(f.text_length == 8 && memcmp(f.text,"before\r\n",8) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 36);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667);
	CHECK(append(&f,"after \xb3\r\n\xe2\x98\x83\r\n!|3D0|J1S\r") == 0);
	f.now = 16666666;
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.read == strlen("before\r\n!|3D0001|") && f.reads == f.read);
	f.now++;
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.read == strlen("before\r\n!|3D0001|J1S|3D0001|"));
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 64);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 16666667);
	f.now += 16666667;
	st_rip3_input_process(f.host.input,1024);
	const char expected[] = "before\r\nafter \xb3\r\n\xe2\x98\x83\r\n";
	CHECK(f.read == f.length && f.errors == 0 && st_rip3_input_wait_ns(f.host.input) == 0);
	CHECK(f.text_length == sizeof(expected)-1 && memcmp(f.text,expected,sizeof(expected)-1) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 64);
	destroy(&f);
	return 0;
}

static int
fields(void)
{
	struct fixture f = {0};
	struct st_rip3_environment environment;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_environments_get(f.host.environments,0,&environment) == 0);
	CHECK(st_rip3_environments_set_playback_rate(f.host.environments, 2399) == 0);
	environment.graphics_style = 7; environment.port = 9;
	const struct { unsigned radix; const char *command; uint64_t duration; } cases[] = {
		{36,"3D0",0},{36,"3D0000",0},{36,"3D1",16666667},{36,"3D0001",16666667},
		{36,"3D001O",1000000000},{36,"3D002D",1416666667},{36,"3D--1O",1000000000},
		{36,"3DZZZZ",27993583333334},{64,"3Da",600000000},
		{64,"3D&&&&",279620250000000},
	};
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
		environment.base_math = cases[i].radix;
		if (i != 0) CHECK(st_rip3_environments_protect(f.host.environments,1,false) == 0);
		CHECK(st_rip3_environments_define(f.host.environments,1,&environment) == 0);
		CHECK(st_rip3_environments_select(f.host.environments,1) == 0 && st_rip3_environments_protect(f.host.environments,1,true) == 0);
		CHECK(process(&f,cases[i].command,-1) == 0);
		CHECK(st_rip3_input_wait_ns(f.host.input) == cases[i].duration);
		CHECK(st_rip3_environments_get(f.host.environments,1,&environment) == 0
		    && st_rip3_environments_playback_rate(f.host.environments) == 2399 && environment.graphics_style == 7 && environment.port == 9);
	}
	const char *const invalid[] = {"3D","3D00000","3D0?","3D0001tail","3D-"};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		st_rip3_input_delay(f.host.input,60);
		CHECK(process(&f,invalid[i],ST_RIP3_COMMAND_INVALID) == 0 && st_rip3_input_wait_ns(f.host.input) == 1000000000);
	}
	CHECK(process(&f,"3D0001 comment",ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	struct st_rip3_input *bound = f.host.input; f.host.input = NULL;
	CHECK(process(&f,"3D0001",ST_RIP3_COMMAND_UNSUPPORTED) == 0); f.host.input = bound;
	CHECK(st_rip3_input_wait_ns(bound) == 1000000000);
	CHECK(process(&f,"3D0",-1) == 0 && st_rip3_input_wait_ns(bound) == 0);
	CHECK(f.read == 0 && f.reads == 0);
	destroy(&f);
	return 0;
}

static int
recovery(void)
{
	struct fixture f = {0};
	struct st_rip3_environment environment;
	CHECK(create(&f,ST_RIP3_CANVAS_640_480) == 0);
	CHECK(append(&f,"!|3D000?|3D0001\r") == 0);
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.errors == 1 && f.kind == ST_RIP3_COMMAND_INVALID && f.read == f.length);
	CHECK(append(&f,"!|J1S\r") == 0);
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.read < f.length);
	f.now = 16666667;
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.read == f.length && st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 64);
	CHECK(append(&f,"!|3D003m|J10\r") == 0); /* 240 ticks in base 64. */
	st_rip3_input_process(f.host.input,1024);
	CHECK(st_rip3_input_wait_ns(f.host.input) == 4000000000);
	st_rip3_input_reset(f.host.input);
	st_rip3_input_process(f.host.input,1024);
	CHECK(f.read == f.length && st_rip3_environments_get(f.host.environments,0,&environment) == 0 && environment.base_math == 36);
	destroy(&f);
	return 0;
}

int
main(void)
{
	CHECK(pipeline(ST_RIP3_CANVAS_640_480) == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_800_600) == 0);
	CHECK(pipeline(ST_RIP3_CANVAS_1024_768) == 0);
	CHECK(fields() == 0);
	CHECK(recovery() == 0);
	return 0;
}
