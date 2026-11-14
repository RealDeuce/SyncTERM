/* BIG ? separate query requests and current caches, references31/63. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "io_error.h"

int cterm_big_query_begin(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	big->query=(struct cterm_big_query){.phase=CTERM_BIG_QUERY_READY,
	    .selector=(uint16_t)view->arguments[0].value,.chained=view->chained};
	return IO_ERROR_IN_PROGRESS;
}
static void request(struct cterm_big_query *q, uint8_t byte, enum cterm_big_query_phase next)
{
	q->byte=byte; q->next=next; q->phase=CTERM_BIG_QUERY_SEND;
}
int cterm_big_query_poll(struct cterm_big *big, size_t *output_budget, bool *complete)
{
	struct cterm_big_query *q=&big->query; *complete=false;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		switch (q->phase) {
		case CTERM_BIG_QUERY_READY: {
			int status=big->control_io.ready==NULL?0:big->control_io.ready(big->control_io.context,ST_BIG_OUTPUT_QUERY);
			if (status!=0) return status;
			q->phase=q->selector==0?CTERM_BIG_QUERY_VERSION:CTERM_BIG_QUERY_RESOLUTION; break;
		}
		case CTERM_BIG_QUERY_VERSION:
			if (q->index<4) {
				static const uint8_t version[]={'1','.','0','3'};
				request(q,version[q->index++],CTERM_BIG_QUERY_VERSION);
			} else q->phase=CTERM_BIG_QUERY_REGISTERED;
			break;
		case CTERM_BIG_QUERY_REGISTERED:
			/* This comparison follows all four returning byte requests. */
			if (big->feature_gates[3][0]==big->feature_gates[3][1]) request(q,'R',CTERM_BIG_QUERY_RESOLUTION);
			else q->phase=CTERM_BIG_QUERY_RESOLUTION;
			break;
		case CTERM_BIG_QUERY_RESOLUTION:
			if (q->selector==1) request(q,(uint8_t)(big->numeric.context.resolution+0x30u),CTERM_BIG_QUERY_RESOLUTION_COLON);
			else q->phase=CTERM_BIG_QUERY_MEMORY;
			break;
		case CTERM_BIG_QUERY_RESOLUTION_COLON:
			request(q,':',CTERM_BIG_QUERY_MEMORY); break;
		case CTERM_BIG_QUERY_MEMORY:
			if (q->selector==2) request(q,(uint8_t)(big->music_memory+0x30u),CTERM_BIG_QUERY_MEMORY_COLON);
			else q->phase=CTERM_BIG_QUERY_DONE;
			break;
		case CTERM_BIG_QUERY_MEMORY_COLON:
			request(q,':',CTERM_BIG_QUERY_DONE); break;
		case CTERM_BIG_QUERY_SEND: {
			if (*output_budget==0) return IO_ERROR_IN_PROGRESS;
			if (big->control_io.send==NULL) return IO_ERROR_PROTOCOL;
			int status=big->control_io.send(big->control_io.context,ST_BIG_OUTPUT_QUERY,q->byte);
			if (big->reset_pending) return IO_ERROR_INTERRUPTED;
			if (status==IO_ERROR_IN_PROGRESS) return status;
			/* The original ignores returning transmit results. A completed
			 * request consumes work even when it did not deliver its byte. */
			--*output_budget; q->phase=q->next; break;
		}
		case CTERM_BIG_QUERY_DONE:
			q->phase=CTERM_BIG_QUERY_OFF; *complete=true; return 0;
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
