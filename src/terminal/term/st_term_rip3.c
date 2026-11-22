/* RIP3 transport, display, resource, prompt and mixer integration. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_term_rip3.h"
#include "st_codepage.h"
#include "st_term_rip3_view.h"
#include "st_rip3_session.h"
#include "st_rip3_text_window.h"
#include "st_rip3_surface.h"
#include "st_rip3_audio.h"
#include "st_rip3_features.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_local_variables.h"
#include "st_rip3_resources.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_variables_file.h"
#include "cterm_rip_time.h"
#include "st_time.h"
#include "io_error.h"
#include "st_pixel_image.h"
#include "filesystem.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct st_term_rip3 {
	struct st_term_config config;
	st_cterm_t *text;
	struct st_cterm_config text_config;
	struct st_term_rip3_view *view;
	struct st_term_rip3_view *windows[36];
	unsigned selected_window;
	bool targeting;
	bool mouse_feedback,mouse_feedback_applied;
	unsigned mouse_feedback_depth;
	struct st_console_rect mouse_area;
	struct st_pixel_buffer *mouse_saved;
	struct st_rip3_point mouse_position;
	uint8_t tty[4096];
	size_t tty_length;
	bool flushing;
	bool text_since_boundary;
	/* Suppress the entry TTY's cursor without discarding its requested shape,
	 * scan lines or visibility. Positioned views keep their own cursor state. */
	bool root_cursor_saved,root_cursor_hidden;
	enum st_console_cursor_shape root_cursor_shape;
	struct st_console_custom_cursor root_cursor;
	st_cterm_output_callback_t output;
	void *output_context;
	struct st_rip3_session *session;
	struct st_rip3_audio *audio;
	struct st_rip3_resource_paths resources;
	struct st_rip3_features features;
	struct st_rip3_terminal_identity identity;
	struct st_rip3_local_queries local;
	struct tm local_time;
	struct st_rip3_variables *session_values, *persistent_values;
	struct st_rip3_user_variables *user;
	char *variable_path;
	bool ready;
	unsigned colors;
	unsigned pointer_number;
	bool pointer_known;
	enum st_console_pointer_shape pointer_shape;
	uint64_t pixel_budget, presented;
	void (*retain_prompt_input)(void *context,uint64_t token,const struct st_console_event *event);
	void *prompt_input_context;
	uint64_t prompt_token,prompt_queued_token;
	bool observing_prompt;
	uint8_t *queue, *damage;
	size_t position, length, capacity, pixels;
	struct st_pixel_buffer *previous;
	uint8_t escape[3];
	size_t escape_length;
};

static void flush_text(void *context);

st_cterm_t *
st_term_rip3_text(struct st_term_rip3 *rip)
{
	return rip==NULL ? NULL : rip->view==NULL ? rip->text : st_term_rip3_view_text(rip->view);
}

bool
st_term_rip3_text_view(struct st_term_rip3 *rip, struct st_term_text_view *view)
{
	if (rip==NULL || rip->view==NULL || view==NULL) return false;
	const struct st_rip3_text_window_layout *layout=st_term_rip3_view_layout(rip->view);
	*view=(struct st_term_text_view){.console=st_term_rip3_view_console(rip->view),
	    .x=layout->x,.y=layout->y,.cell_width=layout->cell_width,.cell_height=layout->cell_height};
	return true;
}

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct st_term_rip3 *rip=context;
	static const char *const messages[]={"RIP3: invalid command discarded",
	    "RIP3: unsupported operation discarded", "RIP3: operation failed; continuing",
	    "RIP3: resource failure; continuing", "RIP3: text-window coordinates clamped; continuing"};
	if (rip->config.cterm_diagnostic!=NULL && rip->config.cterm_log_level>=ST_CTERM_LOG_WARNING)
		rip->config.cterm_diagnostic(rip->config.cterm_diagnostic_context,
		    ST_CTERM_LOG_WARNING,messages[kind]);
}

int
st_term_rip3_prepare_console(st_console_t *console)
{
	uint32_t width,height,caps;
	int status=st_console_capabilities(console,&caps);
	if (status!=0) return status;
	if ((caps&ST_CONSOLE_CAP_PIXELS)==0) return IO_ERROR_NOT_SUPPORTED;
	status=st_console_pixel_size(console,&width,&height);
	if (status!=0) return status;
	return width==0 || height==0 ? IO_ERROR_INVALID_ARGUMENT : 0;
}

static uint64_t
now(void *context)
{
	(void)context;
	uint64_t ns=0;
	(void)st_time_monotonic_ns(&ns);
	return ns;
}

static bool
next(void *context, uint8_t *byte)
{
	struct st_term_rip3 *rip=context;
	if (rip->position==rip->length) return false;
	*byte=rip->queue[rip->position++];
	return true;
}

static int
modify(void *context, const struct st_pixel_buffer *destination, struct st_console_rect *changed)
{
	struct st_term_rip3 *rip=context;
	const struct st_pixel_buffer *source=rip->previous;
	uint32_t left=source->width,top=source->height,right=0,bottom=0;
	for (uint32_t y=0; y<source->height && y<destination->height; y++) {
		for (uint32_t x=0; x<source->width && x<destination->width; x++) {
			size_t i=(size_t)y*source->width+x;
			if (rip->damage[i]==0) continue;
			size_t to=(size_t)y*destination->width+x;
			destination->primary[to]=source->primary[i];
			if (destination->alternate!=NULL) destination->alternate[to]=source->primary[i];
			if (x<left) left=x;
			if (y<top) top=y;
			if (x>=right) right=x+1;
			if (y>=bottom) bottom=y+1;
		}
	}
	if (right>left && bottom>top) *changed=(struct st_console_rect){left,top,right-left,bottom-top};
	return 0;
}

static void
refresh_pointer(struct st_term_rip3 *rip)
{
	unsigned number;
	if (!st_rip3_session_mouse_pointer(rip->session,&number)
	    || (rip->pointer_known && number==rip->pointer_number)) return;
	rip->pointer_number=number;rip->pointer_known=true;
	if (number>7) {diagnostic(rip,ST_RIP3_COMMAND_UNSUPPORTED);return;}
	static const enum st_console_pointer_shape shapes[]={ST_CONSOLE_POINTER_DEFAULT,
	    ST_CONSOLE_POINTER_WATCH,ST_CONSOLE_POINTER_CROSSHAIR,ST_CONSOLE_POINTER_TEXT,
	    ST_CONSOLE_POINTER_LINK,ST_CONSOLE_POINTER_STOP,ST_CONSOLE_POINTER_WAIT,ST_CONSOLE_POINTER_PROHIBITED};
	rip->pointer_shape=shapes[number];
	int status=st_console_set_pointer_shape(rip->config.console,rip->pointer_shape);
	if (status!=0) {
		/* Keep the requested ENV number; hover restores the actual fallback. */
		rip->pointer_shape=ST_CONSOLE_POINTER_DEFAULT;
		(void)st_console_set_pointer_shape(rip->config.console,rip->pointer_shape);
		if (status!=IO_ERROR_NOT_SUPPORTED) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	}
}

