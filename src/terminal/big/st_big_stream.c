/* Original BIG readers, references7/8/62/64/73; native time policy. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_stream.h"
#include "io_error.h"
#include <string.h>

void st_big_stream_cancel(struct st_big_stream_reader *r)
{
	if (r!=NULL) memset(r,0,sizeof(*r));
}
static int sample(struct st_big_stream_reader *r, struct st_big_numeric *n)
{
	uint64_t value;
	int status=n->io.now_ns(n->io.context,&value);
	if (status==IO_ERROR_IN_PROGRESS) status=IO_ERROR_PROTOCOL;
	if (status!=0) return status;
	if (r->sampled && value<r->sample) return IO_ERROR_OUT_OF_RANGE;
	r->sample=value; r->sampled=true;
	return 0;
}
static int fail(struct st_big_stream_reader *r, int status)
{
	if (status!=IO_ERROR_IN_PROGRESS) { r->phase=ST_BIG_STREAM_FAILED; r->error=status; }
	return status;
}
static int begin(struct st_big_stream_reader *r, struct st_big_numeric *n)
{
	st_big_stream_cancel(r);
	int status=sample(r,n);
	if (status!=0) return fail(r,status);
	r->deadline=UINT64_MAX-r->sample<ST_BIG_NUMERIC_TIMEOUT_NS?UINT64_MAX:r->sample+ST_BIG_NUMERIC_TIMEOUT_NS;
	r->phase=ST_BIG_STREAM_READ;
	return 0;
}
int st_big_stream_aux(struct st_big_stream_reader *r, struct st_big_numeric *n)
{
	if (r==NULL || n==NULL || !n->initialized) return IO_ERROR_INVALID_ARGUMENT;
	return begin(r,n);
}
int st_big_stream_text(struct st_big_stream_reader *r, struct st_big_numeric *n,
    struct st_big_text *text, const struct st_big_control *control)
{
	if (r==NULL || n==NULL || !n->initialized || text==NULL || control==NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	text->active=1; text->count=0;
	int status=begin(r,n);
	if (status!=0) return status;
	r->text=true; r->phase=ST_BIG_STREAM_TEXT_TEST;
	if (n->loop.active==1 && n->loop.type!='@') text->active=0;
	if (n->loop.active==1 && n->loop.type=='@') {
		n->loop.first_text=(uint16_t)(n->loop.first_text+1u);
		if (n->loop.first_text==1) {
			st_big_flow_begin(&r->flow,control,true); r->phase=ST_BIG_STREAM_FLOW;
		}
	}
	return 0;
}
int st_big_stream_poll(struct st_big_stream_reader *r, struct st_big_numeric *n,
    struct st_big_text *text, struct st_big_control *control, const struct st_big_control_sink *sink,
    const uint8_t *bytes, size_t size, size_t *accepted, size_t *budget, bool *complete)
{
	if (accepted!=NULL) *accepted=0;
	if (complete!=NULL) *complete=false;
	if (r==NULL || n==NULL || !n->initialized || accepted==NULL || complete==NULL
	    || (bytes==NULL && size!=0) || r->phase<=ST_BIG_STREAM_OFF || r->phase>ST_BIG_STREAM_FAILED
	    || (r->text && (text==NULL || control==NULL || sink==NULL || budget==NULL)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (r->phase==ST_BIG_STREAM_FAILED) return r->error;
	for (;;) {
		if (r->phase==ST_BIG_STREAM_FLOW) {
			int status=st_big_flow_poll(&r->flow,control,sink,budget);
			if (status!=0) return fail(r,status);
			r->phase=ST_BIG_STREAM_TEXT_TEST;
		}
		if (r->phase==ST_BIG_STREAM_TEXT_TEST) {
			if (n->loop.active==0 && n->loop.type=='Z') { text->active=0; n->loop.type=' '; }
			r->phase=text->active==0?ST_BIG_STREAM_DONE:ST_BIG_STREAM_READ;
		}
		if (r->phase==ST_BIG_STREAM_DONE) {
			if (r->text && !r->returned) n->loop.command_read=true;
			r->returned=true;
			*complete=true; return 0;
		}
		int status=sample(r,n);
		if (status!=0) return fail(r,status);
		bool expired=r->sample>=r->deadline, available=*accepted<size;
		if (!available && !expired) return 0;
		uint8_t byte=available?bytes[(*accepted)++]:0;
		if (!r->text) {
			r->byte=expired?' ':byte;
			if (expired && !n->context.auto_mode && n->io.bell!=NULL) n->io.bell(n->io.context);
			r->phase=ST_BIG_STREAM_DONE;
		} else {
			if (text->count>128) return fail(r,IO_ERROR_OUT_OF_RANGE);
			if (available) {
				if (text->count<128 && byte!='@' && byte!='\r' && byte!='\n')
					text->bytes[text->count++]=byte;
				if (text->count>=128 || byte=='@') {
					if (text->count==0) text->bytes[text->count++]='@';
					text->bytes[text->count]=0; text->active=0;
				}
			}
			if (expired) {
				text->active=0;
				if (n->io.bell!=NULL) n->io.bell(n->io.context);
			}
			if (text->active==0) r->phase=ST_BIG_STREAM_DONE;
		}
		if (*accepted==size && r->phase!=ST_BIG_STREAM_DONE) return 0;
	}
}
