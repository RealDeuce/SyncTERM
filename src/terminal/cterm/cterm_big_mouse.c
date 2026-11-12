/* Native BIG mouse observation, ownership and ordered pointer samples. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "cterm.h"
#include "cterm_render.h"

bool cterm_big_pointer_active(const struct cterm_big *big)
{
	return big!=NULL && big->collect.active && big->collect.pointer;
}
static void enqueue(struct cterm_big *big, bool allowed)
{
	if (!cterm_big_pointer_active(big) || !big->mouse.owned || big->mouse.generation!=big->collect.generation) return;
	const struct cterm_big_event event={.mouse=true,.data.sample={
	    .x=big->mouse.observed?big->mouse.x:-1,.y=big->mouse.observed?big->mouse.y:-1,.buttons=big->mouse.buttons,
	    .generation=big->mouse.generation,.allowed=allowed}};
	(void)cterm_big_input_push(big,&event);
}
void cterm_big_mouse_flush(struct cterm_big *big)
{
	if (!big->mouse.pending) return;
	big->mouse.pending=false; enqueue(big,false);
}
void cterm_big_mouse_observe(struct cterm_big *big, const struct st_console_event *event)
{
	cterm_big_mouse_flush(big);
	big->mouse.x=event->data.mouse.pixel_x; big->mouse.y=event->data.mouse.pixel_y;
	big->mouse.buttons=event->data.mouse.buttons;
	if (big->mouse.x>=0 && big->mouse.y>=0) {
		big->mouse.known=true; big->mouse.known_x=big->mouse.x; big->mouse.known_y=big->mouse.y;
	}
	big->mouse.observed=big->mouse.pending=true;
	big->mouse.owned=cterm_big_pointer_active(big);
	big->mouse.generation=big->collect.generation;
}
bool cterm_big_mouse(struct cterm_big *big, const struct st_console_event *event)
{
	if (!big->mouse.pending || big->mouse.x!=event->data.mouse.pixel_x
	    || big->mouse.y!=event->data.mouse.pixel_y || big->mouse.buttons!=event->data.mouse.buttons)
		cterm_big_mouse_observe(big,event);
	bool owned=cterm_big_pointer_active(big);
	big->mouse.pending=false;
	bool allowed=event->data.mouse.action!=ST_CONSOLE_MOUSE_WHEEL;
	enqueue(big,allowed);
	if (owned && !big->feeding) cterm_render_record_error(big->terminal,cterm_big_poll(big));
	return owned && allowed;
}
void cterm_big_mouse_sync(struct cterm_big *big, uint32_t buttons)
{
	big->mouse.buttons=buttons;
	big->mouse.pending=false;
	big->mouse.owned=cterm_big_pointer_active(big);
	big->mouse.generation=big->collect.generation;
	enqueue(big,false);
}