bool
st_term_rip3_pointer_shape(const struct st_term_rip3 *rip,enum st_console_pointer_shape *result)
{
	if (rip==NULL || result==NULL) return false;
	*result=rip->pointer_shape;return true;
}

static void
present(struct st_term_rip3 *rip)
{
	refresh_pointer(rip);
	uint64_t revision=st_rip3_session_revision(rip->session);
	if (revision==rip->presented) return;
	struct st_rip3_session_capture capture={0};
	if (st_rip3_session_capture(rip->session,&capture)!=0) {
		diagnostic(rip,ST_RIP3_COMMAND_RESOURCE_FAILURE);return;
	}
	/* Palette edits repaint existing graphics. Explicit paint damage also
	 * overwrites intervening ANSI ink when a COPY repeats the same index. */
	for (size_t i=0; i<rip->pixels; i++)
		if (capture.pixels->primary[i]!=rip->previous->primary[i]) rip->damage[i]=1;
	struct st_pixel_buffer *old=rip->previous;
	rip->previous=capture.pixels;
	if (st_console_modify_pixels(rip->config.console,modify,rip)==0) {
		rip->presented=revision;memset(rip->damage,0,rip->pixels);
		st_pixel_buffer_destroy(old);
	}
	else {
		rip->previous=old;st_pixel_buffer_destroy(capture.pixels);
		diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	}
}

static void
ansi(struct st_term_rip3 *rip, const uint8_t *bytes, size_t length)
{
	if (!st_rip3_session_text_active(rip->session)) return;
	if (length!=0) rip->text_since_boundary=true;
	for (size_t i=0;i<length;i++) {
		if (rip->tty_length==sizeof(rip->tty)) flush_text(rip);
		rip->tty[rip->tty_length++]=bytes[i];
		/* Streaming hooks can inspect state or suspend after this scalar.
		 * Keep their per-byte ordering; batch only the hook-free path. */
		if ((rip->config.input_filter!=NULL && (rip->config.input_filter_active==NULL
		    || rip->config.input_filter_active(rip->config.input_filter_context)))
		    || (rip->config.wire_filter!=NULL && (rip->config.wire_filter_active==NULL
		    || rip->config.wire_filter_active(rip->config.wire_filter_context)))) flush_text(rip);
	}
}

static int
root_visibility(struct st_term_rip3 *rip, bool active)
{
	if (active && !rip->root_cursor_saved) return 0;
	int status=0;
	if (!active) {
		if (!rip->root_cursor_saved) {
			bool visible;
			status=st_console_get_cursor(rip->config.console,&visible,&rip->root_cursor_shape);
			if (status==0) status=st_console_get_custom_cursor(rip->config.console,&rip->root_cursor);
			if (status!=0) return status;
			rip->root_cursor.visible=visible;rip->root_cursor_saved=true;
		}
		if (rip->root_cursor_hidden) return 0;
		struct st_console_custom_cursor hidden=rip->root_cursor;hidden.visible=false;
		status=rip->root_cursor_shape==ST_CONSOLE_CURSOR_BAR
		    ? st_console_set_cursor(rip->config.console,false,ST_CONSOLE_CURSOR_BAR)
		    : st_console_set_custom_cursor(rip->config.console,&hidden);
		if (status==0) rip->root_cursor_hidden=true;
	}
	else {
		status=rip->root_cursor_shape==ST_CONSOLE_CURSOR_BAR
		    ? st_console_set_cursor(rip->config.console,rip->root_cursor.visible,ST_CONSOLE_CURSOR_BAR)
		    : st_console_set_custom_cursor(rip->config.console,&rip->root_cursor);
		if (status==0) rip->root_cursor_saved=rip->root_cursor_hidden=false;
	}
	return status;
}

static int
text_visibility(struct st_term_rip3 *rip)
{
	if (rip->targeting) return 0;
	bool active=st_rip3_session_text_active(rip->session);
	return rip->view!=NULL ? st_term_rip3_view_active(rip->view,rip->config.console,active)
	    : root_visibility(rip,active);
}

static void
flush_text(void *context)
{
	struct st_term_rip3 *rip=context;
	if (rip->tty_length==0 || rip->flushing) return;
	rip->flushing=true;size_t length=rip->tty_length,used=0;rip->tty_length=0;
	int status=text_visibility(rip);
	if (status==0 && rip->view!=NULL) status=st_term_rip3_view_begin(rip->view,rip->config.console);
	if (status==0) status=st_cterm_write(st_term_rip3_text(rip),rip->tty,length,&used);
	if (rip->view!=NULL && status==0) status=st_term_rip3_view_present(rip->view,rip->config.console,
	    st_rip3_session_text_active(rip->session));
	if (status!=0 || used!=length) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	rip->flushing=false;
}

static void feedback_begin(struct st_term_rip3 *rip);
static void feedback_end(struct st_term_rip3 *rip);

void
st_term_rip3_flush(struct st_term_rip3 *rip)
{
	if (rip!=NULL) {feedback_begin(rip);present(rip);flush_text(rip);feedback_end(rip);}
}

int
st_term_rip3_begin_local_text(struct st_term_rip3 *rip)
{
	if (rip==NULL || rip->flushing) return 0;
	st_rip3_session_mouse_cancel(rip->session);
	rip->text_since_boundary=true;
	flush_text(rip);
	return rip->view==NULL ? 0 : st_term_rip3_view_begin(rip->view,rip->config.console);
}

int
st_term_rip3_end_local_text(struct st_term_rip3 *rip)
{
	return rip==NULL || rip->view==NULL || rip->flushing ? 0
	    : st_term_rip3_view_present(rip->view,rip->config.console,st_rip3_session_text_active(rip->session));
}

struct window_background {
	struct st_console_rect area;
	uint32_t color;
};

static int
paint_window_background(void *context, const struct st_pixel_buffer *pixels, struct st_console_rect *changed)
{
	const struct window_background *fill=context;
	uint32_t left=fill->area.x,top=fill->area.y;
	if (left>=pixels->width || top>=pixels->height) return 0;
	uint32_t width=fill->area.width,height=fill->area.height;
	if (width>pixels->width-left) width=pixels->width-left;
	if (height>pixels->height-top) height=pixels->height-top;
	for (uint32_t y=top;y<top+height;y++) for (uint32_t x=left;x<left+width;x++) {
		size_t at=(size_t)y*pixels->width+x;
		pixels->primary[at]=fill->color;
		if (pixels->alternate!=NULL) pixels->alternate[at]=fill->color;
	}
	*changed=(struct st_console_rect){left,top,width,height};return 0;
}

