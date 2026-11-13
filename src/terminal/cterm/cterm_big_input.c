/* BIG's BIOS readiness checkpoint and byte-sized local getch adaptation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "cterm.h"
#include "st_cterm.h"
#include "io_error.h"

bool cterm_big_input_owned(const struct cterm_big *big)
{
	return big!=NULL && (big->rotation.active || big->collect.active || big->setup.phase!=CTERM_BIG_SETUP_OFF
	    || (big->control.frame.command=='<' && !big->footer_active && st_graphics_parser_in_command(big->parser)) || (big->numeric.loop.active==1
	    && st_graphics_parser_in_command(big->parser)
	    && (!big->footer_active || big->numeric.loop.command_read)));
}

bool cterm_big_input_push(struct cterm_big *big, const struct cterm_big_event *event)
{
	struct cterm_big_input *input=&big->input;
	if (input->count==CTERM_BIG_INPUT_KEYS) {
		bool warn=!input->overflow; input->overflow=true;
		if (event->mouse) big->collect.mouse_lost=true;
		if (warn) cterm_log(big->terminal,ST_CTERM_LOG_WARNING,
		    "BIG local input queue full; discarding new event");
		return false;
	}
	input->events[(input->first+input->count)%CTERM_BIG_INPUT_KEYS]=*event;
	++input->count; return true;
}
bool cterm_big_handle_key(struct cterm_big *big, const struct cterm_key *key)
{
	if (big==NULL || big->input.replaying) return false;
	cterm_big_mouse_flush(big);
	if (!cterm_big_input_owned(big) && big->input.count==0) return false;
	const struct cterm_big_event event={.data.key=*key};
	(void)cterm_big_input_push(big,&event); return true;
}
bool cterm_big_input_take(struct cterm_big *big, struct cterm_big_event *event)
{
	struct cterm_big_input *input=&big->input;
	if (input->count==0) return false;
	*event=input->events[input->first]; input->first=(input->first+1)%CTERM_BIG_INPUT_KEYS;
	--input->count; input->overflow=false; return true;
}
static void discard_mouse(struct cterm_big *big)
{
	struct cterm_big_event event;
	while (big->input.count!=0 && big->input.events[big->input.first].mouse)
		(void)cterm_big_input_take(big,&event);
}
int cterm_big_input_peek(void *context, bool *ready)
{
	struct cterm_big *big=context;
	discard_mouse(big);
	/* A scan byte left by getch is not another BIOS key. It is read only
	 * after a later event satisfies this checkpoint's readiness probe. */
	*ready=big->input.count!=0;
	return 0;
}

uint8_t cterm_big_key_byte(const struct cterm_key *key, uint8_t *scan, bool *extended)
{
	uint8_t value; *extended=false;
	if (key->kind==CTERM_KEY_TEXT) {
		if (key->value<0x80) value=(uint8_t)key->value;
		else if (!st_codepage_from_unicode(&value,ST_CODEPAGE_CP437,key->value)) value='?';
	} else if (key->local>0xff && key->local<=0xff00 && (key->local&0xff)==0) {
		value=0; *scan=(uint8_t)((unsigned)key->local>>8); *extended=true;
	} else value=key->local>=0 && key->local<=0xff?(uint8_t)key->local:'?';
	return value;
}
uint16_t cterm_big_input_status(struct cterm_big *big)
{
	discard_mouse(big);
	if (big->input.count==0) return 0;
	uint8_t scan=0; bool extended;
	uint8_t value=cterm_big_key_byte(&big->input.events[big->input.first].data.key,&scan,&extended);
	/* Retain the distinction between ready-with-AX0 and a nonzero key
	 * result. A pending private getch scan is not a new ready event. */
	return extended?(uint16_t)((unsigned)scan<<8):value;
}
int cterm_big_input_read(void *context, uint8_t *value)
{
	struct cterm_big *big=context; struct cterm_big_input *input=&big->input;
	if (input->scan_pending) {
		*value=input->scan; input->scan_pending=false;
		return 0;
	}
	discard_mouse(big);
	struct cterm_big_event event;
	if (!cterm_big_input_take(big,&event)) return IO_ERROR_IN_PROGRESS;
	*value=cterm_big_key_byte(&event.data.key,&input->scan,&input->scan_pending);
	return 0;
}

int cterm_big_input_forward(struct cterm_big *big)
{
	if (cterm_big_input_owned(big) || big->footer_active || big->zone.phase!=CTERM_BIG_ZONE_OFF) return 0;
	struct cterm_big_input *input=&big->input;
	while (input->count!=0) {
		struct cterm_big_event event; (void)cterm_big_input_take(big,&event);
		if (event.mouse) continue;
		input->replaying=true;
		int status=cterm_key_replay(big->terminal,&event.data.key);
		input->replaying=false;
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		if (status!=0) return status;
	}
	return 0;
}
