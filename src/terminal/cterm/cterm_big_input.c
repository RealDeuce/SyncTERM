/* BIG's BIOS readiness checkpoint and byte-sized local getch adaptation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "cterm.h"
#include "st_cterm.h"
#include "io_error.h"

bool cterm_big_input_owned(const struct cterm_big *big)
{
	return big!=NULL && big->numeric.loop.active==1
	    && st_graphics_parser_in_command(big->parser)
	    && (!big->footer_active || big->numeric.loop.command_read);
}

bool cterm_big_handle_key(struct cterm_big *big, const struct cterm_key *key)
{
	if (big==NULL || big->input.replaying
	    || (!cterm_big_input_owned(big) && big->input.count==0)) return false;
	struct cterm_big_input *input=&big->input;
	if (input->count==CTERM_BIG_INPUT_KEYS) {
		/* Keep admitted events ordered and local even under a stalled
		 * consumer. Reject the newest event rather than leaking it out. */
		bool warn=!input->overflow; input->overflow=true;
		if (warn) cterm_log(big->terminal,ST_CTERM_LOG_WARNING,
		    "BIG local key queue full; discarding new key");
		return true;
	}
	input->keys[(input->first+input->count)%CTERM_BIG_INPUT_KEYS]=*key;
	++input->count;
	return true;
}

int cterm_big_input_peek(void *context, bool *ready)
{
	struct cterm_big *big=context;
	/* A scan byte left by getch is not another BIOS key. It is read only
	 * after a later event satisfies this checkpoint's readiness probe. */
	*ready=big->input.count!=0;
	return 0;
}

static struct cterm_key take(struct cterm_big_input *input)
{
	struct cterm_key key=input->keys[input->first];
	input->first=(input->first+1)%CTERM_BIG_INPUT_KEYS;
	--input->count; input->overflow=false;
	return key;
}

int cterm_big_input_read(void *context, uint8_t *value)
{
	struct cterm_big *big=context; struct cterm_big_input *input=&big->input;
	if (input->scan_pending) {
		*value=input->scan; input->scan_pending=false;
		return 0;
	}
	if (input->count==0) return IO_ERROR_IN_PROGRESS;
	struct cterm_key key=take(input);
	if (key.kind==CTERM_KEY_TEXT) {
		/* BIG's local console is CP437 even on a UTF-8 wire. Unmapped
		 * Unicode is a non-cancelling key, never a truncated control byte. */
		if (key.value<0x80) *value=(uint8_t)key.value;
		else if (!st_codepage_from_unicode(value,ST_CODEPAGE_CP437,key.value)) *value='?';
	} else if (key.local>0xff && key.local<=0xff00 && (key.local&0xff)==0) {
		*value=0; input->scan=(uint8_t)((unsigned)key.local>>8); input->scan_pending=true;
	} else *value=key.local>=0 && key.local<=0xff?(uint8_t)key.local:'?';
	return 0;
}

int cterm_big_input_forward(struct cterm_big *big)
{
	if (cterm_big_input_owned(big) || big->footer_active) return 0;
	struct cterm_big_input *input=&big->input;
	while (input->count!=0) {
		struct cterm_key key=take(input); input->replaying=true;
		int status=cterm_key_replay(big->terminal,&key);
		input->replaying=false;
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		if (status!=0) return status;
	}
	return 0;
}