static int
paint_window_bounds(st_console_t *console, const struct st_rip3_rect *area, uint32_t rgb)
{
	if (area->x0>area->x1 || area->y0>area->y1) return 0;
	struct window_background fill={.area={(uint32_t)area->x0,(uint32_t)area->y0,
	    (uint32_t)(area->x1-area->x0+1),(uint32_t)(area->y1-area->y0+1)},.color=ST_PIXEL_COLOR_RGB_TAG|rgb};
	return st_console_modify_pixels(console,paint_window_background,&fill);
}

static int
window_budget(struct st_term_rip3 *rip, const struct st_rip3_text_window_layout *layout,
    unsigned replacing, const bool *keep)
{
	uint64_t width=(uint64_t)layout->columns*layout->cell_width;
	uint64_t height=(uint64_t)layout->rows*layout->cell_height;
	if (width>UINT32_MAX || height>UINT32_MAX || width*height>rip->pixel_budget)
		return IO_ERROR_OUT_OF_MEMORY;
	uint64_t remaining=rip->pixel_budget-width*height;
	for (unsigned i=0;i<36;i++) if (i!=replacing && rip->windows[i]!=NULL && (keep==NULL || keep[i])) {
		const struct st_rip3_text_window_layout *other=st_term_rip3_view_layout(rip->windows[i]);
		uint64_t pixels=(uint64_t)other->columns*other->cell_width*other->rows*other->cell_height;
		if (pixels>remaining) return IO_ERROR_OUT_OF_MEMORY;
		remaining-=pixels;
	}
	return 0;
}

static int
text_window(void *context, const struct st_rip3_text_window_layout *layout, bool identical, uint32_t clear_rgb)
{
	struct st_term_rip3 *rip=context;
	present(rip);
	if (identical && rip->view!=NULL) {
		int status=st_term_rip3_view_begin(rip->view,rip->config.console);
		return status!=0 ? status : st_cterm_set_text_wrap(st_term_rip3_view_text(rip->view),layout->wrap);
	}
	uint32_t width,height;
	int status=st_console_pixel_size(rip->config.console,&width,&height);
	if (status==0) status=window_budget(rip,layout,rip->selected_window,NULL);
	bool visible=true;
	if (status==0 && layout->extended) status=st_cterm_get_text_cursor_visible(st_term_rip3_text(rip),&visible);
	struct st_term_rip3_view *view=NULL;
	if (status==0) status=st_term_rip3_view_create(&view,&rip->text_config,layout,width,height);
	if (status==0) status=st_cterm_inherit_window_interpreter(st_term_rip3_view_text(view),st_term_rip3_text(rip),!layout->extended);
	if (status==0) {
		st_cterm_set_music_mode(st_term_rip3_view_text(view),st_cterm_music_mode(st_term_rip3_text(rip)));
		st_cterm_set_doorway_mode(st_term_rip3_view_text(view),st_cterm_doorway_mode(st_term_rip3_text(rip)));
	}
	if (status==0 && layout->extended && (!visible || (layout->flags&2)))
		status=st_cterm_set_text_cursor_visible(st_term_rip3_view_text(view),false);
	if (status==0) status=st_term_rip3_view_begin(view,rip->config.console);
	if (status==0 && layout->extended && (layout->flags&16)) {
		struct st_rip3_rect bounds;
		status=st_rip3_text_window_bounds(layout,width,height,&bounds);
		if (status==0) status=st_cterm_erase_text(st_term_rip3_view_text(view),true,clear_rgb);
		if (status==0) status=paint_window_bounds(rip->config.console,&bounds,clear_rgb);
	}
	if (status==0) status=st_term_rip3_view_present(view,rip->config.console,!layout->extended || !(layout->flags&4));
	if (status!=0) {st_term_rip3_view_destroy(view);return status;}
	st_term_rip3_view_destroy(rip->view);rip->view=view;rip->windows[rip->selected_window]=view;
	if (rip->text_config.size_changed!=NULL)
		rip->text_config.size_changed(rip->text_config.size_context,layout->columns,layout->rows,
		    layout->columns*layout->cell_width,layout->rows*layout->cell_height);
	return 0;
}

static int
present_window(struct st_term_rip3 *rip, bool active)
{
	if (rip->view!=NULL) {
		int status=st_term_rip3_view_begin(rip->view,rip->config.console);
		return status!=0 ? status : st_term_rip3_view_present(rip->view,rip->config.console,active);
	}
	const struct st_console_pixel_cursor cursor={0};
	int status=st_console_set_pixel_cursor(rip->config.console,&cursor);
	if (status==0) status=st_cterm_present_text_cursor(rip->text);
	rip->root_cursor_saved=rip->root_cursor_hidden=false;
	return status!=0 ? status : root_visibility(rip,active);
}

static int
text_snapshot(void *context, unsigned number, struct st_rip3_text_window_snapshot *result)
{
	struct st_term_rip3 *rip=context;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	struct st_term_rip3_view *view=rip->windows[number];
	st_cterm_t *text=view==NULL ? rip->text : st_term_rip3_view_text(view);
	struct st_cterm_state state;
	struct st_rip3_text_window_snapshot value={0};
	int status=st_cterm_state(text,&state);
	if (status==0) status=st_cterm_get_text_cursor_visible(text,&value.cursor_visible);
	if (status!=0) return status;
	value.column=(uint32_t)state.x;value.row=(uint32_t)state.y;
	const struct st_rip3_text_window_layout *layout=st_term_rip3_view_layout(view);
	if (layout!=NULL) value.layout=*layout;
	else {
		uint32_t columns,rows,cw,ch,x,y;
		status=st_cterm_size(text,&columns,&rows);
		if (status==0) status=st_cterm_origin(text,&x,&y);
		if (status==0) status=st_console_cell_pixel_size(rip->config.console,&cw,&ch);
		if (status!=0) return status;
		value.layout=(struct st_rip3_text_window_layout){.x1=columns-1,.y1=rows-1,
		    .columns=columns,.rows=rows,.cell_width=cw,.cell_height=ch,
		    .x=(uint64_t)(x-1)*cw,.y=(uint64_t)(y-1)*ch,
		    .bound_x=(uint64_t)(x-1)*cw,.bound_y=(uint64_t)(y-1)*ch,
		    .bound_width=(uint64_t)columns*cw,.bound_height=(uint64_t)rows*ch};
	}
	*result=value;return 0;
}

