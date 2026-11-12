/* BIG < native acquisition; keyboard/zone operators are in st_big_collect. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "cterm_text.h"
#include "io_error.h"

static int echo(void *context, uint8_t byte)
{
	struct cterm_big *big=context; struct cterminal *t=big->terminal;
	if (big->reset_pending) return IO_ERROR_INTERRUPTED;
	int status=cterm_text_boundary(t); if (status!=0) return status;
	if (byte==7) {
		if (big->numeric.io.bell!=NULL) big->numeric.io.bell(big->numeric.io.context);
	} else if (byte==8) {
		int x,y; cterm_coord_get_xy(t,CTERM_COORD_CURR,&x,&y);
		if (x>1) --x;
		cterm_render_set_position(t,x,y);
		cterm_coord_get_xy(t,CTERM_COORD_ABSTERM,&t->xpos,&t->ypos);
		t->last_column_flag&=~CTERM_LCF_SET;
	} else {
		/* Literal native console output: no synthetic ANSI/VT52 input.
		 * DOS-style immediate wrap, retaining the terminal's window/style. */
		int lcf=t->last_column_flag, extattr=t->extattr;
		t->last_column_flag=0; t->extattr|=CTERM_EXTATTR_AUTOWRAP;
		cterm_text_literal(t,byte);
		t->last_column_flag=lcf&~CTERM_LCF_SET; t->extattr=extattr;
	}
	return big->reset_pending?IO_ERROR_INTERRUPTED:cterm_render_take_error(t);
}
static int host_pointer(struct cterm_big *big, bool visible)
{
	int status=st_console_set_pointer_visible(big->terminal->screen.console,visible);
	return status==IO_ERROR_NOT_SUPPORTED?0:status;
}
static int pointer_end(struct cterm_big *big)
{
	struct cterm_big_collect *c=&big->collect; int status=0;
	if (c->shown) { c->shown=false; status=cterm_big_pmarker(big,c->old_x,c->old_y); }
	if (c->pointer) big->state.bgi.write_mode=big->state.application.write_mode;
	c->pointer=false; c->armed=false;
	if (c->host_saved) {
		c->host_saved=false; int restored=host_pointer(big,c->host_visible);
		if (status==0) status=restored;
	}
	return status;
}
static int redraw(struct cterm_big *big)
{
	struct cterm_big_collect *c=&big->collect;
	if (c->x==c->old_x && c->y==c->old_y) return 0;
	int status=0;
	if (c->shown) { c->shown=false; status=cterm_big_pmarker(big,c->old_x,c->old_y); }
	if (status!=0) return status;
	c->old_x=c->x; c->old_y=c->y;
	status=cterm_big_pmarker(big,c->x,c->y);
	if (status==0) c->shown=true;
	return status;
}
static bool mouse_inside(const struct cterm_big *big, int32_t x, int32_t y)
{
	unsigned ch=big->state.bgi.height==200?8:big->state.bgi.height==480?16:14;
	return x>=0 && y>=0 && (uint32_t)x<big->state.bgi.width && (uint32_t)y<(unsigned)big->terminal->height*ch;
}
static int pointer_begin(struct cterm_big *big)
{
	struct cterm_big_collect *c=&big->collect;
	c->pointer=true; c->shown=false; c->confirming=c->extended=c->scan_pending=c->armed=false;
	c->x=318; c->y=98; c->old_x=319; c->old_y=99;
	c->buttons=big->mouse.buttons;
	c->mouse_known=big->mouse.known;
	c->mouse_x=big->mouse.known_x; c->mouse_y=big->mouse.known_y;
	big->state.bgi.write_mode=1;
	int status=st_console_get_pointer_visible(big->terminal->screen.console,&c->host_visible);
	if (status!=0 && status!=IO_ERROR_NOT_SUPPORTED) return status;
	if (status==0) { c->host_saved=true; status=host_pointer(big,false); }
	else status=0;
	return status==0?redraw(big):status;
}
int cterm_big_collect_begin(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (view->count!=3 || !view->arguments[1].has_value || !view->arguments[2].has_value) return IO_ERROR_PROTOCOL;
	uint64_t generation=big->collect.generation+1u;
	big->collect=(struct cterm_big_collect){.active=true,.chained=view->chained,.generation=generation};
	int status=st_big_collect_begin(&big->collect.reader,&big->control,(uint16_t)view->arguments[1].value,(uint16_t)view->arguments[2].value);
	if (status==0 && big->collect.reader.type==2) status=pointer_begin(big);
	return status==0?IO_ERROR_IN_PROGRESS:status;
}
static int select_zone(struct cterm_big *big)
{
	struct cterm_big_collect *c=&big->collect;
	int status=pointer_end(big); if (status!=0) return status;
	bool retry; const struct st_big_collect_sink sink={big,echo};
	status=st_big_collect_zone(&c->reader,&big->control,c->x,c->y,&sink,&retry);
	if (status==0 && retry) status=pointer_begin(big);
	return status;
}
int cterm_big_collect_sample(struct cterm_big *big, const struct cterm_big_mouse_sample *sample)
{
	struct cterm_big_collect *c=&big->collect;
	if (!c->pointer || sample->generation!=c->generation) return 0;
	bool inside=mouse_inside(big,sample->x,sample->y);
	bool moved=inside && (!c->mouse_known || c->mouse_x!=sample->x || c->mouse_y!=sample->y);
	uint32_t changed=c->buttons^sample->buttons;
	c->buttons=sample->buttons;
	/* A cell-only observation has no new pixel sample. Keep the last
	 * known coordinates so its later stationary release cannot undo a key. */
	if (sample->x>=0 && sample->y>=0) {
		c->mouse_known=true; c->mouse_x=sample->x; c->mouse_y=sample->y;
	}
	if (!sample->allowed || !inside) { c->armed=false; return 0; }
	if (c->confirming) return select_zone(big);
	int status=0;
	if (moved) { c->x=(uint16_t)sample->x; c->y=(uint16_t)sample->y; status=redraw(big); }
	if (status!=0) return status;
	if ((changed&ST_CONSOLE_MOUSE_BUTTON_LEFT)!=0) {
		if ((sample->buttons&ST_CONSOLE_MOUSE_BUTTON_LEFT)!=0) c->armed=true;
		else if (c->armed) { c->armed=false; return select_zone(big); }
	}
	return 0;
}
static int pointer_byte(struct cterm_big *big, uint8_t byte)
{
	struct cterm_big_collect *c=&big->collect; unsigned direction=0;
	if (c->extended) {
		c->extended=false;
		switch (byte) { case 0x48:direction=1;break; case 0x50:direction=2;break;
		case 0x4d:direction=3;break; case 0x4b:direction=4;break; default:break; }
	} else if (byte==0) { c->extended=true; return 0; }
	else if (byte==13) direction=5;
	if (c->confirming) return direction==5?0:select_zone(big);
	if (direction==5) { c->confirming=true; c->armed=false; return 0; }
	st_big_collect_move(&big->control,(uint16_t)(big->state.bgi.width-1),(uint16_t)(big->state.bgi.height-1),direction,&c->x,&c->y);
	return redraw(big);
}
int cterm_big_collect_poll(struct cterm_big *big, size_t *budget, bool *complete)
{
	struct cterm_big_collect *c=&big->collect;
	const struct st_big_collect_sink sink={big,echo}; *complete=false;
	/* If pointer history overflows, suppress all already queued mouse
	 * events. A fresh observed press is required; keys retain their order. */
	if (c->mouse_lost) {
		for (size_t i=0;i<big->input.count;++i) {
			struct cterm_big_event *e=&big->input.events[(big->input.first+i)%CTERM_BIG_INPUT_KEYS];
			if (e->mouse) e->data.sample.generation=c->generation-1u;
		}
		c->mouse_lost=false; c->armed=false; c->buttons=big->mouse.buttons;
		c->mouse_x=big->mouse.known_x; c->mouse_y=big->mouse.known_y;
		c->mouse_known=big->mouse.known;
	}
	for (;;) {
		if (big->reset_pending) return IO_ERROR_INTERRUPTED;
		if (!c->reader.active) { c->active=false; *complete=true; return 0; }
		if (*budget==0) return IO_ERROR_IN_PROGRESS;
		int status;
		if (!c->pointer) {
			uint8_t byte; status=cterm_big_input_read(big,&byte);
			if (status==IO_ERROR_IN_PROGRESS) return 0;
			if (status!=0) return status;
			--*budget; status=st_big_collect_key(&c->reader,&big->control,byte,&sink);
		} else if (c->scan_pending) {
			c->scan_pending=false; --*budget; status=pointer_byte(big,c->scan);
		} else {
			struct cterm_big_event event;
			if (!cterm_big_input_take(big,&event)) {
				if (!c->confirming || c->extended) return 0;
				status=select_zone(big);
			} else {
				--*budget;
				if (event.mouse) status=cterm_big_collect_sample(big,&event.data.sample);
				else {
					uint8_t byte=cterm_big_key_byte(&event.data.key,&c->scan,&c->scan_pending);
					status=pointer_byte(big,byte);
				}
			}
		}
		if (status!=0) return status;
	}
}
int cterm_big_collect_cancel(struct cterm_big *big)
{
	int status=pointer_end(big);
	uint64_t generation=big->collect.generation+1u;
	big->collect=(struct cterm_big_collect){.generation=generation};
	big->mouse.pending=false;
	return status;
}
