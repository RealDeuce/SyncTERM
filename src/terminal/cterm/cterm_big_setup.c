/* Initial & setup; selected/repeated & skips this operator (21/65/73). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "io_error.h"
#include <string.h>

static const struct st_graphics_parameter scalar={.kind=ST_GRAPHICS_NUMBER};
static const struct st_graphics_command_view view={.code='&'};
void cterm_big_setup_begin(struct cterm_big *big)
{
	memset(&big->setup,0,sizeof(big->setup));
	big->setup.phase=CTERM_BIG_SETUP_HEADER;
	big->numeric.loop.active=0; st_big_numeric_cancel(&big->numeric);
}
static int number(struct cterm_big *big, const uint8_t *bytes, size_t size,
    size_t *used, uint16_t *word, bool *complete)
{
	struct cterm_big_setup *s=&big->setup; int value=0, status=0;
	*used=0; *complete=false;
	if (!s->number_started) {
		status=st_big_numeric_field(&big->numeric,&view,&scalar);
		if (status!=0) return status;
		s->number_started=true; s->number_wire=false;
	}
	if (!s->number_wire) {
		status=st_big_numeric_number(&big->numeric,&view,&scalar,&value,complete);
		if (status!=0) return status;
		if (!*complete) s->number_wire=true;
	}
	if (s->number_wire) {
		status=st_big_numeric_read_number(&big->numeric,&view,&scalar,bytes,size,used,&value,complete);
		if (status!=0 || !*complete) return status;
	}
	*word=(uint16_t)value; s->number_started=false;
	return 0;
}
static int copy_gang(struct cterm_big *big)
{
	const uint8_t *end=memchr(big->text.bytes,0,sizeof(big->text.bytes));
	if (end==NULL) return IO_ERROR_OUT_OF_RANGE;
	/* Copy through NUL and retain the old same-frame suffix. Selectors
	 * remain0..127; the extra byte holds a full128-byte string's NUL. */
	memcpy(big->control.frame.gang,big->text.bytes,(size_t)(end-big->text.bytes)+1);
	return 0;
}
int cterm_big_setup_poll(struct cterm_big *big, const uint8_t *bytes, size_t size,
    size_t *used, size_t *output_budget, size_t *work_budget, bool *complete)
{
	struct cterm_big_setup *s=&big->setup; struct st_big_loop *loop=&big->numeric.loop;
	*used=0; *complete=false; s->cooperative=false;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		uint16_t word=0; size_t accepted=0; bool done=false; int status=0;
		const uint8_t *input=*used<size?bytes+*used:NULL;
		size_t available=size-*used;
		switch (s->phase) {
		case CTERM_BIG_SETUP_HEADER:
		case CTERM_BIG_SETUP_COUNT:
		case CTERM_BIG_SETUP_WORD:
			status=number(big,input,available,&accepted,&word,&done); *used+=accepted;
			if (status!=0 || !done) return status;
			if (s->phase==CTERM_BIG_SETUP_HEADER) {
				switch (s->header++) {
				case 0: loop->current=word; break;
				case 1: loop->to=word; break;
				case 2: loop->step=word; break;
				case 3: loop->delay=word; break;
				default: return IO_ERROR_PROTOCOL;
				}
				if (s->header==4) {
					loop->step=st_big_loop_setup_step(loop->current,loop->to,loop->step);
					status=st_big_stream_aux(&s->stream,&big->numeric); s->phase=CTERM_BIG_SETUP_TARGET;
				}
			} else if (s->phase==CTERM_BIG_SETUP_COUNT) {
				int32_t count=st_big_number_signed(word);
				loop->count=(uint16_t)(count<1?1:count>ST_BIG_LOOP_CAPACITY?ST_BIG_LOOP_CAPACITY:count);
				loop->active=0; loop->origin=loop->current; loop->random_suppression=1;
				s->index=0; s->phase=CTERM_BIG_SETUP_KEY;
			} else {
				if (s->index>=ST_BIG_LOOP_CAPACITY) return IO_ERROR_OUT_OF_RANGE;
				loop->words[s->index++]=s->abort?0:word; s->phase=CTERM_BIG_SETUP_KEY;
			}
			if (status!=0) return status;
			break;
		case CTERM_BIG_SETUP_TARGET:
		case CTERM_BIG_SETUP_TYPE:
		case CTERM_BIG_SETUP_TEXT:
			status=st_big_stream_poll(&s->stream,&big->numeric,&big->text,&big->control,&big->control_io,
			    input,available,&accepted,output_budget,&done); *used+=accepted;
			if (status!=0 || !done) return status;
			if (s->phase==CTERM_BIG_SETUP_TARGET) {
				word=s->stream.byte;
				big->control.frame.command=word; big->control.frame.gang_marker=(uint8_t)word; loop->command=word;
				if (word=='>') {
					status=st_big_stream_text(&s->stream,&big->numeric,&big->text,&big->control);
					s->phase=CTERM_BIG_SETUP_TEXT;
				} else { status=st_big_stream_aux(&s->stream,&big->numeric); s->phase=CTERM_BIG_SETUP_TYPE; }
			} else if (s->phase==CTERM_BIG_SETUP_TEXT) {
				status=copy_gang(big);
				if (status==0) status=st_big_stream_aux(&s->stream,&big->numeric);
				s->phase=CTERM_BIG_SETUP_TYPE;
			} else { loop->type=s->stream.byte; s->phase=CTERM_BIG_SETUP_COUNT; }
			if (status!=0) return status;
			break;
		case CTERM_BIG_SETUP_KEY: {
			if (loop->count<1 || loop->count>ST_BIG_LOOP_CAPACITY) return IO_ERROR_OUT_OF_RANGE;
			if (s->index>=loop->count) {
				status=st_big_loop_publish(loop); if (status!=0) return status;
				big->control.frame.did_loop=1;
				st_big_flow_begin(&s->flow,&big->control,false); s->phase=CTERM_BIG_SETUP_FLOW;
				break;
			}
			if (*work_budget==0) { s->cooperative=true; return IO_ERROR_IN_PROGRESS; }
			if (!s->key_pending) {
				bool ready=false;
				status=big->numeric.io.peek_key==NULL?0:big->numeric.io.peek_key(big->numeric.io.context,&ready);
				if (status!=0) return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
				s->key_pending=ready;
			}
			if (s->key_pending) {
				uint8_t key;
				status=big->numeric.io.read_key(big->numeric.io.context,&key);
				if (status!=0) return status;
				s->key_pending=false; if (key==3 || key==24) s->abort=true;
			}
			--*work_budget;
			if (s->abort) loop->words[s->index++]=0;
			else s->phase=CTERM_BIG_SETUP_WORD;
			break;
		}
		case CTERM_BIG_SETUP_FLOW:
			status=st_big_flow_poll(&s->flow,&big->control,&big->control_io,output_budget);
			if (status!=0) return status;
			if (big->reset_pending) return IO_ERROR_INTERRUPTED;
			status=st_big_loop_scan(loop); if (status!=0) return status;
			st_big_numeric_cancel(&big->numeric); s->phase=CTERM_BIG_SETUP_OFF; *complete=true;
			return 0;
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