static int
text_select(void *context, unsigned number, bool first, bool active,
    const struct st_rip3_text_window_layout *fresh)
{
	struct st_term_rip3 *rip=context;
	st_cterm_t *source=st_term_rip3_text(rip);
	struct st_term_rip3_view *candidate=rip->windows[number];
	present(rip);
	int status=0;
	if (first) {
		struct st_rip3_text_window_layout layout;
		const struct st_rip3_text_window_layout *existing=st_term_rip3_view_layout(rip->view);
		uint32_t width,height;
		status=st_console_pixel_size(rip->config.console,&width,&height);
		if (fresh!=NULL) layout=*fresh;
		else if (existing!=NULL) layout=*existing;
		else {
			uint32_t columns,rows,cw,ch,x,y;struct st_cterm_state state;
			if (status==0) status=st_cterm_size(source,&columns,&rows);
			if (status==0) status=st_cterm_origin(source,&x,&y);
			if (status==0) status=st_console_cell_pixel_size(rip->config.console,&cw,&ch);
			if (status==0) status=st_cterm_state(source,&state);
			if (status==0) layout=(struct st_rip3_text_window_layout){.x1=columns-1,.y1=rows-1,
			    .columns=columns,.rows=rows,.cell_width=cw,.cell_height=ch,.wrap=state.ext_attr.auto_wrap,
			    .x=(uint64_t)(x-1)*cw,.y=(uint64_t)(y-1)*ch,.bound_x=(uint64_t)(x-1)*cw,.bound_y=(uint64_t)(y-1)*ch,
			    .bound_width=(uint64_t)columns*cw,.bound_height=(uint64_t)rows*ch};
		}
		if (status==0) status=window_budget(rip,&layout,number,NULL);
		if (status==0) status=st_term_rip3_view_create(&candidate,&rip->text_config,&layout,width,height);
		if (status==0) status=fresh!=NULL
		    ? st_cterm_default_window_interpreter(st_term_rip3_view_text(candidate),source)
		    : st_cterm_copy_window_state(st_term_rip3_view_text(candidate),source);
	}
	else status=st_cterm_select_window_interpreter(candidate==NULL ? rip->text : st_term_rip3_view_text(candidate),source);
	if (status==0) {
		st_cterm_t *target=candidate==NULL ? rip->text : st_term_rip3_view_text(candidate);
		st_cterm_set_music_mode(target,st_cterm_music_mode(source));
		st_cterm_set_doorway_mode(target,st_cterm_doorway_mode(source));
		struct st_term_rip3_view *previous=rip->view;
		rip->view=candidate;status=present_window(rip,active);
		if (status!=0) rip->view=previous;
	}
	if (status!=0) {if (first) st_term_rip3_view_destroy(candidate);return status;}
	rip->windows[number]=candidate;rip->selected_window=number;
	if (rip->text_config.size_changed!=NULL) {
		uint32_t columns,rows,width,height;
		if (st_cterm_size(st_term_rip3_text(rip),&columns,&rows)==0
		    && st_console_pixel_size(rip->config.console,&width,&height)==0)
			rip->text_config.size_changed(rip->text_config.size_context,columns,rows,width,height);
	}
	return 0;
}

static int
text_reset(void *context, const struct st_rip3_text_window_layout *layout,
    const bool keep[36], bool old_active)
{
	struct st_term_rip3 *rip=context;
	st_cterm_t *source=st_term_rip3_text(rip);
	int status=st_cterm_rip3_ansi_reset(source,old_active);
	uint32_t width,height;
	if (status==0) status=st_console_pixel_size(rip->config.console,&width,&height);
	if (status==0) status=window_budget(rip,layout,0,keep);
	struct st_term_rip3_view *candidate=NULL;
	if (status==0) status=st_term_rip3_view_create(&candidate,&rip->text_config,layout,width,height);
	st_cterm_t *zero=rip->windows[0]==NULL ? rip->text : st_term_rip3_view_text(rip->windows[0]);
	/* Ordinary redefinition retains TW0's tabs. Pending ANSI syntax belongs
	 * to the connection and comes from the old current owner instead. */
	if (status==0) status=st_cterm_inherit_window_interpreter(st_term_rip3_view_text(candidate),zero,true);
	if (status==0) status=st_cterm_select_window_interpreter(st_term_rip3_view_text(candidate),source);
	if (status==0) {
		st_cterm_t *target=st_term_rip3_view_text(candidate);
		st_cterm_set_music_mode(target,st_cterm_music_mode(source));
		st_cterm_set_doorway_mode(target,st_cterm_doorway_mode(source));
		struct st_term_rip3_view *previous=rip->view;
		rip->view=candidate;status=present_window(rip,true);
		if (status!=0) rip->view=previous;
	}
	if (status!=0) {st_term_rip3_view_destroy(candidate);return status;}
	for (unsigned i=0;i<36;i++) if (i==0 || !keep[i]) {
		st_term_rip3_view_destroy(rip->windows[i]);rip->windows[i]=NULL;
	}
	rip->windows[0]=candidate;rip->selected_window=0;
	if (rip->text_config.size_changed!=NULL)
		rip->text_config.size_changed(rip->text_config.size_context,layout->columns,layout->rows,
		    layout->columns*layout->cell_width,layout->rows*layout->cell_height);
	return 0;
}

