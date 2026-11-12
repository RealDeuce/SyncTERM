/* BIG original counted output and command-return ordering; reference21/32/33/43. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_control.h"
#include "st_big_number.h"
#include "io_error.h"
#include <string.h>
enum { FLOW_OFF, FLOW_CHECK, FLOW_READY, FLOW_SEND, FLOW_DONE, FLOW_FAILED };
enum { FOOTER_OFF, FOOTER_REPEAT, FOOTER_FLOW, FOOTER_RESET, FOOTER_REPLY,
	FOOTER_CHECK, FOOTER_READY, FOOTER_SEND, FOOTER_CR, FOOTER_CR_SEND,
	FOOTER_DONE, FOOTER_FAILED };
int32_t st_big_zone_id(uint16_t word)
{
	int32_t id=st_big_number_signed(word);
	return id>47 && id!=9999 && id!=9998 && id!=9997?47:id;
}
void st_big_control_enter(struct st_big_control *c)
{
	if (c == NULL) return;
	/* Safe port: zero formerly uninitialized gang suffix bytes on entry.
	 * A later gang copy keeps its existing suffix within the same frame. */
	memset(&c->frame,0,sizeof(c->frame)); c->frame.gang_marker='X';
}
void st_big_control_init(struct st_big_control *c)
{
	if (c == NULL) return;
	memset(c,0,sizeof(*c)); c->flow.mode=1;
	c->flow.count[0]=c->flow.count[1]=1; c->flow.byte[0]=17; c->flow.byte[1]=19;
	c->pointer.step_x=12; c->pointer.step_y=6;
	st_big_control_enter(c);
}
void st_big_flow_begin(struct st_big_flow_reader *r, const struct st_big_control *c, bool on)
{
	if (r == NULL || c == NULL) return;
	*r=(struct st_big_flow_reader){.phase=FLOW_CHECK,.which=on?0u:1u,.enabled=c->flow.mode!=0};
}
static int flow_error(struct st_big_flow_reader *r, int status)
{
	if (status != IO_ERROR_IN_PROGRESS) { r->phase=FLOW_FAILED; r->error=status; }
	return status;
}
int st_big_flow_poll(struct st_big_flow_reader *r, struct st_big_control *c,
    const struct st_big_control_sink *sink, size_t *budget)
{
	if (r == NULL || c == NULL || sink == NULL || sink->send == NULL || budget == NULL
	    || r->phase == FLOW_OFF || r->which > 1) return IO_ERROR_INVALID_ARGUMENT;
	enum st_big_output_kind kind=r->which==0?ST_BIG_OUTPUT_XON:ST_BIG_OUTPUT_XOFF;
	for (;;) switch (r->phase) {
	case FLOW_CHECK:
		if (!r->enabled || st_big_number_signed(r->index)>=st_big_number_signed(c->flow.count[r->which])) {
			r->phase=FLOW_DONE; continue;
		}
		r->phase=FLOW_READY;
		continue;
	case FLOW_READY: {
		if (*budget==0) return IO_ERROR_IN_PROGRESS;
		int status=sink->ready==NULL?0:sink->ready(sink->context,kind);
		if (status!=0) return flow_error(r,status);
		r->byte=c->flow.byte[r->which]; r->phase=FLOW_SEND;
		continue;
	}
	case FLOW_SEND: {
		if (*budget==0) return IO_ERROR_IN_PROGRESS;
		int status=sink->send(sink->context,kind,r->byte);
		if (status!=0) return flow_error(r,status);
		--*budget; r->index=(uint16_t)(r->index+1u); r->phase=FLOW_CHECK;
		continue;
	}
	case FLOW_DONE: return 0;
	case FLOW_FAILED: return r->error;
	default: return IO_ERROR_INVALID_ARGUMENT;
	}
}
void st_big_footer_begin(struct st_big_footer *r, bool chained)
{
	if (r == NULL) return;
	*r=(struct st_big_footer){.phase=FOOTER_REPEAT,.chained=chained};
}
static int footer_error(struct st_big_footer *r, int status)
{
	if (status!=IO_ERROR_IN_PROGRESS) { r->phase=FOOTER_FAILED; r->error=status; }
	return status;
}
static int complete(struct st_big_footer *r, enum st_big_footer_action action,
    enum st_big_footer_action *result)
{
	r->action=action; r->phase=FOOTER_DONE; *result=action; return 0;
}
int st_big_footer_poll(struct st_big_footer *r, struct st_big_loop *loop,
    struct st_big_control *c, const struct st_big_control_sink *sink, size_t *budget,
    enum st_big_footer_action *action)
{
	if (r==NULL || loop==NULL || c==NULL || sink==NULL || sink->send==NULL
	    || budget==NULL || action==NULL || r->phase==FOOTER_OFF) return IO_ERROR_INVALID_ARGUMENT;
	for (;;) switch (r->phase) {
	case FOOTER_REPEAT:
		if (loop->active==1 && loop->command_read) return complete(r,ST_BIG_FOOTER_REPEAT,action);
		if (c->frame.did_loop==1) {
			c->frame.did_loop=0; st_big_flow_begin(&r->flow,c,true); r->phase=FOOTER_FLOW;
		} else r->phase=FOOTER_RESET;
		continue;
	case FOOTER_FLOW: {
		int status=st_big_flow_poll(&r->flow,c,sink,budget);
		if (status!=0) return footer_error(r,status);
		r->phase=FOOTER_RESET; continue;
	}
	case FOOTER_RESET:
		loop->type=' '; c->frame.gang_marker='X';
		if (r->chained) return complete(r,ST_BIG_FOOTER_CHAIN,action);
		r->phase=FOOTER_REPLY; continue;
	case FOOTER_REPLY:
		if (c->input.pending==0) { c->frame.command=0; return complete(r,ST_BIG_FOOTER_RETURN,action); }
		c->input.pending=0; r->index=1; r->phase=FOOTER_CHECK;
		continue;
	case FOOTER_CHECK:
		if (st_big_number_signed((uint16_t)(c->input.count+1u))<=st_big_number_signed(r->index)) {
			r->phase=FOOTER_CR; continue;
		}
		r->phase=FOOTER_READY;
		continue;
	case FOOTER_READY: {
		if (*budget==0) return IO_ERROR_IN_PROGRESS;
		int status=sink->ready==NULL?0:sink->ready(sink->context,ST_BIG_OUTPUT_REPLY);
		if (status!=0) return footer_error(r,status);
		/* Preserve the completed prefix, but do not follow escaped DOS
		 * addresses beyond the bounded normal/byte-counted reply buffer. */
		if (r->index>=ST_BIG_REPLY_CAPACITY) return footer_error(r,IO_ERROR_OUT_OF_RANGE);
		r->byte=c->input.payload[r->index]; r->phase=FOOTER_SEND;
		continue;
	}
	case FOOTER_SEND: {
		if (*budget==0) return IO_ERROR_IN_PROGRESS;
		int status=sink->send(sink->context,ST_BIG_OUTPUT_REPLY,r->byte);
		if (status!=0) return footer_error(r,status);
		--*budget; r->index=(uint16_t)(r->index+1u); r->phase=FOOTER_CHECK; continue;
	}
	case FOOTER_CR:
		if (c->input.append_cr==0) { c->frame.command=0; return complete(r,ST_BIG_FOOTER_RETURN,action); }
		r->phase=FOOTER_CR_SEND;
		continue;
	case FOOTER_CR_SEND: {
		if (*budget==0) return IO_ERROR_IN_PROGRESS;
		int status=sink->send(sink->context,ST_BIG_OUTPUT_REPLY,13);
		if (status!=0) return footer_error(r,status);
		--*budget; c->frame.command=0; return complete(r,ST_BIG_FOOTER_RETURN,action);
	}
	case FOOTER_DONE: *action=r->action; return 0;
	case FOOTER_FAILED: return r->error;
	default: return IO_ERROR_INVALID_ARGUMENT;
	}
}
