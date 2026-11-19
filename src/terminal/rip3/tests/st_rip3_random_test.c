/* Authored endpoint/wrapping vectors and the published original CRT sequence. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct observation {
	unsigned errors, replies;
	enum st_rip3_host_diagnostic kind;
	size_t length;
	uint8_t bytes[24];
};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen=context;
	seen->replies++;seen->length=length;
	if (length<=sizeof(seen->bytes)) memcpy(seen->bytes,bytes,length);
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observation *seen=context;
	seen->errors++;seen->kind=kind;
}

/* A public processing error cannot become a terminal/session return value. */
static void (*const process)(const struct st_rip3_host_variables *,
    const struct st_rip3_host_sink *,const uint8_t *,size_t)=st_rip3_host_variables_process;

static int
run(const struct st_rip3_host_variables *host, const char *text, const char *expected)
{
	struct observation seen={0};
	const struct st_rip3_host_sink sink={&seen,reply,diagnostic};
	process(host,&sink,(const uint8_t *)text,strlen(text));
	CHECK(seen.errors==0 && seen.replies==1 && seen.length==strlen(expected)
	    && seen.length<=sizeof(seen.bytes) && memcmp(seen.bytes,expected,seen.length)==0);
	return 0;
}

static int
vectors(void)
{
	/* Seeds solve the generator's modular congruence for the next 32-bit
	 * words 00000000, 40000000 and 7fff0000, respectively. */
	static const struct {
		uint32_t seed, next;
		const char *text, *expected;
	} cases[]={
		{0xa170f641,0,"$RAND(9,3,2)$","002"},
		{0xe170f641,0x40000000,"$RAND(9,3,2)$","006"},
		{0xf01bf641,0x7fff0000,"$RAND(9,3,2)$","010"},
		{0xf01bf641,0x7fff0000,"$RAND(32767,1)$","32768"},
		{0xa170f641,0,"$RAND(9,,2)$","00"}, /* Empty field omitted: width 2. */
		{0xf01bf641,0x7fff0000,"$rAnD(,\\9,)$","10"},
		{0xf01bf641,0x7fff0000,"$RAND(4294967297)$","2"},
		{0xf01bf641,0x7fff0000,"$RAND(18446744073709551617)$","2"},
		{0xa170f641,0,"$RAND(9,4294967295)$","0"},
		{0xa170f641,0,"$RAND(9,2147483648)$","0"},
		{0xa170f641,0,"$RAND(9,4294967301)$","00000"},
		{0xa170f641,0,"$RAND(9,0,4294967295)$","-1"},
		{0xa170f641,0,"$RAND(9,5,4294967295)$","000-1"},
		{0xa170f641,0,"$RAND(9,0,2147483648)$","-2147483648"},
		{0xf01bf641,0x7fff0000,"$RAND(9,0,2147483648)$","2147418120"},
		{0xf01bf641,0x7fff0000,"$RAND(9,0,4294901760)$","-131066"},
	};
	for (size_t i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		struct st_rip3_random random={cases[i].seed};
		const struct st_rip3_host_variables host={.random=&random};
		CHECK(run(&host,cases[i].text,cases[i].expected)==0 && random.state==cases[i].next);
	}
	/* RAND(32766) maps each CRT draw unchanged. These independent sequence
	 * values distinguish the generator from a host libc or a stub provider. */
	static const char *const sequence[]={"41","18467","6334","26500","19169"};
	struct st_rip3_random first={1},second={1};
	const struct st_rip3_host_variables a={.random=&first}, b={.random=&second};
	for (size_t i=0; i<sizeof(sequence)/sizeof(sequence[0]); i++) {
		CHECK(run(&a,"$RAND(32766)$",sequence[i])==0);
		if (i==2) CHECK(run(&b,"$RAND(32766)$","41")==0);
	}
	CHECK(run(&b,"$RAND(32766)$","18467")==0 && first.state!=second.state);
	return 0;
}

static int
failures(void)
{
	static const char *const invalid[]={
		"$RAND$","$RAND()$","$RAND(0)$","$RAND(32768)$","$RAND(2147483648)$",
		"$RAND(4294967296)$","$RAND(9,6)$","$RAND(9,0,9)$","$RAND(9,0,10)$",
		"$RAND(9,0,2,3)$","$RAND(-9)$","$RAND( 9)$","$RAND(9 )$",
		"$RAND(9, )$","$RAND(9,0,A)$","$RAND(9,0,2.0)$","$RAND(\"9\")$",
		"$RAND(\x82)$","$RAND(\xc3\xa9)$","$RAND(000000000000000000001)$",
	};
	struct st_rip3_random random={1};
	struct st_rip3_host_variables host={.random=&random};
	struct observation seen={0};
	const struct st_rip3_host_sink sink={&seen,reply,diagnostic};
	for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
		process(&host,&sink,(const uint8_t *)invalid[i],strlen(invalid[i]));
		CHECK(seen.errors==i+1 && seen.kind==ST_RIP3_HOST_INVALID_REFERENCE
		    && seen.replies==0 && random.state==1);
	}
	CHECK(run(&host,"$RAND(32766)$","41")==0); /* Failure is not latched. */
	host.random=NULL;seen=(struct observation){0};
	process(&host,&sink,(const uint8_t *)"$RAND(9)$",9);
	CHECK(seen.errors==1 && seen.kind==ST_RIP3_HOST_UNSUPPORTED_REFERENCE && seen.replies==0);
	host.random=&random;
	struct st_rip3_builtin_reference reference;
	CHECK(st_rip3_builtin_reference_parse(&reference,(const uint8_t *)"$RAND(9,5)$",11)==0);
	char output[24],before[24];
	memset(output,'Q',sizeof(output));memcpy(before,output,sizeof(output));
	uint32_t saved=random.state;
	for (size_t capacity=0; capacity<6; capacity++) {
		CHECK(st_rip3_random_execute(&host,&reference,output,capacity)==IO_ERROR_OUT_OF_RANGE
		    && memcmp(output,before,sizeof(output))==0 && random.state==saved);
	}
	CHECK(st_rip3_random_execute(NULL,&reference,output,sizeof(output))==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_random_execute(&host,NULL,output,sizeof(output))==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_random_execute(&host,&reference,NULL,sizeof(output))==IO_ERROR_INVALID_ARGUMENT);
	reference.parameters[0].length=21;
	CHECK(st_rip3_random_execute(&host,&reference,output,sizeof(output))==IO_ERROR_INVALID_FORMAT
	    && memcmp(output,before,sizeof(output))==0 && random.state==saved);
	reference.parameters[0].length=1;
	CHECK(st_rip3_random_execute(&host,&reference,output,6)==0 && strcmp(output,"00005")==0
	    && random.state!=saved); /* Five digits plus the terminator fit exactly. */
	return 0;
}

int
main(void)
{
	CHECK(vectors()==0 && failures()==0);
	return 0;
}
