/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>

struct field {
	struct st_rip3_rect area;
	enum st_rip3_mouse_kind kind;
	struct st_rip3_mouse_binding binding;
	uint64_t instance;
	bool invert,reset;
	int identity;
	size_t length;
	uint8_t program[];
};
struct st_rip3_mouse {
	struct st_rip3_mouse_sink sink;
	struct field *fields[128];
	size_t count;
	struct field *pressed;
	uint64_t sequence;
	bool left, inverted,hotkeys;
};

int
st_rip3_mouse_identity(const uint8_t **program, size_t *length)
{
	if (program==NULL || length==NULL || *program==NULL || *length<5) return -1;
	const uint8_t *text=*program;size_t size=*length;
	if ((text[0]!='I' && text[0]!='i') || (text[1]!='D' && text[1]!='d') || text[2]!='=') return -1;
	unsigned value=0;size_t at=3;
	while (at<size && text[at]>='0' && text[at]<='9') {
		if (at==8) return -1;
		value=value*10+(unsigned)(text[at++]-'0');
	}
	if (at==3 || at==size || text[at]!=':' || value>32767) return -1;
	*program=text+at+1;*length=size-at-1;return (int)value;
}

int
st_rip3_mouse_create(struct st_rip3_mouse **result, const struct st_rip3_mouse_sink *sink)
{
	if (result==NULL || sink==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_mouse *mouse=calloc(1,sizeof(*mouse));
	if (mouse==NULL) return IO_ERROR_OUT_OF_MEMORY;
	mouse->sink=*sink;mouse->hotkeys=true;*result=mouse;return 0;
}

static void
feedback(struct st_rip3_mouse *mouse, bool inverted)
{
	if (mouse->inverted==inverted) return;
	if (mouse->sink.invert!=NULL) mouse->sink.invert(mouse->sink.context,&mouse->pressed->area);
	mouse->inverted=inverted;
}

void
st_rip3_mouse_cancel(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	feedback(mouse,false);mouse->pressed=NULL;
	/* Retain the sampled button state: a held button cannot acquire a new
	 * field merely because its old definition was removed. */
}

void
st_rip3_mouse_kill(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	st_rip3_mouse_cancel(mouse);
	for (size_t i=0;i<mouse->count;i++) free(mouse->fields[i]);
	mouse->count=0;
}

void
st_rip3_mouse_destroy(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	st_rip3_mouse_kill(mouse);free(mouse);
}

bool
st_rip3_mouse_active(const struct st_rip3_mouse *mouse)
{
	return mouse!=NULL && mouse->count!=0;
}

size_t
st_rip3_mouse_count(const struct st_rip3_mouse *mouse)
{
	return mouse==NULL ? 0 : mouse->count;
}

int
st_rip3_mouse_define(struct st_rip3_mouse *mouse, enum st_rip3_mouse_kind kind,
    const struct st_rip3_mouse_binding *binding, const struct st_rip3_rect *area,
    bool invert, bool reset, int identity, const uint8_t *program, size_t length)
{
	if (mouse==NULL || area==NULL || (program==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (kind!=ST_RIP3_MOUSE_SIMPLE && kind!=ST_RIP3_MOUSE_BUTTON) return IO_ERROR_INVALID_ARGUMENT;
	if (identity<-1 || identity>32767 || area->x0<0 || area->y0<0 || area->x1<=area->x0 || area->y1<=area->y0)
		return IO_ERROR_OUT_OF_RANGE;
	if (mouse->count==128 || length>SIZE_MAX-sizeof(struct field)) return IO_ERROR_OUT_OF_RANGE;
	if (mouse->sequence==UINT64_MAX) return IO_ERROR_OUT_OF_RANGE;
	if (invert && mouse->sink.invert==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct field *field=malloc(sizeof(*field)+length);
	if (field==NULL) return IO_ERROR_OUT_OF_MEMORY;
	field->area=*area;field->kind=kind;field->invert=invert;field->reset=reset;field->identity=identity;field->length=length;
	field->binding=kind==ST_RIP3_MOUSE_BUTTON && binding!=NULL ? *binding : (struct st_rip3_mouse_binding){0};
	field->instance=++mouse->sequence;
	if (length!=0) memcpy(field->program,program,length);
	mouse->fields[mouse->count++]=field;return 0;
}

int
st_rip3_mouse_remove(struct st_rip3_mouse *mouse, const struct st_rip3_rect *area, unsigned flags,
    const struct st_rip3_mouse_paint *paint)
{
	if (mouse==NULL || area==NULL || area->x0<0 || area->y0<0 || (flags&~255u)!=0)
		return IO_ERROR_INVALID_ARGUMENT;
	if ((flags&7)==0 || area->x1<=area->x0 || area->y1<=area->y0) return 0;
	bool painting=(flags&96)!=0;
	if (painting && (paint==NULL || paint->apply==NULL)) return IO_ERROR_NOT_SUPPORTED;
	/* Bounded command-local snapshots, not another resident damage/scene
	 * layer. Stable pointers identify the selection until feedback is gone. */
	bool selected[128];struct st_rip3_rect removed[128];size_t count=0;
	for (size_t i=0;i<mouse->count;i++) {
		struct field *field=mouse->fields[i];
		bool excluded=(flags&(field->kind==ST_RIP3_MOUSE_SIMPLE ? 8u : 16u))!=0;
		bool contained=field->area.x0>=area->x0 && field->area.y0>=area->y0
		    && field->area.x1<=area->x1 && field->area.y1<=area->y1;
		bool overlap=field->area.x0<area->x1 && field->area.y0<area->y1
		    && field->area.x1>area->x0 && field->area.y1>area->y0;
		selected[i]=!excluded && (((flags&1)!=0 && contained)
		    || ((flags&2)!=0 && overlap && !contained) || ((flags&4)!=0 && !overlap));
		if (selected[i]) removed[count++]=field->area;
	}
	for (size_t i=0;i<mouse->count;i++)
		if (selected[i] && mouse->pressed==mouse->fields[i]) st_rip3_mouse_cancel(mouse);
	size_t kept=0;
	for (size_t i=0;i<mouse->count;i++) {
		struct field *field=mouse->fields[i];
		if (selected[i]) free(field);
		else mouse->fields[kept++]=field;
	}
	mouse->count=kept;
	if (!painting) return 0;
	bool pattern=(flags&64)!=0;
	if ((flags&128)!=0) return paint->apply(paint->context,area,pattern);
	int status=0;
	while (count!=0) {
		int result=paint->apply(paint->context,&removed[--count],pattern);
		if (status==0) status=result;
	}
	return status;
}

static bool
inside(const struct field *field, int32_t x, int32_t y)
{
	return x>=field->area.x0 && x<field->area.x1 && y>=field->area.y0 && y<field->area.y1;
}

static void
commit(struct st_rip3_mouse *mouse, struct field *field, bool keyboard, int32_t x, int32_t y)
{
	/* Snapshot before cancel/feedback and before a callback can delete the
	 * field. Cleanup checks only the live capture's token, never field. */
	size_t length=field->length;int identity=field->identity;bool reset=field->reset;
	uint64_t instance=field->instance;
	uint8_t *copy=malloc(length==0 ? 1 : length);
	if (copy!=NULL && length!=0) memcpy(copy,field->program,length);
	if (keyboard) st_rip3_mouse_cancel(mouse);
	if (copy==NULL) {
		if (mouse->sink.resource_failure!=NULL) mouse->sink.resource_failure(mouse->sink.context);
		return;
	}
	if (keyboard) {mouse->pressed=field;feedback(mouse,field->invert);}
	if (mouse->sink.activate!=NULL)
		mouse->sink.activate(mouse->sink.context,copy,length,reset,identity,!keyboard,x,y);
	/* Deletion clears pressed before freeing. A same-address replacement
	 * cannot acquire that canceled capture without a fresh physical press. */
	if (keyboard && mouse->pressed!=NULL && mouse->pressed->instance==instance) st_rip3_mouse_cancel(mouse);
	free(copy);
}

void
st_rip3_mouse_hotkeys(struct st_rip3_mouse *mouse, bool enabled)
{
	if (mouse!=NULL) mouse->hotkeys=enabled;
}

static uint8_t
fold(uint8_t byte)
{
	return byte>='a' && byte<='z' ? (uint8_t)(byte-'a'+'A') : byte;
}

bool
st_rip3_mouse_key(struct st_rip3_mouse *mouse, uint8_t byte)
{
	if (mouse==NULL || !mouse->hotkeys) return false;
	struct field *winner=NULL;unsigned best=3;
	for (size_t i=mouse->count;i>0;i--) {
		struct field *field=mouse->fields[i-1];
		if (field->kind!=ST_RIP3_MOUSE_BUTTON) continue;
		unsigned rank=3;uint8_t key=field->binding.hotkey;
		if (key!=0 && key!=255 && fold(key)==fold(byte)) rank=0;
		else if (byte==13 && field->binding.default_enter) rank=1;
		else if (key==255) rank=2;
		if (rank<best) {winner=field;best=rank;}
		if (best==0) break;
	}
	if (winner==NULL) return false;
	commit(mouse,winner,true,0,0);return true;
}

bool
st_rip3_mouse_event(struct st_rip3_mouse *mouse, int32_t x, int32_t y, bool left)
{
	if (mouse==NULL) return false;
	bool previous=mouse->left;mouse->left=left;
	if (left && !previous) {
		for (size_t i=mouse->count;i>0;i--) if (inside(mouse->fields[i-1],x,y)) {
			mouse->pressed=mouse->fields[i-1];break;
		}
	}
	struct field *field=mouse->pressed;
	if (field==NULL) return false;
	bool hit=inside(field,x,y);
	feedback(mouse,left && hit && field->invert);
	if (left) return true;
	mouse->pressed=NULL;
	if (hit && mouse->sink.activate!=NULL) commit(mouse,field,false,x,y);
	return true;
}
