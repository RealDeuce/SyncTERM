/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_mouse.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_backup.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>

struct field {
	struct st_rip3_rect area;
	enum st_rip3_mouse_kind kind;
	struct st_rip3_mouse_binding binding;
	struct st_rip3_mouse_visual visual;
	uint64_t instance;
	bool invert,reset,hovered;
	int identity;
	size_t length;
	uint8_t program[];
};
struct activation {
	struct activation *next;
	uint64_t redraw_preview;size_t redraw_count,redraw_at,redraw_capacity;
	bool associated,render_only;
	size_t length;
	uint64_t instance;
	unsigned stage;
	int identity;int32_t x,y;
	bool keyboard;
	enum st_rip3_mouse_reset reset;
	uint64_t redraw[];
};
struct st_rip3_mouse {
	struct st_rip3_mouse_sink sink;
	struct field *fields[128];
	size_t count;
	struct field *pressed;
	struct field *tab_focus;
	uint64_t sequence,scene,gesture;
	struct activation *activation;
	struct st_rip3_templates *templates;
	bool left, inverted,hotkeys,pointer_capture,tab_enabled;
	struct {uint8_t *bytes;size_t length;} queries[2]; /* Entry, exit. */
	/* One bounded motion snapshot, independent of resident field storage. */
	struct {uint64_t instance;bool inside;} hover[128];
	size_t hover_count,hover_cursor;
	unsigned hover_phase;
	int32_t hover_x,hover_y;
};

static uint8_t *
action_program(struct activation *action)
{
	return (uint8_t *)(action->redraw+action->redraw_capacity);
}

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
	mouse->sink=*sink;mouse->hotkeys=true;mouse->tab_enabled=true;*result=mouse;return 0;
}

static void
set_tab_focus(struct st_rip3_mouse *mouse, struct field *field)
{
	if (mouse->tab_focus == field)
		return;
	mouse->tab_focus = field;
	if (mouse->sink.tab_focus != NULL)
		mouse->sink.tab_focus(mouse->sink.context, field == NULL ? NULL : &field->area);
}

void
st_rip3_mouse_clear_tab_focus(struct st_rip3_mouse *mouse)
{
	if (mouse != NULL)
		set_tab_focus(mouse, NULL);
}

void st_rip3_mouse_bind_templates(struct st_rip3_mouse *mouse,struct st_rip3_templates *templates)
{
	if (mouse!=NULL && mouse->count==0) mouse->templates=templates;
}
int st_rip3_mouse_group_check(const struct st_rip3_mouse *mouse,unsigned group,enum st_rip3_group_kind kind)
{
	return mouse==NULL || mouse->templates==NULL ? IO_ERROR_NOT_SUPPORTED
	    : st_rip3_templates_group_check(mouse->templates,group,kind);
}
uint64_t st_rip3_mouse_callback_owner(const struct st_rip3_mouse *mouse)
{
	return mouse!=NULL && mouse->activation!=NULL && mouse->activation->associated ? mouse->activation->instance : 0;
}
static void enqueue(struct st_rip3_mouse *mouse,struct activation *action)
{
	struct activation **tail=&mouse->activation;while (*tail!=NULL) tail=&(*tail)->next;*tail=action;
}

static void
feedback(struct st_rip3_mouse *mouse, bool inverted)
{
	if (mouse->inverted==inverted) return;
	if (mouse->pressed->visual.select!=NULL) {
		struct field *field=mouse->pressed;
		bool grouped=field->binding.group_kind!=ST_RIP3_GROUP_NONE;
		bool selected=grouped ? st_rip3_templates_button_selected(mouse->templates,field->instance) : false;
		bool display=grouped ? (inverted ? field->binding.group_kind==ST_RIP3_GROUP_RADIO || !selected : selected) : inverted;
		int status=!inverted && field->visual.restore!=NULL
		    ? field->visual.restore(field->visual.context,display) : field->visual.select(field->visual.context,display);
		if (status!=0 && mouse->sink.render_failure!=NULL) mouse->sink.render_failure(mouse->sink.context);
		if (mouse->sink.changed!=NULL) mouse->sink.changed(mouse->sink.context);
	}
	else if (mouse->sink.invert!=NULL) mouse->sink.invert(mouse->sink.context,&mouse->pressed->area);
	mouse->inverted=inverted;
}