static void
before_command(void *context)
{
	struct st_term_rip3 *rip=context;
	if (!rip->text_since_boundary) return;
	flush_text(rip);
	int status=rip->view!=NULL && st_rip3_session_text_active(rip->session)
	    ? st_term_rip3_view_begin(rip->view,rip->config.console) : 0;
	if (status==0) status=st_cterm_text_boundary(st_term_rip3_text(rip));
	if (status==0 && rip->view!=NULL && st_rip3_session_text_active(rip->session))
		status=st_term_rip3_view_present(rip->view,rip->config.console,true);
	if (status!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	else rip->text_since_boundary=false;
}

static int
text_cursor_current(void *context, const struct st_rip3_text_cursor_position *position)
{
	struct st_term_rip3 *rip=context;
	st_cterm_t *text=st_term_rip3_text(rip);
	struct st_cterm_state state;uint32_t columns,rows;
	int status=st_cterm_state(text,&state);
	if (status==0) status=st_cterm_size(text,&columns,&rows);
	if (status!=0) return status;
	uint32_t column=position->set_column
	    ? position->column>=columns ? columns-1 : (uint32_t)position->column : (uint32_t)(state.x-1);
	uint32_t row=position->set_row
	    ? position->row>=rows ? rows-1 : (uint32_t)position->row : (uint32_t)(state.y-1);
	status=st_cterm_position_text(text,column,row);
	if (status==0 && rip->view!=NULL)
		status=st_term_rip3_view_present(rip->view,rip->config.console,true);
	return status;
}

static int
text_erase_current(void *context, bool whole, uint32_t rgb, struct st_rip3_rect *area)
{
	struct st_term_rip3 *rip=context;
	/* Commit earlier graphics before importing the ANSI surface. The session
	 * mirrors this same erase into its index plane after a successful render. */
	present(rip);
	int status=text_visibility(rip);
	if (status==0 && rip->view!=NULL) status=st_term_rip3_view_begin(rip->view,rip->config.console);
	st_cterm_t *text=st_term_rip3_text(rip);struct st_cterm_state state;
	uint32_t columns,rows,width,height,cw,ch;uint64_t origin_x,origin_y;
	if (status==0) status=st_cterm_state(text,&state);
	if (status==0) status=st_cterm_size(text,&columns,&rows);
	if (status==0) status=st_console_pixel_size(rip->config.console,&width,&height);
	if (status!=0) return status;
	const struct st_rip3_text_window_layout *layout=st_term_rip3_view_layout(rip->view);
	if (layout!=NULL) {
		origin_x=layout->x;origin_y=layout->y;cw=layout->cell_width;ch=layout->cell_height;
	}
	else {
		uint32_t x,y;
		status=st_console_cell_pixel_size(rip->config.console,&cw,&ch);
		if (status==0) status=st_cterm_origin(text,&x,&y);
		if (status!=0) return status;
		origin_x=(uint64_t)(x-1)*cw;origin_y=(uint64_t)(y-1)*ch;
	}
	uint64_t left=origin_x,top=origin_y;
	uint64_t right=left+(uint64_t)columns*cw,bottom=top+(uint64_t)rows*ch;
	if (!whole) {
		left+=(uint64_t)(state.x-1)*cw;top+=(uint64_t)(state.y-1)*ch;
		bottom=top+ch;
	}
	struct st_rip3_rect clipped={0,0,-1,-1};
	if (left<width && top<height && right>left && bottom>top) {
		if (right>width) right=width;
		if (bottom>height) bottom=height;
		clipped=(struct st_rip3_rect){(int32_t)left,(int32_t)top,(int32_t)(right-1),(int32_t)(bottom-1)};
	}
	if (whole && layout!=NULL && layout->extended)
		status=st_rip3_text_window_bounds(layout,width,height,&clipped);
	if (status==0) status=st_cterm_erase_text(text,whole,rgb);
	if (status==0 && whole && layout!=NULL && layout->extended)
		status=paint_window_bounds(rip->config.console,&clipped,rgb);
	if (status==0 && rip->view!=NULL)
		status=st_term_rip3_view_present(rip->view,rip->config.console,true);
	if (status==0) *area=clipped;
	return status;
}

static int
text_erase_eol_current(void *context)
{
	struct st_term_rip3 *rip=context;
	present(rip);
	int status=text_visibility(rip);
	if (status==0 && rip->view!=NULL) status=st_term_rip3_view_begin(rip->view,rip->config.console);
	if (status==0) status=st_cterm_erase_ansi_text_eol(st_term_rip3_text(rip));
	if (status==0 && rip->view!=NULL)
		status=st_term_rip3_view_present(rip->view,rip->config.console,true);
	return status;
}

static int
text_cursor_visible_current(void *context, bool visible)
{
	struct st_term_rip3 *rip=context;
	/* An ATW earlier in the same host expansion can still have a hidden
	 * renderer cursor saved. Restore that gate before applying CON/COFF. */
	int status=text_visibility(rip);
	if (status==0) status=st_cterm_set_text_cursor_visible(st_term_rip3_text(rip),visible);
	if (status==0 && rip->view!=NULL)
		status=st_term_rip3_view_present(rip->view,rip->config.console,true);
	return status;
}

/* A noncurrent operation temporarily addresses its private native state. The
 * selected window is restored over freshly imported scene pixels, so cursor
 * restoration cannot replay stale artwork over the target's completed paint. */
static struct st_term_rip3_view *
target_window(struct st_term_rip3 *rip, unsigned number)
{
	struct st_term_rip3_view *previous=rip->view;
	rip->view=rip->windows[number];rip->targeting=true;return previous;
}

static int
finish_target(struct st_term_rip3 *rip, struct st_term_rip3_view *previous, int status)
{
	rip->view=previous;rip->targeting=false;
	int restored=present_window(rip,st_rip3_session_text_active(rip->session));
	return status!=0 ? status : restored;
}

static int
text_cursor(void *context, unsigned number, const struct st_rip3_text_cursor_position *position)
{
	struct st_term_rip3 *rip=context;
	if (number==rip->selected_window) return text_cursor_current(context,position);
	struct st_term_rip3_view *previous=target_window(rip,number);
	return finish_target(rip,previous,text_cursor_current(context,position));
}

static int
text_cursor_visible(void *context, unsigned number, bool visible)
{
	struct st_term_rip3 *rip=context;
	if (number==rip->selected_window) return text_cursor_visible_current(context,visible);
	struct st_term_rip3_view *previous=target_window(rip,number);
	return finish_target(rip,previous,text_cursor_visible_current(context,visible));
}

static int
text_erase(void *context, unsigned number, bool whole, uint32_t rgb, struct st_rip3_rect *area)
{
	struct st_term_rip3 *rip=context;
	if (number==rip->selected_window) return text_erase_current(context,whole,rgb,area);
	struct st_term_rip3_view *previous=target_window(rip,number);
	return finish_target(rip,previous,text_erase_current(context,whole,rgb,area));
}

static int
text_erase_eol(void *context, unsigned number)
{
	struct st_term_rip3 *rip=context;
	if (number==rip->selected_window) return text_erase_eol_current(context);
	struct st_term_rip3_view *previous=target_window(rip,number);
	return finish_target(rip,previous,text_erase_eol_current(context));
}

static void
text(void *context, const uint8_t *bytes, size_t length)
{
	struct st_term_rip3 *rip=context;
	present(rip);
	if (text_visibility(rip)!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	for (size_t i=0;i<length;i++) {
		uint8_t byte=bytes[i];
		if ((rip->escape_length==2 || rip->escape_length==3) && byte=='!') {
			static const uint8_t identity[]="RIPSCRIP030001";
			rip->escape_length=0;
			rip->output(rip->output_context,ST_CTERM_OUTPUT_RESPONSE,identity,sizeof(identity)-1);
			continue;
		}
		if ((rip->escape_length==1 && byte=='[') || (rip->escape_length==2 && byte=='0')) {
			rip->escape[rip->escape_length++]=byte;continue;
		}
		if (rip->escape_length!=0) {
			ansi(rip,rip->escape,rip->escape_length);rip->escape_length=0;
		}
		if (byte==0x1b) rip->escape[rip->escape_length++]=byte;
		else ansi(rip,&byte,1);
	}
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct st_term_rip3 *rip=context;
	present(rip);
	/* Publish active-variable effects before the peer receives its fence. */
	if (text_visibility(rip)!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	rip->output(rip->output_context,ST_CTERM_OUTPUT_RESPONSE,bytes,length);
}

static bool
line_start(void *context)
{
	struct st_term_rip3 *rip=context;
	flush_text(rip);
	struct st_cterm_state state;
	return st_cterm_state(st_term_rip3_text(rip),&state)==0 && state.x==1;
}

static int
modifier(void *context, enum st_rip3_modifier kind, bool *down)
{
	struct st_term_rip3 *rip=context;
	bool left=false,right=false;
	int status=st_console_physical_key_state(rip->config.console,
	    kind==ST_RIP3_MODIFIER_SHIFT ? 42 : 29,&left);
	if (status==0) status=st_console_physical_key_state(rip->config.console,
	    kind==ST_RIP3_MODIFIER_SHIFT ? 54 : 97,&right);
	if (status==0) *down=left || right;
	return status;
}

static bool
press_tail(const struct st_console_event *event)
{
	return (event->type==ST_CONSOLE_EVENT_MOUSE && event->data.mouse.action!=ST_CONSOLE_MOUSE_WHEEL
	        && (event->data.mouse.buttons&ST_CONSOLE_MOUSE_BUTTON_LEFT)==0)
	    || (event->type==ST_CONSOLE_EVENT_FOCUS && !event->data.focus.focused)
	    || event->type==ST_CONSOLE_EVENT_CLOSE || event->type==ST_CONSOLE_EVENT_RESIZE;
}

void
st_term_rip3_bind_prompt_input(struct st_term_rip3 *rip,
    void (*retain)(void *,uint64_t,const struct st_console_event *),void *context)
{
	if (rip==NULL) return;
	rip->retain_prompt_input=retain;rip->prompt_input_context=context;
}

void
st_term_rip3_note_queued_input(struct st_term_rip3 *rip,const struct st_console_event *event)
{
	if (rip==NULL) return;
	if (event->type==ST_CONSOLE_EVENT_FOCUS && !event->data.focus.focused)
		st_rip3_session_pointer_boundary(rip->session);
	if (press_tail(event)) rip->prompt_queued_token=st_rip3_session_press_token(rip->session);
}

void
st_term_rip3_observe_prompt_input(struct st_term_rip3 *rip,const struct st_console_event *event)
{
	if (rip==NULL || !rip->observing_prompt) return;
	/* Modal editors consume focus events too. Apply their interaction
	 * boundary now, even when this hover prompt has no held guest press.
	 * Keep its committed query/FIELDID; suppress the old traversal tail. */
	if (event->type==ST_CONSOLE_EVENT_FOCUS && !event->data.focus.focused)
		st_rip3_session_pointer_boundary(rip->session);
	if (rip->prompt_token==0 || rip->retain_prompt_input==NULL
	    || rip->prompt_queued_token==rip->prompt_token || !press_tail(event)) return;
	/* Exactly one release/cancel belongs to the original guest gesture. Later
	 * editor clicks/keys remain editor input. The owner already holds earlier
	 * physical events; append this tail rather than replaying it ahead of them. */
	rip->prompt_queued_token=rip->prompt_token;
	rip->retain_prompt_input(rip->prompt_input_context,rip->prompt_token,event);
}

static void
prompt(struct st_term_rip3 *rip)
{
	struct st_rip3_session_prompt view;
	if (!st_rip3_session_prompt(rip->session,&view)) return;
	char *value=NULL;
	const struct st_rip3_variable_reference *reference=view.reference;
	/* The existing editor supports unmasked, unconverted ANY values. Other
	 * editor policies remain explicitly unsupported rather than silently
	 * displaying a password or accepting incorrectly formatted input. */
	bool supported=(reference->directives&ST_RIP3_VARIABLE_MASKED)==0
	    && !reference->has_position;
	int status=IO_ERROR_NOT_SUPPORTED;
	rip->prompt_token=st_rip3_session_press_token(rip->session);
	rip->observing_prompt=true;
	if (supported && rip->config.rip_prompt!=NULL)
		status=rip->config.rip_prompt(rip->config.rip_prompt_context,
		    reference->has_question ? (const char *)reference->question.bytes : reference->name,
		    view.initial==NULL ? "" : (const char *)view.initial->bytes,
		    reference->has_width ? reference->width : 255,false,&value);
	rip->observing_prompt=false;
	if (status!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	st_rip3_session_respond(rip->session,(const uint8_t *)value,value==NULL ? 0 : strlen(value),
	    status!=0 || value==NULL);
	free(value);
	/* Required prompts reject user cancellation. A missing/failed editor
	 * cannot leave an unanswerable prompt holding the connection forever. */
	if (st_rip3_session_prompt(rip->session,&view) && (status!=0 || rip->config.rip_prompt==NULL))
		st_rip3_session_cancel(rip->session);
}

static void
process(struct st_term_rip3 *rip, bool publish)
{
	if (rip==NULL) return;
	(void)cterm_rip_localtime(time(NULL),&rip->local_time);
	st_rip3_session_process(rip->session,4096);
	if (publish) present(rip);
	prompt(rip);
	if (publish) present(rip);
	if (text_visibility(rip)!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	if (rip->position==rip->length) rip->position=rip->length=0;
}

static void
feedback_remove(struct st_term_rip3 *rip)
{
	if (st_console_restore_pixel_region(rip->config.console,rip->mouse_saved,rip->mouse_area.x,rip->mouse_area.y)!=0)
		diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	st_pixel_buffer_destroy(rip->mouse_saved);rip->mouse_saved=NULL;
}

static void
feedback_begin(struct st_term_rip3 *rip)
{
	if (rip->mouse_feedback_depth++==0 && rip->mouse_feedback_applied) {
		feedback_remove(rip);rip->mouse_feedback_applied=false;
	}
}

static void
feedback_end(struct st_term_rip3 *rip)
{
	if (--rip->mouse_feedback_depth==0 && rip->mouse_feedback) {
		int status=st_console_get_pixels(&rip->mouse_saved,rip->config.console,&rip->mouse_area,false);
		if (status==0) {
			/* Keep original index values as well as both blink planes. An RGB
			 * complement is visually reversible but loses palette identity. */
			rip->mouse_feedback_applied=true;
			status=st_console_invert_pixels(rip->config.console,&rip->mouse_area);
		}
		if (status!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	}
}

static void
mouse_invert(void *context, const struct st_rip3_rect *area)
{
	struct st_term_rip3 *rip=context;
	/* Remove the transient effect around native writes. The graphics owner
	 * retains normal ink, while text and graphics share the actual console.
	 * The temporary capture contains only the pressed rectangle. */
	feedback_begin(rip);
	rip->mouse_feedback=!rip->mouse_feedback;
	if (rip->mouse_feedback) rip->mouse_area=(struct st_console_rect){(uint32_t)area->x0,(uint32_t)area->y0,
	    (uint32_t)(area->x1-area->x0),(uint32_t)(area->y1-area->y0)};
	feedback_end(rip);
}

bool
st_term_rip3_mouse_active(const struct st_term_rip3 *rip)
{
	return rip!=NULL && st_rip3_session_mouse_active(rip->session);
}

static void
mouse_capture(void *context, int32_t x, int32_t y)
{
	struct st_term_rip3 *rip=context;
	rip->mouse_position=(struct st_rip3_point){x,y};
	rip->local.mouse_position=&rip->mouse_position;
}

void
st_term_rip3_captured_input(struct st_term_rip3 *rip,uint64_t token,const struct st_console_event *event)
{
	if (rip==NULL || token==0 || st_rip3_session_press_token(rip->session)!=token) return;
	if (event->type==ST_CONSOLE_EVENT_MOUSE) {
		mouse_capture(rip,event->data.mouse.pixel_x,event->data.mouse.pixel_y);
		(void)st_rip3_session_mouse(rip->session,event->data.mouse.pixel_x,event->data.mouse.pixel_y,
		    (event->data.mouse.buttons&ST_CONSOLE_MOUSE_BUTTON_LEFT)!=0);
	}
	else st_rip3_session_pointer_boundary(rip->session);
}

static void
mouse_redraw(void *context)
{
	struct st_term_rip3 *rip=context;
	present(rip);flush_text(rip);
}

bool
st_term_rip3_mouse(struct st_term_rip3 *rip, const struct st_console_event *event)
{
	if (rip==NULL || event->type!=ST_CONSOLE_EVENT_MOUSE) return false;
	mouse_capture(rip,event->data.mouse.pixel_x,event->data.mouse.pixel_y);
	if (event->data.mouse.action==ST_CONSOLE_MOUSE_WHEEL) return false;
	st_term_rip3_flush(rip);feedback_begin(rip);
	bool handled=st_rip3_session_mouse(rip->session,event->data.mouse.pixel_x,event->data.mouse.pixel_y,
	    (event->data.mouse.buttons&ST_CONSOLE_MOUSE_BUTTON_LEFT)!=0);
	if (event->data.mouse.action==ST_CONSOLE_MOUSE_MOVE)
		st_rip3_session_mouse_move(rip->session,event->data.mouse.pixel_x,event->data.mouse.pixel_y);
	present(rip);flush_text(rip);feedback_end(rip);return handled;
}

bool
st_term_rip3_key(struct st_term_rip3 *rip, const struct st_console_event *event)
{
	if (rip==NULL || !st_rip3_session_mouse_active(rip->session)) return false;
	uint8_t byte;uint32_t scalar;
	switch (event->type) {
	case ST_CONSOLE_EVENT_TEXT:
		scalar=event->data.text.codepoint;
		if (!st_codepage_from_unicode(&byte,ST_CODEPAGE_CP437,scalar)) return false;
		break;
	case ST_CONSOLE_EVENT_CODEPAGE_BYTE:
		scalar=event->data.codepage_byte;
		if (rip->config.encoding==ST_CTERM_ENCODING_CODEPAGE)
			scalar=st_codepage_to_unicode(rip->config.codepage,event->data.codepage_byte,false);
		if (!st_codepage_from_unicode(&byte,ST_CODEPAGE_CP437,scalar)) return false;
		break;
	case ST_CONSOLE_EVENT_KEY_CODE: {
		unsigned code=event->data.key.code;uint32_t modifiers=event->data.key.modifiers;
		if (code>127 || (modifiers&(ST_CONSOLE_MODIFIER_ALT|ST_CONSOLE_MODIFIER_GUI))!=0) return false;
		if ((modifiers&ST_CONSOLE_MODIFIER_CONTROL)!=0) {
			if (code>='a' && code<='z') code-=32;
			if (code>='@' && code<='_') code&=31;
			else if (code==' ') code=0;
			else if (code=='?') code=127;
		}
		byte=(uint8_t)code;break;
	}
	default:
		/* Backends deliver the translated key/text event separately. Raw
		 * physical notifications (including key-up) never double-activate.
		 * Semantic arrows/functions remain on the normal encoding path. */
		return false;
	}
	/* Unlike per-byte wire batching, keyboard feedback must reach the
	 * console during its callback and be restored by the field owner. */
	st_term_rip3_flush(rip);
	bool handled=st_rip3_session_key(rip->session,byte);
	if (handled) {present(rip);flush_text(rip);}
	return handled;
}

void
st_term_rip3_mouse_cancel(struct st_term_rip3 *rip)
{
	if (rip!=NULL) st_rip3_session_pointer_boundary(rip->session);
}

void
st_term_rip3_mouse_ignore(struct st_term_rip3 *rip, const struct st_console_event *event)
{
	if (rip==NULL || event->type!=ST_CONSOLE_EVENT_MOUSE) return;
	mouse_capture(rip,event->data.mouse.pixel_x,event->data.mouse.pixel_y);
	(void)st_rip3_session_mouse(rip->session,-1,-1,(event->data.mouse.buttons&ST_CONSOLE_MOUSE_BUTTON_LEFT)!=0);
	st_rip3_session_pointer_boundary(rip->session);
}

void
st_term_rip3_poll(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
	if (rip->mouse_feedback_applied && rip->position==rip->length && rip->tty_length==0
	    && !st_rip3_session_pending(rip->session)) return;
	feedback_begin(rip);process(rip,true);flush_text(rip);feedback_end(rip);
}

bool
st_term_rip3_graphical_pending(const struct st_term_rip3 *rip)
{
	return rip!=NULL && st_rip3_session_graphical_pending(rip->session);
}

void
st_term_rip3_graphical_failure(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
	st_rip3_session_graphical_cancel(rip->session);
	diagnostic(rip,ST_RIP3_COMMAND_RESOURCE_FAILURE);
}

bool
st_term_rip3_paused(const struct st_term_rip3 *rip)
{
	return rip!=NULL && (rip->position<rip->length || st_rip3_session_pending(rip->session)
	    || st_rip3_session_wait_ns(rip->session)!=0);
}

void
st_term_rip3_write(struct st_term_rip3 *rip, const uint8_t *bytes, size_t length)
{
	if (length>SIZE_MAX-rip->length) {diagnostic(rip,ST_RIP3_COMMAND_RESOURCE_FAILURE);return;}
	if (rip->length+length>rip->capacity) {
		size_t capacity=rip->length+length;
		if (capacity<4096) capacity=4096;
		uint8_t *queue=realloc(rip->queue,capacity);
		if (queue==NULL) {diagnostic(rip,ST_RIP3_COMMAND_RESOURCE_FAILURE);return;}
		rip->queue=queue;rip->capacity=capacity;
	}
	if (length!=0) memcpy(rip->queue+rip->length,bytes,length);
	rip->length+=length;
	/* The transport dispatches individual bytes to preserve its pause and
	 * interception boundaries. Publish at its batch flush, replies, text or
	 * explicit view operations, rather than snapshotting after each command. */
	feedback_begin(rip);process(rip,false);
	/* Keep feedback off until the transport's batch flush. Incoming bytes are
	 * dispatched individually; repainting a held region for every byte would
	 * turn a transient mouse effect into the rendering bottleneck. */
	--rip->mouse_feedback_depth;
}

void
st_term_rip3_cancel(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
	st_rip3_session_mouse_cancel(rip->session);
	flush_text(rip);
	if (rip->view!=NULL) {
		(void)st_cterm_finish(st_term_rip3_text(rip));
		if (st_rip3_session_text_active(rip->session))
			(void)st_term_rip3_view_present(rip->view,rip->config.console,true);
	}
	present(rip);st_rip3_session_cancel(rip->session);
	rip->position=rip->length=0;
	if (rip->escape_length!=0) {ansi(rip,rip->escape,rip->escape_length);rip->escape_length=0;}
}

void
st_term_rip3_destroy(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
	st_term_rip3_cancel(rip);
	if (rip->ready && rip->variable_path!=NULL) {
		int status=fs_create_directories(rip->config.rip_directory);
		if (status==0) status=st_rip3_variables_save_file(rip->persistent_values,rip->variable_path);
		if (status!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	}
	st_rip3_session_destroy(rip->session);
	st_rip3_audio_destroy(rip->audio);
	for (unsigned i=0;i<36;i++) st_term_rip3_view_destroy(rip->windows[i]);
	st_rip3_user_variables_destroy(rip->user);
	st_rip3_variables_destroy(rip->session_values);
	st_rip3_variables_destroy(rip->persistent_values);
	st_pixel_buffer_destroy(rip->previous);
	st_pixel_buffer_destroy(rip->mouse_saved);
	free(rip->variable_path);free(rip->queue);free(rip->damage);free(rip);
}

int
st_term_rip3_create(struct st_term_rip3 **result, const struct st_term_config *config,
    st_cterm_t *ansi, const struct st_cterm_config *text_config)
{
	struct st_term_rip3 *rip=calloc(1,sizeof(*rip));
	if (rip==NULL) return IO_ERROR_OUT_OF_MEMORY;
	rip->config=*config;rip->text=ansi;rip->text_config=*text_config;
	rip->text_config.palette_mapping=NULL;rip->text_config.palette_override=NULL;
	rip->output=text_config->output;rip->output_context=text_config->output_context;
	uint32_t width,height;
	int status=st_console_pixel_size(config->console,&width,&height);
	if (status!=0) {st_term_rip3_destroy(rip);return status;}
	rip->pixels=(size_t)width*height;
	rip->damage=calloc(rip->pixels,1);
	if (rip->damage==NULL) status=IO_ERROR_OUT_OF_MEMORY;
	/* Thirty-six full-canvas index planes is the session's offscreen policy. */
	rip->pixel_budget=UINT64_C(36)*width*height;
	rip->resources=(struct st_rip3_resource_paths){.host=config->rip_directory,.fonts=config->rip_directory};
	rip->identity=(struct st_rip3_terminal_identity){"SyncTERM","Stephen Hurd",
	    config->application_version==NULL ? "" : config->application_version};
	rip->colors=256;
	rip->local=(struct st_rip3_local_queries){.context=rip,.modifier=modifier,.colors=&rip->colors};
	(void)cterm_rip_localtime(time(NULL),&rip->local_time);
	(void)st_rip3_features_set(&rip->features,ST_RIP3_FEATURE_ANSI,true);
	(void)st_rip3_features_set(&rip->features,ST_RIP3_FEATURE_RIPSCRIP,true);
	(void)st_rip3_features_set(&rip->features,ST_RIP3_FEATURE_BMP,true);
	(void)st_rip3_features_set(&rip->features,ST_RIP3_FEATURE_JPEG,st_pixel_image_jpeg_supported());
	if (config->audio!=NULL) {
		audio_engine_t *engine=NULL;
		if (st_audio_runtime_acquire(config->audio,&engine)==0
		    && st_rip3_audio_create(&rip->audio,engine,0,0)==0)
			(void)st_rip3_features_set(&rip->features,ST_RIP3_FEATURE_WAV,true);
	}
	if (status==0) status=st_rip3_variables_create(&rip->session_values);
	if (status==0 && config->rip_directory!=NULL && config->rip_directory[0]!='\0') {
		size_t length=strlen(config->rip_directory)+sizeof("/rip3-variables.bin");
		rip->variable_path=malloc(length);
		if (rip->variable_path==NULL) status=IO_ERROR_OUT_OF_MEMORY;
		else {
			(void)snprintf(rip->variable_path,length,"%s/rip3-variables.bin",config->rip_directory);
			int loaded=st_rip3_variables_load_file(&rip->persistent_values,rip->variable_path);
			if (loaded!=0 && loaded!=IO_ERROR_NOT_FOUND) {
				diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
				/* Retain an unreadable/corrupt database for inspection. */
				free(rip->variable_path);rip->variable_path=NULL;
			}
		}
	}
	if (status==0 && rip->persistent_values==NULL) status=st_rip3_variables_create(&rip->persistent_values);
	const struct st_rip3_user_variable_host user={.session=rip->session_values,.persistent=rip->persistent_values};
	if (status==0) status=st_rip3_user_variables_create(&rip->user,&user);
	const struct st_rip3_host_variables services={.features=&rip->features,.identity=&rip->identity,
	    .local_time=&rip->local_time,.local=&rip->local,.user_variables=rip->user};
	const struct st_rip3_session_config session={
	    .canvas=width==1024 ? ST_RIP3_CANVAS_1024_768 : width==800 ? ST_RIP3_CANVAS_800_600 : ST_RIP3_CANVAS_640_480,
	    .device_width=width,.device_height=height,
	    .default_text_font=0,.default_text_wrap=true,
	    .encoding=ST_RIP3_TEXT_UTF8,.graphical_codepage_converted=config->encoding==ST_CTERM_ENCODING_CODEPAGE,
	    .source={rip,now,next},
	    .sink={.context=rip,.text=text,.reply=reply,.at_line_start=line_start,.diagnostic=diagnostic,
	        .before_command=before_command,.text_window=text_window,.text_select=text_select,.text_reset=text_reset,.text_snapshot=text_snapshot,.text_cursor=text_cursor,
	        .text_cursor_visible=text_cursor_visible,.text_erase=text_erase,.text_erase_eol=text_erase_eol,
	        .mouse_invert=mouse_invert,.mouse_capture=mouse_capture,.mouse_redraw=mouse_redraw},
	    .services=&services,.random_seed=(uint32_t)now(rip),.resources=&rip->resources,
	    .offscreen_pixel_budget=&rip->pixel_budget,.audio=rip->audio,.builtin_bitmap_errors=true};
	if (status==0) status=st_rip3_session_create(&rip->session,&session);
	struct st_rip3_session_capture capture={0};
	if (status==0) status=st_rip3_session_capture(rip->session,&capture);
	if (status==0) rip->previous=capture.pixels;
	if (status==0) status=st_rip3_session_damage(rip->session,rip->damage,rip->pixels);
	/* Start ANSI before the first graphics command; its lazy startup clears
	 * the text window. It must never clear a subsequently drawn RIP scene. */
	if (status==0) status=st_cterm_start_text(ansi);
	if (status!=0) {st_term_rip3_destroy(rip);return status;}
	rip->ready=true;refresh_pointer(rip);*result=rip;return 0;
}
