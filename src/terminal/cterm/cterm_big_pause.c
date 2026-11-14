/* BIG t: local key drain, shared class0 acquisition and a local pause. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "st_time.h"
#include "io_error.h"

void cterm_big_pause_begin(struct cterm_big *big, bool chained)
{
	big->pause=(struct cterm_big_pause){.phase=CTERM_BIG_PAUSE_DRAIN,.chained=chained};
}
static int sample(struct cterm_big *big, uint64_t *value)
{
	struct cterm_big_pause *p=&big->pause;
	int status=big->numeric.io.now_ns==NULL?st_time_monotonic_ns(value)
	    :big->numeric.io.now_ns(big->numeric.io.context,value);
	if (status==IO_ERROR_IN_PROGRESS) return IO_ERROR_PROTOCOL;
	if (status!=0) return status;
	if (p->sampled && *value<p->sample) return IO_ERROR_OUT_OF_RANGE;
	p->sample=*value; p->sampled=true; return 0;
}
static int peek(struct cterm_big *big, bool *ready)
{
	*ready=false;
	int status=big->numeric.io.peek_key==NULL?0:big->numeric.io.peek_key(big->numeric.io.context,ready);
	return status==IO_ERROR_IN_PROGRESS?IO_ERROR_PROTOCOL:status;
}
int cterm_big_pause_poll(struct cterm_big *big, const uint8_t *bytes, size_t size,
    size_t *used, size_t *work_budget, bool *complete)
{
	static const struct st_graphics_parameter scalar={.kind=ST_GRAPHICS_NUMBER};
	static const struct st_graphics_command_view view={.code='t'};
	struct cterm_big_pause *p=&big->pause;
	*used=0; *complete=false;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		int status; bool ready=false;
		switch (p->phase) {
		case CTERM_BIG_PAUSE_DRAIN:
			if (*work_budget==0) return IO_ERROR_IN_PROGRESS;
			status=peek(big,&ready); if (status!=0) return status;
			p->phase=ready?CTERM_BIG_PAUSE_DRAIN_READ:CTERM_BIG_PAUSE_NUMBER;
			break;
		case CTERM_BIG_PAUSE_DRAIN_READ:
		case CTERM_BIG_PAUSE_READ: {
			uint8_t ignored;
			if (p->phase==CTERM_BIG_PAUSE_DRAIN_READ && *work_budget==0) return IO_ERROR_IN_PROGRESS;
			if (big->numeric.io.read_key==NULL) return IO_ERROR_PROTOCOL;
			status=big->numeric.io.read_key(big->numeric.io.context,&ignored);
			if (status!=0) return status;
			if (p->phase==CTERM_BIG_PAUSE_READ) {
				p->phase=CTERM_BIG_PAUSE_OFF; break;
			}
			--*work_budget; p->phase=CTERM_BIG_PAUSE_DRAIN; break;
		}
		case CTERM_BIG_PAUSE_NUMBER: {
			int value=0; bool done=false;
			if (!p->number_started) {
				status=st_big_numeric_field(&big->numeric,&view,&scalar);
				if (status!=0) return status;
				p->number_started=true;
			}
			if (!p->wire) {
				status=st_big_numeric_number(&big->numeric,&view,&scalar,&value,&done);
				if (status!=0) return status;
				if (!done) p->wire=true;
			}
			if (p->wire) {
				status=st_big_numeric_read_number(&big->numeric,&view,&scalar,bytes,size,used,&value,&done);
				if (status!=0 || !done) return status;
			}
			if (big->reset_pending) return IO_ERROR_INTERRUPTED;
			int32_t seconds=st_big_number_signed((uint16_t)value);
			p->seconds=(uint16_t)(seconds<1?1:seconds>30?30:seconds);
			p->phase=CTERM_BIG_PAUSE_START; break;
		}
		case CTERM_BIG_PAUSE_START: {
			uint64_t current;
			status=sample(big,&current); if (status!=0) return status;
			uint64_t duration=(uint64_t)p->seconds*UINT64_C(1000000000);
			p->deadline=UINT64_MAX-current<duration?UINT64_MAX:current+duration;
			p->phase=CTERM_BIG_PAUSE_TIME; break;
		}
		case CTERM_BIG_PAUSE_TIME: {
			uint64_t current;
			status=sample(big,&current); if (status!=0) return status;
			p->expired=current>=p->deadline; p->phase=CTERM_BIG_PAUSE_KEY; break;
		}
		case CTERM_BIG_PAUSE_KEY:
			/* Expiry still consumes one ready getch byte. A private scan
			 * alone is not readiness; no post-exit drain or retransmit. */
			status=peek(big,&ready); if (status!=0) return status;
			if (ready) p->phase=CTERM_BIG_PAUSE_READ;
			else if (p->expired) p->phase=CTERM_BIG_PAUSE_OFF;
			else { p->phase=CTERM_BIG_PAUSE_TIME; return IO_ERROR_IN_PROGRESS; }
			break;
		case CTERM_BIG_PAUSE_OFF:
			*complete=true; return 0;
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