void
st_rip3_mouse_cancel(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	feedback(mouse,false);mouse->pressed=NULL;mouse->pointer_capture=false;
	/* Retain the sampled button state: a held button cannot acquire a new
	 * field merely because its old definition was removed. */
}

static void
destroy_field(struct st_rip3_mouse *mouse,struct field *field)
{
	st_rip3_templates_button_remove(mouse->templates,field->instance);
	if (field->visual.destroy!=NULL) field->visual.destroy(field->visual.context);
	free(field);
}

void
st_rip3_mouse_kill(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	++mouse->scene;mouse->hover_phase=0;
	set_tab_focus(mouse, NULL);
	st_rip3_mouse_cancel(mouse);
	for (size_t i=0;i<mouse->count;i++) destroy_field(mouse,mouse->fields[i]);
	mouse->count=0;
}

void
st_rip3_mouse_destroy(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	st_rip3_mouse_abort(mouse);st_rip3_mouse_kill(mouse);
	st_rip3_mouse_clear_queries(mouse);free(mouse);
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
    bool invert, bool reset, int identity, const uint8_t *program, size_t length,
    const struct st_rip3_mouse_visual *visual)
{
	if (mouse==NULL || area==NULL || (program==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (kind!=ST_RIP3_MOUSE_SIMPLE && kind!=ST_RIP3_MOUSE_BUTTON) return IO_ERROR_INVALID_ARGUMENT;
	if (identity<-1 || identity>32767 || area->x0<0 || area->y0<0 || area->x1<=area->x0 || area->y1<=area->y0)
		return IO_ERROR_OUT_OF_RANGE;
	if (mouse->count==128 || length>SIZE_MAX-sizeof(struct field)) return IO_ERROR_OUT_OF_RANGE;
	if (mouse->sequence==UINT64_MAX) return IO_ERROR_OUT_OF_RANGE;
	if (visual!=NULL && (kind!=ST_RIP3_MOUSE_BUTTON || visual->select==NULL || visual->destroy==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if (invert && visual==NULL && mouse->sink.invert==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct field *field=malloc(sizeof(*field)+length);
	if (field==NULL) return IO_ERROR_OUT_OF_MEMORY;
	field->hovered=false;field->area=*area;field->kind=kind;field->invert=invert;field->reset=reset;field->identity=identity;field->length=length;
	field->binding=binding!=NULL ? *binding : (struct st_rip3_mouse_binding){0};
	if (kind==ST_RIP3_MOUSE_SIMPLE)
		field->binding.group_kind=ST_RIP3_GROUP_NONE;
	field->visual=visual!=NULL ? *visual : (struct st_rip3_mouse_visual){0};
	field->instance=++mouse->sequence;
	if (length!=0) memcpy(field->program,program,length);
	struct activation *initial=NULL;
	if (field->binding.group_kind!=ST_RIP3_GROUP_NONE) {
		if (field->binding.group_kind==ST_RIP3_GROUP_RADIO && field->binding.selected) {
			initial=calloc(1,sizeof(*initial)+128*sizeof(uint64_t));if (initial==NULL) {free(field);return IO_ERROR_OUT_OF_MEMORY;}
		}
		uint64_t redraw[128];size_t count;
		int status=st_rip3_templates_button_add(mouse->templates,field->instance,field->binding.group,
		    field->binding.group_kind,field->binding.selected,redraw,&count);
		if (status!=0) {free(initial);free(field);return status;}
		if (initial!=NULL) {
			initial->render_only=true;initial->redraw_capacity=128;
			for (size_t i=0;i<count;i++) if (redraw[i]!=field->instance) initial->redraw[initial->redraw_count++]=redraw[i];
			if (initial->redraw_count!=0) enqueue(mouse,initial);else free(initial);
		}
	}
	set_tab_focus(mouse, NULL);
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
	if (count != 0)
		set_tab_focus(mouse, NULL);
	size_t kept=0;
	for (size_t i=0;i<mouse->count;i++) {
		struct field *field=mouse->fields[i];
		if (selected[i]) destroy_field(mouse,field);
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

uint64_t
st_rip3_mouse_scene(const struct st_rip3_mouse *mouse)
{
	return mouse==NULL ? 0 : mouse->scene;
}

bool
st_rip3_mouse_pending(const struct st_rip3_mouse *mouse)
{
	return mouse!=NULL && (mouse->activation!=NULL || mouse->hover_phase!=0);
}

void
st_rip3_mouse_cancel_silent(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	if (mouse->pressed!=NULL && mouse->pressed->visual.restore!=NULL) {
		struct field *field=mouse->pressed;
		(void)field->visual.restore(field->visual.context,field->binding.group_kind!=ST_RIP3_GROUP_NONE
		    && st_rip3_templates_button_selected(mouse->templates,field->instance));
		mouse->inverted=false;mouse->pressed=NULL;
	}
	else if (mouse->pressed!=NULL && mouse->pressed->visual.select!=NULL && mouse->sink.busy!=NULL) {
		if (mouse->pressed->visual.abort!=NULL) mouse->pressed->visual.abort(mouse->pressed->visual.context);
		mouse->inverted=false;mouse->pressed=NULL;
	}
	else st_rip3_mouse_cancel(mouse);
	mouse->pointer_capture=false;
}

void
st_rip3_mouse_boundary(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	st_rip3_mouse_cancel_silent(mouse);mouse->hover_phase=0;
	for (size_t i=0;i<mouse->count;i++) mouse->fields[i]->hovered=false;
	/* An allocated activation already owns its source/FIELDID. Keep it;
	 * only the remaining uncommitted traversal candidates are invalidated. */
}

void
st_rip3_mouse_clear_queries(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	for (size_t i=0;i<2;i++) {
		free(mouse->queries[i].bytes);mouse->queries[i].bytes=NULL;mouse->queries[i].length=0;
	}
}

void
st_rip3_mouse_abort(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	set_tab_focus(mouse, NULL);
	st_rip3_mouse_boundary(mouse);while (mouse->activation!=NULL) {struct activation *next=mouse->activation->next;free(mouse->activation);mouse->activation=next;}
}

uint64_t
st_rip3_mouse_press_token(const struct st_rip3_mouse *mouse)
{
	return mouse==NULL || !mouse->pointer_capture || mouse->pressed==NULL ? 0 : mouse->gesture;
}

int
st_rip3_mouse_query_encoded(struct st_rip3_mouse *mouse,bool entry,const uint8_t *bytes,size_t length,bool converted)
{
	if (mouse==NULL || (bytes==NULL && length!=0)) return IO_ERROR_INVALID_ARGUMENT;
	const uint8_t *end=length==0 ? NULL : memchr(bytes,0,length);
	if (end!=NULL) length=(size_t)(end-bytes);
	if (length==0) return IO_ERROR_INVALID_FORMAT;
	if (st_rip3_graphical_units(bytes,length,converted)>1023) return IO_ERROR_OUT_OF_RANGE;
	bool off=length==5;
	static const char remove[]="$OFF$";
	for (size_t i=0;off && i<5;i++) {
		uint8_t c=bytes[i];if (c>='a' && c<='z') c-='a'-'A';off=c==(uint8_t)remove[i];
	}
	uint8_t *copy=off ? NULL : malloc(length);
	if (!off && copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (!off) memcpy(copy,bytes,length);
	unsigned slot=entry ? 0 : 1;
	free(mouse->queries[slot].bytes);mouse->queries[slot].bytes=copy;mouse->queries[slot].length=off ? 0 : length;
	return 0;
}

int
st_rip3_mouse_query(struct st_rip3_mouse *mouse,bool entry,const uint8_t *bytes,size_t length)
{
	return st_rip3_mouse_query_encoded(mouse,entry,bytes,length,false);
}

static bool
next_hover(struct st_rip3_mouse *mouse)
{
	while (mouse->hover_phase!=0) {
		if (mouse->hover_cursor==0) {
			if (mouse->hover_phase==2) {mouse->hover_phase=0;return false;}
			mouse->hover_phase=2;mouse->hover_cursor=mouse->hover_count;continue;
		}
		size_t at=--mouse->hover_cursor;struct field *field=NULL;
		for (size_t i=0;i<mouse->count;i++) if (mouse->fields[i]->instance==mouse->hover[at].instance) {field=mouse->fields[i];break;}
		if (field==NULL) continue;
		bool entry=mouse->hover_phase==2;
		if (mouse->hover[at].inside!=entry || field->hovered==entry) continue;
		field->hovered=entry;
		if (field->identity<0) continue;
		unsigned slot=entry ? 0 : 1;size_t length=mouse->queries[slot].length;
		if (length==0) continue;
		struct activation *action=length>SIZE_MAX-sizeof(*action) ? NULL : malloc(sizeof(*action)+length);
		if (action==NULL) {
			if (mouse->sink.resource_failure!=NULL) mouse->sink.resource_failure(mouse->sink.context);
			continue;
		}
		*action=(struct activation){.length=length,.instance=field->instance,.identity=field->identity,
		    .x=mouse->hover_x,.y=mouse->hover_y};
		memcpy(action_program(action),mouse->queries[slot].bytes,length);mouse->activation=action;return true;
	}
	return false;
}

void
st_rip3_mouse_move(struct st_rip3_mouse *mouse,int32_t x,int32_t y)
{
	if (mouse==NULL) return;
	if (mouse->hover_phase!=0) {
		if (mouse->sink.render_failure!=NULL) mouse->sink.render_failure(mouse->sink.context);
		return;
	}
	/* With no callbacks there can be no traversal mutation. Keep membership
	 * in one pass without creating a pending action for ordinary motion. */
	if (mouse->queries[0].length==0 && mouse->queries[1].length==0) {
		for (size_t i=0;i<mouse->count;i++)
			mouse->fields[i]->hovered=inside(mouse->fields[i],x,y);
		return;
	}
	mouse->hover_count=0;mouse->hover_x=x;mouse->hover_y=y;
	for (size_t i=0;i<mouse->count;i++) if (mouse->fields[i]->hovered!=inside(mouse->fields[i],x,y)) {
		size_t at=mouse->hover_count++;
		mouse->hover[at].instance=mouse->fields[i]->instance;mouse->hover[at].inside=inside(mouse->fields[i],x,y);
	}
	if (mouse->hover_count==0) return;
	mouse->hover_phase=1;mouse->hover_cursor=mouse->hover_count;st_rip3_mouse_resume(mouse);
}

void
st_rip3_mouse_resume(struct st_rip3_mouse *mouse)
{
	if (mouse==NULL) return;
	for (;;) {
		if (mouse->sink.busy!=NULL && mouse->sink.busy(mouse->sink.context)) return;
		if (mouse->activation==NULL && !next_hover(mouse)) return;
		struct activation *action=mouse->activation;
		if (action->stage==0) {
			if (action->redraw_preview!=0) {
				if (mouse->pressed!=NULL && mouse->pressed->instance==action->redraw_preview && mouse->inverted
				    && mouse->pressed->visual.restore!=NULL) {
					struct field *field=mouse->pressed;
					bool preview=field->binding.group_kind==ST_RIP3_GROUP_RADIO
					    || !st_rip3_templates_button_selected(mouse->templates,field->instance);
					if (field->visual.restore(field->visual.context,preview)!=0 && mouse->sink.render_failure!=NULL)
						mouse->sink.render_failure(mouse->sink.context);
				}
				action->redraw_preview=0;
				if (mouse->sink.changed!=NULL) mouse->sink.changed(mouse->sink.context);
			}
			if (action->redraw_at<action->redraw_count) {
				uint64_t token=action->redraw[action->redraw_at++];
				for (size_t i=0;i<mouse->count;i++) if (mouse->fields[i]->instance==token) {
					struct field *field=mouse->fields[i];
					if (mouse->pressed==field && mouse->inverted) action->redraw_preview=token;
					if (field->visual.select!=NULL && field->visual.select(field->visual.context,
					    st_rip3_templates_button_selected(mouse->templates,token))!=0 && mouse->sink.render_failure!=NULL)
						mouse->sink.render_failure(mouse->sink.context);
					break;
				}
				if (mouse->sink.changed!=NULL) mouse->sink.changed(mouse->sink.context);
				continue;
			}
			action->stage=1;
			if (action->keyboard && !action->associated) for (size_t i=0;i<mouse->count;i++) {
				struct field *field=mouse->fields[i];
				if (field->instance==action->instance) {
					mouse->pressed=field;feedback(mouse,field->invert || field->visual.select!=NULL);break;
				}
			}
		}
		else if (action->stage==1) {
			action->stage=2;
			if (!action->render_only && mouse->sink.activate!=NULL) mouse->sink.activate(mouse->sink.context,action_program(action),
			    action->length,action->reset,action->identity,!action->keyboard,action->x,action->y);
		}
		else if (action->stage==2) {
			action->stage=3;
			if (action->keyboard && !action->associated && mouse->pressed!=NULL && mouse->pressed->instance==action->instance)
				st_rip3_mouse_cancel(mouse);
		}
		else {mouse->activation=action->next;free(action);}
	}
}

static void
commit(struct st_rip3_mouse *mouse,struct field *field,bool keyboard,int32_t x,int32_t y)
{
	if (mouse->activation!=NULL) {
		if (mouse->sink.render_failure!=NULL) mouse->sink.render_failure(mouse->sink.context);
		return;
	}
	/* Snapshot before optional feedback, which can suspend while the source
	 * field/destination is deleted. Selection failure never revokes commitment. */
	size_t capacity=field->binding.group_kind==ST_RIP3_GROUP_NONE ? 0 : 128;
	size_t extra=capacity*sizeof(uint64_t);
	struct activation *action=field->length>SIZE_MAX-sizeof(*action)-extra ? NULL : malloc(sizeof(*action)+extra+field->length);
	if (action!=NULL) {
		*action=(struct activation){.length=field->length,.instance=field->instance,.identity=field->identity,
		    .x=x,.y=y,.keyboard=keyboard,.redraw_capacity=capacity};
		if (field->reset)
			action->reset=field->kind==ST_RIP3_MOUSE_SIMPLE ? ST_RIP3_MOUSE_RESET_AFTER : ST_RIP3_MOUSE_RESET_BEFORE;
		if (field->length!=0) memcpy(action_program(action),field->program,field->length);
	}
	if (keyboard) st_rip3_mouse_cancel(mouse);
	if (action==NULL) {
		if (mouse->sink.resource_failure!=NULL) mouse->sink.resource_failure(mouse->sink.context);
		return;
	}
	if (field->binding.group_kind!=ST_RIP3_GROUP_NONE) {
		action->associated=true;
		(void)st_rip3_templates_button_activate(mouse->templates,field->instance,action->redraw,&action->redraw_count);
		for (size_t at=0;at<action->redraw_count;at++) for (size_t i=0;i<mouse->count;i++) {
			struct field *target=mouse->fields[i];
			if (target->instance==action->redraw[at] && target->visual.committed!=NULL)
				target->visual.committed(target->visual.context,st_rip3_templates_button_selected(mouse->templates,target->instance));
		}
	}
	mouse->activation=action;st_rip3_mouse_resume(mouse);
}

void
st_rip3_mouse_hotkeys(struct st_rip3_mouse *mouse, bool enabled)
{
	if (mouse!=NULL) mouse->hotkeys=enabled;
}

bool
st_rip3_mouse_hotkeys_enabled(const struct st_rip3_mouse *mouse)
{
	return mouse != NULL && mouse->hotkeys;
}

bool
st_rip3_mouse_tab_enabled(const struct st_rip3_mouse *mouse)
{
	return mouse != NULL && mouse->tab_enabled;
}

void
st_rip3_mouse_set_tab(struct st_rip3_mouse *mouse, bool enabled)
{
	if (mouse == NULL)
		return;
	mouse->tab_enabled = enabled;
	if (!enabled)
		set_tab_focus(mouse, NULL);
}

static bool
tab_next(struct st_rip3_mouse *mouse, bool backward)
{
	if (!st_rip3_mouse_tab_enabled(mouse) || mouse->count == 0)
		return false;
	size_t next = 0;
	for (size_t i = 0; mouse->tab_focus != NULL && i < mouse->count; i++) {
		if (mouse->fields[i] != mouse->tab_focus)
			continue;
		next = backward ? (i + mouse->count - 1) % mouse->count : (i + 1) % mouse->count;
		break;
	}
	set_tab_focus(mouse, mouse->fields[next]);
	return true;
}

bool
st_rip3_mouse_navigation(struct st_rip3_mouse *mouse, uint8_t key, bool backward)
{
	if (key == '\t')
		return tab_next(mouse, backward);
	if (mouse == NULL || mouse->tab_focus == NULL || (key != '\r' && key != 0x1b))
		return false;
	struct field *field = mouse->tab_focus;
	set_tab_focus(mouse, NULL);
	if (key == '\r')
		commit(mouse, field, true, 0, 0);
	return true;
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
		unsigned rank=3;uint8_t key=field->binding.hotkey;
		if (key!=0 && key!=255 && fold(key)==fold(byte)) rank=0;
		else if ((byte==13 && field->binding.default_enter)
		    || (byte==27 && field->binding.default_escape)) rank=1;
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
			if (mouse->gesture==UINT64_MAX) {
				if (mouse->sink.resource_failure!=NULL) mouse->sink.resource_failure(mouse->sink.context);
				return false;
			}
			mouse->pressed=mouse->fields[i-1];mouse->pointer_capture=true;++mouse->gesture;break;
		}
	}
	struct field *field=mouse->pressed;
	if (field==NULL || !mouse->pointer_capture) return false;
	bool hit=inside(field,x,y);
	if (!left && hit && field->binding.group_kind!=ST_RIP3_GROUP_NONE) mouse->inverted=false;
	else if (!left && hit && mouse->inverted && field->visual.select!=NULL) {
		/* A committed ordinary release requests its actual unselected redraw.
		 * Outside/focus cancellation below only removes temporary feedback. */
		mouse->inverted=false;
		if (field->visual.committed!=NULL) field->visual.committed(field->visual.context,false);
		int status=field->visual.select(field->visual.context,false);
		if (status!=0 && mouse->sink.render_failure!=NULL) mouse->sink.render_failure(mouse->sink.context);
		if (mouse->sink.changed!=NULL) mouse->sink.changed(mouse->sink.context);
	}
	else feedback(mouse,left && hit && (field->invert || field->visual.select!=NULL));
	if (left) return true;
	mouse->pressed=NULL;mouse->pointer_capture=false;
	if (hit && mouse->sink.activate!=NULL) commit(mouse,field,false,x,y);
	return true;
}


/* Saved definitions own their text and detached visual descriptors. They never
 * own live input or scene pixels and never execute providers on save/restore. */
struct mouse_snapshot {
	struct field *fields[128];size_t count;
	struct st_rip3_group_snapshot *groups;
	struct {uint8_t *bytes;size_t length;} queries[2];
};
static void
free_snapshot(void *object)
{
	struct mouse_snapshot *snapshot=object;if (snapshot==NULL) return;
	for (size_t i=0;i<snapshot->count;i++) {
		struct field *field=snapshot->fields[i];
		if (field->visual.destroy!=NULL) field->visual.destroy(field->visual.context);
		free(field);
	}
	for (unsigned i=0;i<2;i++) free(snapshot->queries[i].bytes);
	st_rip3_templates_free_groups(snapshot->groups);
	free(snapshot);
}
static int
copy_fields(struct mouse_snapshot *copy,struct field *const *fields,size_t count,struct st_rip3_mouse *owner,const struct st_rip3_templates *templates)
{
	for (size_t i=0;i<count;i++) {
		const struct field *source=fields[i];
		struct field *field=malloc(sizeof(*field)+source->length);
		if (field==NULL) return IO_ERROR_OUT_OF_MEMORY;
		memcpy(field,source,sizeof(*field)+source->length);field->instance=0;field->hovered=false;
		if (source->visual.select!=NULL) {
			field->visual.context=NULL;
			int status=source->visual.copy(source->visual.context,&field->visual.context,owner);
			if (status!=0 || field->visual.context==NULL) {free(field);return status!=0 ? status : IO_ERROR_INVALID_FORMAT;}
		}
		if (field->binding.group_kind!=ST_RIP3_GROUP_NONE) {
			field->binding.selected=owner==NULL ? st_rip3_templates_button_selected(templates,source->instance) : source->binding.selected;
			if (field->visual.committed!=NULL) field->visual.committed(field->visual.context,field->binding.selected);
		}
		copy->fields[copy->count++]=field;
	}
	return 0;
}
static int
copy_handler(struct mouse_snapshot *copy,unsigned index,const uint8_t *bytes,size_t length)
{
	if (length!=0) {
		copy->queries[index].bytes=malloc(length);if (copy->queries[index].bytes==NULL) return IO_ERROR_OUT_OF_MEMORY;
		memcpy(copy->queries[index].bytes,bytes,length);
	}
	copy->queries[index].length=length;return 0;
}
static int
snapshot_mouse(const void *object,void **result)
{
	if (object==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	const struct st_rip3_mouse *mouse=object;
	for (size_t i=0;i<mouse->count;i++) {
		const struct field *field=mouse->fields[i];
		if (field->visual.select!=NULL && field->visual.copy==NULL)
			return IO_ERROR_NOT_SUPPORTED;
	}
	struct mouse_snapshot *copy=calloc(1,sizeof(*copy));if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	uint64_t tokens[128];for (size_t i=0;i<mouse->count;i++) tokens[i]=mouse->fields[i]->instance;
	int status=st_rip3_templates_capture_groups(mouse->templates,tokens,mouse->count,&copy->groups);
	if (status==0) status=copy_fields(copy,mouse->fields,mouse->count,NULL,mouse->templates);
	for (unsigned i=0;status==0 && i<2;i++) status=copy_handler(copy,i,mouse->queries[i].bytes,mouse->queries[i].length);
	if (status!=0) {free_snapshot(copy);return status;}
	*result=copy;return 0;
}
static int
restore_mouse(void *object,const void *saved)
{
	if (object==NULL || saved==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_mouse *mouse=object;const struct mouse_snapshot *source=saved;
	if (source->count>UINT64_MAX-mouse->sequence) return IO_ERROR_OUT_OF_RANGE;
	/* The manager validates existence/location/protection before calling us.
	 * Once applying starts, later allocation failure cannot revive a press or
	 * old uncommitted hover traversal. Definitions/handlers already live remain
	 * usable on failure; committed source/FIELDID continuations remain owned. */
	set_tab_focus(mouse, NULL);
	st_rip3_mouse_boundary(mouse);
	struct mouse_snapshot *copy=calloc(1,sizeof(*copy));if (copy==NULL) return IO_ERROR_OUT_OF_MEMORY;
	uint64_t tokens[128];for (size_t i=0;i<source->count;i++) tokens[i]=mouse->sequence+i+1;
	int status=copy_fields(copy,source->fields,source->count,mouse,NULL);
	if (status==0) status=st_rip3_templates_prepare_groups(source->groups,tokens,source->count,&copy->groups);
	for (unsigned i=0;status==0 && i<2;i++) status=copy_handler(copy,i,source->queries[i].bytes,source->queries[i].length);
	if (status!=0) {free_snapshot(copy);return status;}
	st_rip3_templates_apply_groups(mouse->templates,copy->groups);copy->groups=NULL;
	st_rip3_mouse_kill(mouse);st_rip3_mouse_clear_queries(mouse);
	for (size_t i=0;i<copy->count;i++) {
		copy->fields[i]->instance=++mouse->sequence;mouse->fields[mouse->count++]=copy->fields[i];
	}
	for (unsigned i=0;i<2;i++) {mouse->queries[i].bytes=copy->queries[i].bytes;mouse->queries[i].length=copy->queries[i].length;}
	free(copy);return 0;
}
int
st_rip3_mouse_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops={snapshot_mouse,restore_mouse,free_snapshot};
	return st_rip3_backup_create(result,&ops);
}
