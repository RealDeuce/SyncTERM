/* BIG fill ownership/validation; the native cache contains indexed pens. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_fill.h"
#include "st_big_number.h"
#include "st_bgi_data.h"
#include "io_error.h"

static int installed(struct st_big_state *s)
{
	if (s->bgi.state!=0) return 0;
	s->bgi.error=-1; return IO_ERROR_PROTOCOL;
}
static int colors(struct st_big_state *s)
{
	s->bgi.fill_low=(uint8_t)s->bgi.fill_color;
	int status=installed(s); if (status!=0) return status;
	s->bgi.device_color=(uint8_t)(s->bgi.color&15);
	s->bgi.device_fill_color=(uint8_t)(s->bgi.fill_low&15);
	return 0;
}
static void pattern(struct st_big_state *s, uint8_t style, const uint8_t *source)
{
	s->bgi.device_fill_style=style;
	for (size_t row=0;row<8;++row) s->bgi.fill_pattern[row]=source[row];
	/* Equivalent to the original colour-expanded plane cache, converted
	 * at installation to an 8x8 indexed tile. Later colour tuple changes
	 * do not rebuild it. Zero bits are opaque pen0, not transparency. */
	uint8_t pen=s->bgi.device_fill_color;
	for (size_t row=0;row<8;++row) for (size_t x=0;x<8;++x)
		s->bgi.fill_pixels[row*8+x]=(s->bgi.fill_pattern[row]&(0x80u>>x))!=0?pen:0;
}
int st_big_fill_builtin(struct st_big_state *s, uint16_t style, uint16_t color)
{
	if (s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (style>11) { s->bgi.error=-11; return 0; }
	s->bgi.fill_style=style; s->bgi.fill_color=color;
	int status=colors(s); if (status!=0) return status;
	pattern(s,(uint8_t)style,st_bgi_fill_patterns[style]);
	return 0;
}
int st_big_fill_custom(struct st_big_state *s, const uint8_t source[8], uint16_t color)
{
	if (s==NULL || source==NULL) return IO_ERROR_INVALID_ARGUMENT;
	/* Query maxcolour before public stores/copies. The selected native
	 * VGA-capable profile supplies all sixteen pens in every resolution. */
	int status=installed(s); if (status!=0) return status;
	if (color>15) { s->bgi.error=-11; return 0; }
	s->bgi.fill_style=12; s->bgi.fill_color=color;
	for (size_t row=0;row<8;++row) s->bgi.fill_user[row]=source[row];
	status=colors(s); if (status!=0) return status;
	/* Re-read the original source, rather than substituting the mirror. */
	for (size_t row=0;row<8;++row) s->bgi.device_fill_user[row]=source[row];
	pattern(s,12,s->bgi.device_fill_user);
	return 0;
}
int st_big_fill_apply(struct st_big_state *s)
{
	if (s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int32_t selected=st_big_number_signed(s->application.fill);
	if (selected<0 || selected>23) s->application.fill=1;
	if (st_big_number_signed(s->application.fill)<12) {
		int status=st_big_fill_builtin(s,s->application.fill,s->application.colors[2]);
		if (status!=0) return status;
	}
	if (st_big_number_signed(s->application.fill)>11) {
		size_t slot=(size_t)(s->application.fill-12);
		if (slot>=12) return IO_ERROR_OUT_OF_RANGE;
		return st_big_fill_custom(s,s->application.patterns[slot],s->application.colors[2]);
	}
	return 0;
}
int st_big_fill_select(struct st_big_state *s, uint16_t requested)
{
	if (s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	s->application.fill=requested;
	return st_big_fill_apply(s);
}
int st_big_fill_edit_begin(struct st_big_fill_edit *e, uint16_t slot, uint16_t alter)
{
	if (e==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*e=(struct st_big_fill_edit){.slot=slot,.alter=alter,.active=true};
	if (st_big_number_signed(e->slot)<12 || st_big_number_signed(e->slot)>23) e->slot=12;
	if (st_big_number_signed(e->alter)<0 || st_big_number_signed(e->alter)>1) e->alter=0;
	e->editing=e->alter==1;
	return 0;
}
static int edit_error(struct st_big_fill_edit *e, int status) { e->error=status; return status; }
int st_big_fill_edit_row(struct st_big_fill_edit *e, struct st_big_state *s, uint16_t value)
{
	if (e==NULL || s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (e->error!=0) return e->error;
	if (!e->active || !e->editing || e->row>=8) return edit_error(e,IO_ERROR_PROTOCOL);
	if (e->slot<12 || e->slot>23) return edit_error(e,IO_ERROR_OUT_OF_RANGE);
	s->application.patterns[e->slot-12][e->row]=(uint8_t)value;
	++e->row;
	return 0;
}
int st_big_fill_edit_finish(struct st_big_fill_edit *e, struct st_big_state *s)
{
	if (e==NULL || s==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (e->error!=0) return e->error;
	if (!e->active || (e->editing && e->row!=8)) return edit_error(e,IO_ERROR_PROTOCOL);
	/* Independent post-edit test: a changed flag can edit then restore.
	 * Restoration reloads the current slot for every source/destination. */
	if (e->alter==0) for (size_t row=0;row<8;++row) {
		if (e->slot<12 || e->slot>23) return edit_error(e,IO_ERROR_OUT_OF_RANGE);
		s->application.patterns[e->slot-12][row]=st_big_fill_restore[e->slot-12][row];
	}
	e->active=false;
	return 0;
}
void st_big_fill_edit_cancel(struct st_big_fill_edit *e)
{
	if (e!=NULL) *e=(struct st_big_fill_edit){0};
}
