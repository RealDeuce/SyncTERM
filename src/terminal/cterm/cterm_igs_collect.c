/* < local input acquisition and deferred serial output. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_igs.h"
#include "cterm_igs_collect.h"
#include "cterm.h"
#include "cterm_vt52.h"
#include "st_console.h"
#include "io_error.h"
#include <string.h>

static int echo(struct cterm_igs *igs, const uint8_t *bytes, size_t size)
{
	int status=cterm_vt52_output(igs->terminal,bytes,size);
	return igs->reset_pending ? IO_ERROR_INTERRUPTED : status;
}
static int attempt(struct cterm_igs *igs)
{
	struct cterm_igs_collect *c=&igs->collect;
	c->release=false;
	int status=cterm_igs_pointer_begin(igs,&c->pointer,c->type);
	if (status==0) status=cterm_igs_collect_mouse(igs,igs->input.mouse_x,igs->input.mouse_y,
	    igs->input.suppressed?0:igs->input.buttons);
	return status;
}
int cterm_igs_collect_begin(struct cterm_igs *igs, int type, int options)
{
	struct cterm_igs_collect *c=&igs->collect;
	c->configuring=false; c->count=0; c->pending=options!=2 && options!=3;
	c->type=type>10?2:type; c->options=options==3?0:options;
	c->active=true; c->ready=c->release=false; c->error=0;
	int status=c->type>1?attempt(igs):0;
	if (status==0) status=cterm_igs_collect_resume(igs);
	if (status!=0) { (void)cterm_igs_collect_cancel(igs); return status; }
	return c->ready?0:IO_ERROR_IN_PROGRESS;
}
static int keyboard(struct cterm_igs *igs, int key)
{
	struct cterm_igs_collect *c=&igs->collect;
	/* Correct native Delete underflow instead of indexing surrounding RAM. */
	if ((key==127 || key==8) && c->count!=0) --c->count;
	int status=0;
	if (key>31 && key<127 && c->count<sizeof(c->bytes)) {
		c->bytes[c->count++]=(uint8_t)key;
		if (c->options!=0) { uint8_t byte=(uint8_t)key; status=echo(igs,&byte,1); }
	}
	if (status==0 && ((c->options!=0 && key==127) || key==8)) {
		const uint8_t erase[]={27,'D',' ',27,'D'};
		status=echo(igs,erase,sizeof(erase));
	}
	if (key==13 || c->type<=0) c->ready=true;
	return status;
}
int cterm_igs_collect_mouse(struct cterm_igs *igs, int x, int y, unsigned buttons)
{
	struct cterm_igs_collect *c=&igs->collect;
	if (!c->active || c->ready || c->type<=1) return 0;
	int status=0;
	if (c->release) {
		if (c->pointer.gem) status=cterm_igs_pointer_move(igs,&c->pointer,x,y);
		if (buttons==0) c->ready=true;
	} else {
		status=cterm_igs_pointer_move(igs,&c->pointer,x,y);
		if (buttons!=0) { c->press_x=x; c->press_y=y; c->release=true; }
	}
	if (status!=0) { c->error=status; c->ready=true; }
	return status;
}
int cterm_igs_collect_resume(struct cterm_igs *igs)
{
	struct cterm_igs_collect *c=&igs->collect;
	if (!c->active) return 0;
	int status=0;
	if (c->type<=1) {
		while (!c->ready && igs->input.key_count!=0 && status==0) {
			struct cterm_igs_key key=igs->input.keys[igs->input.key_first];
			(void)cterm_igs_take_key(igs);
			/* Extended BIOS words are compared intact, never narrowed to an
			 * ASCII byte. Non-BMP host text has no native keyboard word. */
			int value=key.kind==CTERM_IGS_KEY_TEXT && key.value>UINT16_MAX?0:(int)(int16_t)key.local;
			/* The host's extended Delete identifier has the native ASCII
			 * result127 (also shifted), or31 under Control. Other extended
			 * key words still retain their full-word comparison above. */
			if (key.kind==CTERM_IGS_KEY_NORMAL && key.value==ST_CONSOLE_KEY_DELETE
			    && (key.modifiers&(ST_CONSOLE_MODIFIER_ALT|ST_CONSOLE_MODIFIER_GUI))==0)
				value=(key.modifiers&ST_CONSOLE_MODIFIER_CONTROL)!=0?31:127;
			status=keyboard(igs,value);
		}
	} else if (c->ready) {
		status=cterm_igs_pointer_end(igs,&c->pointer);
		if (status==0) status=c->error;
		if (status==0) {
			size_t zone;
			for (zone=0;zone<CTERM_IGS_ZONES;++zone) {
				const int16_t *r=igs->input.zones[zone].rectangle;
				if (c->press_x>=r[0] && c->press_x<=r[2] && c->press_y>=r[1] && c->press_y<=r[3]) break;
			}
			if (zone==CTERM_IGS_ZONES && igs->input.zone_loopback!=0) {
				const uint8_t bell=7; status=echo(igs,&bell,1);
				c->ready=false;
				if (status==0) status=attempt(igs);
			} else {
				if (zone==CTERM_IGS_ZONES) zone=CTERM_IGS_ZONES-1;
				const struct cterm_igs_zone *z=&igs->input.zones[zone];
				/* X4 produces at most80 bytes. Reject corrupt lengths rather
				 * than copying neighboring objects from the native address map. */
				if (z->length>sizeof(z->bytes)) status=IO_ERROR_OUT_OF_RANGE;
				else { c->count=z->length; memcpy(c->bytes,z->bytes,c->count); }
			}
		}
	}
	if (status==0 && c->ready) c->active=false;
	return status;
}
int cterm_igs_collect_send(struct cterm_igs *igs)
{
	struct cterm_igs_collect *c=&igs->collect;
	if (!c->pending || c->active) return 0;
	/* Clear before any transport callback. The current buffer/count/CR are
	 * reread after each send; no snapshot or old-UART readiness loop. */
	c->pending=false;
	bool sending=igs->input.sending; igs->input.sending=true;
	int status=0;
	for (size_t i=0;i<c->count;++i) {
		if (i>=sizeof(c->bytes)) { status=IO_ERROR_OUT_OF_RANGE; break; }
		char byte=(char)c->bytes[i];
		if (igs->terminal->keystroke_cb!=NULL)
			igs->terminal->keystroke_cb(&byte,1,igs->terminal->keystroke_cbdata);
		if (igs->reset_pending) { status=IO_ERROR_INTERRUPTED; break; }
		if (igs->script.active) {
			/* File dispatch rereads the byte after serial output. */
			status=echo(igs,&c->bytes[i],1);
			if (status!=0) break;
		}
	}
	if (status==0 && c->append_cr!=0) {
		if (igs->terminal->keystroke_cb!=NULL)
			igs->terminal->keystroke_cb("\r",1,igs->terminal->keystroke_cbdata);
		if (igs->script.active && !igs->reset_pending) {
			const uint8_t cr=13; status=echo(igs,&cr,1);
		}
	}
	igs->input.sending=sending;
	return igs->reset_pending?IO_ERROR_INTERRUPTED:status;
}
int cterm_igs_collect_cancel(struct cterm_igs *igs)
{
	int status=cterm_igs_pointer_end(igs,&igs->collect.pointer);
	igs->collect.configuring=igs->collect.active=igs->collect.ready=igs->collect.pending=igs->collect.release=false;
	return status;
}
