/* X8 protocol waits around the original palette operators, BIG26/55. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "st_big_number.h"
#include "st_time.h"
#include "io_error.h"

enum { ROTATE_ENTRY, ROTATE_OFF, ROTATE_COUNTER, ROTATE_TURN,
	ROTATE_DELAY_BEGIN, ROTATE_DELAY, ROTATE_STATUS, ROTATE_READ,
	ROTATE_EXIT, ROTATE_ON, ROTATE_RESTORE };

int cterm_big_rotation_begin(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=5) return IO_ERROR_PROTOCOL;
	for (size_t i=0;i<5;++i) if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
	struct cterm_big_rotation *r=&big->rotation;
	int status=st_big_palette_rotation_begin(&r->colors,&big->state,
	    (uint16_t)view->arguments[1].value,(uint16_t)view->arguments[2].value,
	    (uint16_t)view->arguments[3].value,(uint16_t)view->arguments[4].value);
	if (status!=0) return status;
	r->active=true; r->chained=view->chained; r->phase=ROTATE_ENTRY; r->sampled=false;
	return IO_ERROR_IN_PROGRESS;
}
static int sample(struct cterm_big *big, uint64_t *value)
{
	struct cterm_big_rotation *r=&big->rotation;
	int status=big->numeric.io.now_ns==NULL?st_time_monotonic_ns(value):big->numeric.io.now_ns(big->numeric.io.context,value);
	if (status==IO_ERROR_IN_PROGRESS) return IO_ERROR_PROTOCOL;
	if (status!=0) return status;
	if (r->sampled && *value<r->sample) return IO_ERROR_OUT_OF_RANGE;
	r->sample=*value; r->sampled=true; return 0;
}
int cterm_big_rotation_poll(struct cterm_big *big, size_t *output_budget, size_t *body_budget,
	const struct st_big_state_sink *sink, bool *complete)
{
	struct cterm_big_rotation *r=&big->rotation; *complete=false;
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		int status; uint64_t current;
		switch (r->phase) {
		case ROTATE_ENTRY:
			if (r->colors.count==0 || r->colors.first==r->colors.last) r->phase=ROTATE_RESTORE;
			else if (big->numeric.loop.active==0) {
				st_big_flow_begin(&r->flow,&big->control,false); r->phase=ROTATE_OFF;
			} else r->phase=ROTATE_COUNTER;
			break;
		case ROTATE_OFF:
			status=st_big_flow_poll(&r->flow,&big->control,&big->control_io,output_budget);
			if (status!=0) return status;
			r->phase=ROTATE_COUNTER; break;
		case ROTATE_COUNTER:
			r->counter=0; r->phase=ROTATE_TURN; break;
		case ROTATE_TURN:
			if (st_big_number_signed(r->counter)>st_big_number_signed(r->colors.count)) { r->phase=ROTATE_EXIT; break; }
			/* Fairness is between complete turns at the protocol checkpoint,
			 * never inside a palette walk. Count32767 has no finite end. */
			if (*body_budget==0) return IO_ERROR_IN_PROGRESS;
			--*body_budget;
			status=st_big_palette_rotation_turn(&r->colors,&big->state,sink);
			if (status!=0) return status;
			r->phase=ROTATE_DELAY_BEGIN; break;
		case ROTATE_DELAY_BEGIN:
			status=sample(big,&current); if (status!=0) return status;
			/* Native local-domain policy: unsigned delay WORD milliseconds.
			 * No foreign PIT programming or timer/audio clock ownership. */
			{
				uint64_t duration=(uint64_t)r->colors.delay*UINT64_C(1000000);
				r->deadline=UINT64_MAX-current<duration?UINT64_MAX:current+duration;
			}
			r->phase=ROTATE_DELAY; break;
		case ROTATE_DELAY:
			status=sample(big,&current); if (status!=0) return status;
			if (current<r->deadline) return IO_ERROR_IN_PROGRESS;
			r->phase=ROTATE_STATUS; break;
		case ROTATE_STATUS:
			if (cterm_big_input_status(big)!=0) r->phase=ROTATE_READ;
			else { r->counter=(uint16_t)(r->counter+1u); r->phase=ROTATE_TURN; }
			break;
		case ROTATE_READ:
			{
				uint8_t ignored;
				status=big->numeric.io.read_key(big->numeric.io.context,&ignored);
				if (status!=0) return status;
			}
			r->counter=r->colors.count; r->phase=ROTATE_EXIT; break;
		case ROTATE_EXIT:
			if (big->numeric.loop.active==0) {
				st_big_flow_begin(&r->flow,&big->control,true); r->phase=ROTATE_ON;
			} else r->phase=ROTATE_RESTORE;
			break;
		case ROTATE_ON:
			status=st_big_flow_poll(&r->flow,&big->control,&big->control_io,output_budget);
			if (status!=0) return status;
			r->phase=ROTATE_RESTORE; break;
		case ROTATE_RESTORE:
			if (r->colors.count==0) {
				status=st_big_palette_rotation_restore(&r->colors,&big->state,sink);
				if (status!=0) return status;
			}
			r->active=false; *complete=true; return 0;
		default: return IO_ERROR_PROTOCOL;
		}
	}
}
