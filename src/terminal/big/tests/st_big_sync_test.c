/* Independent X7/gate expectations from original BIG15/52. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_control.h"
#include "st_graphics_parser.h"
#include "io_error.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct gate_fixture { unsigned entries, fields, commands, unknown, text; int gate; };
static int entry(void *context, const struct st_graphics_command_view *view)
{
	struct gate_fixture *f=context; ++f->entries;
	CHECK(view->count==0 && (view->code=='X' || view->code=='@')); return f->gate;
}
static int field(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct gate_fixture *f=context; (void)view; (void)parameter;
	CHECK(f->gate==0); ++f->fields; return 0;
}
static int command(void *context, const struct st_graphics_command_view *view)
{
	struct gate_fixture *f=context; CHECK(view->count==1 && f->gate==0);
	CHECK(view->arguments[0].operand.value==7); ++f->commands; return 0;
}
static void unknown(void *context, uint8_t code)
{
	struct gate_fixture *f=context; CHECK(code=='@' && f->gate==0); ++f->unknown;
}
static int text(void *context, const uint8_t *bytes, size_t size)
{
	struct gate_fixture *f=context; (void)bytes; f->text+=(unsigned)size; return 0;
}
static void parser_gate(void)
{
	static const uint8_t attention[]={'I','#'};
	static const struct st_graphics_dialect dialect={.attention=attention,.attention_size=2,
	    .chain_indicator=true,.chain='>',.number_digits=4};
	static const struct st_graphics_parameter scalar[]={{.kind=ST_GRAPHICS_NUMBER}};
	static const struct st_graphics_command commands[]={{'X',scalar,1,NULL}};
	const struct st_graphics_parser_config config={.dialect=&dialect,.commands=commands,
	    .command_count=1,.max_arguments=1,.max_payload=1};
	struct gate_fixture f={.gate=IO_ERROR_IN_PROGRESS};
	const struct st_graphics_parser_sink sink={.context=&f,.text=text,.command=command,
	    .entry=entry,.field=field,.unknown=unknown};
	st_graphics_parser_t *p=NULL; size_t used;
	CHECK(st_graphics_parser_create(&p,&config)==0);
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"I#X 7:",6,&used)==IO_ERROR_IN_PROGRESS && used==4);
	CHECK(f.entries==1 && f.fields==0 && f.commands==0);
	CHECK(st_graphics_parser_feed(p,&sink,NULL,0,&used)==IO_ERROR_IN_PROGRESS && used==0);
	CHECK(st_graphics_parser_resume(p)==IO_ERROR_INVALID_ARGUMENT);
	f.gate=0;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"7:",2,&used)==0 && used==2);
	CHECK(f.entries==3 && f.fields==1 && f.commands==1);
	f.gate=IO_ERROR_IN_PROGRESS;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"I#@>X 7:",8,&used)==IO_ERROR_IN_PROGRESS && used==4 && f.unknown==0);
	f.gate=0; CHECK(st_graphics_parser_feed(p,&sink,NULL,0,&used)==0 && f.unknown==1);
	f.gate=IO_ERROR_IN_PROGRESS;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"X 7:",4,&used)==IO_ERROR_IN_PROGRESS && used==2 && f.fields==1);
	f.gate=IO_ERROR_IO;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"7:",2,&used)==IO_ERROR_IO && used==0);
	CHECK(!st_graphics_parser_in_command(p));
	/* Source-selected commands and missing-header fallback have the same
	 * entry boundary. Cancel and EOF discard a waiting gate. */
	f.gate=IO_ERROR_IN_PROGRESS;
	CHECK(st_graphics_parser_start_command(p,&sink,'X',false)==0);
	CHECK(st_graphics_parser_feed(p,&sink,NULL,0,&used)==IO_ERROR_IN_PROGRESS);
	st_graphics_parser_cancel(p); CHECK(!st_graphics_parser_in_command(p));
	CHECK(st_graphics_parser_begin(p,&sink)==0);
	CHECK(st_graphics_parser_complete_header(p,&sink,'X',false)==IO_ERROR_IN_PROGRESS);
	f.gate=0;
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"7:",2,&used)==0 && f.commands==2 && f.fields==2);
	f.gate=IO_ERROR_IN_PROGRESS;
	CHECK(st_graphics_parser_start_command(p,&sink,'X',false)==0);
	CHECK(st_graphics_parser_feed(p,&sink,NULL,0,&used)==IO_ERROR_IN_PROGRESS);
	CHECK(st_graphics_parser_finish(p,&sink)==IO_ERROR_PROTOCOL && !st_graphics_parser_in_command(p));
	CHECK(st_graphics_parser_feed(p,&sink,(const uint8_t *)"Z",1,&used)==0 && used==1 && f.text==1);
	st_graphics_parser_destroy(p);
}
int main(void)
{
	struct st_big_control c; struct st_big_sync_reader r;
	st_big_control_init(&c); CHECK(c.music.sync==0 && c.music.running==0);
	for (unsigned value=0;value<=65535;++value) {
		c.music.sync=(uint16_t)value; c.music.position_low=5; c.music.position_high=0;
		c.music.running=1; st_big_sync_begin(&r,&c);
		CHECK(c.music.scratch==5);
		CHECK(st_big_sync_poll(&r,&c)==(value?IO_ERROR_IN_PROGRESS:0));
		/* Entered waits do not retest either flag or re-snapshot LOW. */
		c.music.sync=0; c.music.running=0;
		CHECK(st_big_sync_poll(&r,&c)==(value?IO_ERROR_IN_PROGRESS:0));
		c.music.scratch=6; CHECK(st_big_sync_poll(&r,&c)==0);
		c.music.scratch=5; CHECK(st_big_sync_poll(&r,&c)==0);
	}
	c.music.sync=65535; c.music.running=255;
	for (unsigned low=0;low<=65535;++low) {
		c.music.position_low=(uint16_t)low; c.music.position_high=0;
		st_big_sync_begin(&r,&c); CHECK(st_big_sync_poll(&r,&c)==IO_ERROR_IN_PROGRESS);
		c.music.position_low=(uint16_t)(low+1); CHECK(st_big_sync_poll(&r,&c)==0);
		c.music.position_low=(uint16_t)low; c.music.position_high=1;
		st_big_sync_begin(&r,&c); CHECK(st_big_sync_poll(&r,&c)==0);
	}
	c.music.running=0; c.music.position_high=0;
	st_big_sync_begin(&r,&c); c.music.running=1;
	CHECK(st_big_sync_poll(&r,&c)==0); /* Skipped gates stay skipped. */
	c.music.scratch=91; st_big_control_enter(&c);
	CHECK(c.music.sync==65535 && c.music.scratch==91 && c.music.running==1);
	CHECK(st_big_sync_poll(NULL,&c)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_big_sync_poll(&r,NULL)==IO_ERROR_INVALID_ARGUMENT);
	parser_gate();
	return 0;
}
