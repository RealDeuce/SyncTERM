/* RIP3 transport, display, resource, prompt and mixer integration. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_term_rip3.h"
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
	uint8_t tty[4096];
	size_t tty_length;
	bool flushing;
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
	uint64_t pixel_budget, presented;
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
present(struct st_term_rip3 *rip)
{
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
text_visibility(struct st_term_rip3 *rip)
{
	bool active=st_rip3_session_text_active(rip->session);
	if (rip->view!=NULL) return st_term_rip3_view_active(rip->view,rip->config.console,active);
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

void
st_term_rip3_flush(struct st_term_rip3 *rip)
{
	if (rip!=NULL) flush_text(rip);
}

int
st_term_rip3_begin_local_text(struct st_term_rip3 *rip)
{
	if (rip==NULL || rip->flushing) return 0;
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
	st_term_rip3_view_destroy(rip->view);rip->view=view;
	if (rip->text_config.size_changed!=NULL)
		rip->text_config.size_changed(rip->text_config.size_context,layout->columns,layout->rows,
		    layout->columns*layout->cell_width,layout->rows*layout->cell_height);
	return 0;
}

static void
before_command(void *context)
{
	struct st_term_rip3 *rip=context;
	flush_text(rip);
	int status=rip->view!=NULL && st_rip3_session_text_active(rip->session)
	    ? st_term_rip3_view_begin(rip->view,rip->config.console) : 0;
	if (status==0) status=st_cterm_text_boundary(st_term_rip3_text(rip));
	if (status==0 && rip->view!=NULL && st_rip3_session_text_active(rip->session))
		status=st_term_rip3_view_present(rip->view,rip->config.console,true);
	if (status!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
}

static int
text_cursor(void *context, const struct st_rip3_text_cursor_position *position)
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
text_erase(void *context, bool whole, uint32_t rgb, struct st_rip3_rect *area)
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
text_erase_eol(void *context)
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
text_cursor_visible(void *context, bool visible)
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
	if (supported && rip->config.rip_prompt!=NULL)
		status=rip->config.rip_prompt(rip->config.rip_prompt_context,
		    reference->has_question ? (const char *)reference->question.bytes : reference->name,
		    view.initial==NULL ? "" : (const char *)view.initial->bytes,
		    reference->has_width ? reference->width : 255,false,&value);
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
process(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
	(void)cterm_rip_localtime(time(NULL),&rip->local_time);
	st_rip3_session_process(rip->session,4096);
	present(rip);prompt(rip);present(rip);
	if (text_visibility(rip)!=0) diagnostic(rip,ST_RIP3_COMMAND_OPERATION_FAILED);
	if (rip->position==rip->length) rip->position=rip->length=0;
}

void
st_term_rip3_poll(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
	process(rip);flush_text(rip);
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
	process(rip);
}

void
st_term_rip3_cancel(struct st_term_rip3 *rip)
{
	if (rip==NULL) return;
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
	st_term_rip3_view_destroy(rip->view);
	st_rip3_user_variables_destroy(rip->user);
	st_rip3_variables_destroy(rip->session_values);
	st_rip3_variables_destroy(rip->persistent_values);
	st_pixel_buffer_destroy(rip->previous);
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
	    .encoding=ST_RIP3_TEXT_UTF8,.source={rip,now,next},
	    .sink={.context=rip,.text=text,.reply=reply,.at_line_start=line_start,.diagnostic=diagnostic,
	        .before_command=before_command,.text_window=text_window,.text_cursor=text_cursor,
	        .text_cursor_visible=text_cursor_visible,.text_erase=text_erase,.text_erase_eol=text_erase_eol},
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
	rip->ready=true;*result=rip;return 0;
}
