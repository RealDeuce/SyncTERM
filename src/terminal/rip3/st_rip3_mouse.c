/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>

struct field {
	struct st_rip3_rect area;
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
	bool left, inverted;
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
	mouse->sink=*sink;*result=mouse;return 0;
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
st_rip3_mouse_define(struct st_rip3_mouse *mouse, const struct st_rip3_rect *area,
    bool invert, bool reset, int identity, const uint8_t *program, size_t length)
{
	if (mouse==NULL || area==NULL || (program==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (identity<-1 || identity>32767 || area->x0<0 || area->y0<0 || area->x1<=area->x0 || area->y1<=area->y0)
		return IO_ERROR_OUT_OF_RANGE;
	if (mouse->count==128 || length>SIZE_MAX-sizeof(struct field)) return IO_ERROR_OUT_OF_RANGE;
	if (invert && mouse->sink.invert==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct field *field=malloc(sizeof(*field)+length);
	if (field==NULL) return IO_ERROR_OUT_OF_MEMORY;
	field->area=*area;field->invert=invert;field->reset=reset;field->identity=identity;field->length=length;
	if (length!=0) memcpy(field->program,program,length);
	mouse->fields[mouse->count++]=field;return 0;
}

static bool
inside(const struct field *field, int32_t x, int32_t y)
{
	return x>=field->area.x0 && x<field->area.x1 && y>=field->area.y0 && y<field->area.y1;
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
	if (hit && mouse->sink.activate!=NULL) {
		/* An active variable may free this very field. The query evaluator also
		 * copies its input, but this ownership boundary must stand on its own. */
		uint8_t *copy=malloc(field->length==0 ? 1 : field->length);
		if (copy!=NULL) {
			size_t length=field->length;
			int identity=field->identity;
			bool reset=field->reset;
			if (length!=0) memcpy(copy,field->program,length);
			mouse->sink.activate(mouse->sink.context,copy,length,reset,identity,x,y);free(copy);
		}
		else if (mouse->sink.resource_failure!=NULL) mouse->sink.resource_failure(mouse->sink.context);
	}
	return true;
}
