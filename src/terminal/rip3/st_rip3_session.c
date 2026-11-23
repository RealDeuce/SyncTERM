/* The single-threaded owner of the separate RIP3 engine's represented state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_session.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_header.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_mouse.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_backup.h"
#include "st_rip3_screen.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_templates.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_stream.h"
#include "st_rip3_query.h"
#include "st_rip3_graphical.h"
#include "st_rip3_text_metric.h"
#include "st_rip3_button_render.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_table_host.h"
#include "st_rip3_audio.h"
#include "st_rip3_bitmap_error_ui.h"
#include "st_rip3_text_window.h"
#include "st_rip3_resident_query.h"
#include "st_rip3_refresh.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdlib.h>

enum graphical_job { GRAPHICAL_NONE, GRAPHICAL_TEXT, GRAPHICAL_METRIC };

struct session_window {
	struct st_rip3_text_window_info info;
	bool has_layout;
	struct st_rip3_text_window_layout layout;
	struct st_rip3_resident_query query;
};

struct st_rip3_session {
	struct st_rip3_session_source source;
	struct st_rip3_session_sink sink;
	struct st_rip3_command_host commands;
	struct st_rip3_host_variables variables;
	struct st_rip3_host_variables refresh_variables;
	struct st_rip3_refresh refresh;
	struct st_rip3_table_host tables;
	struct st_rip3_screen screen;
	struct st_rip3_host_sink host_sink;
	struct st_rip3_text_context text;
	struct st_rip3_graphical *graphical;
	struct st_rip3_text_argument text_argument;
	enum graphical_job graphical_job;
	struct st_rip3_metric_argument metric_argument;
	struct st_rip3_metric_state metric;
	bool text_retained;
	bool graphical_codepage_converted;
	struct st_rip3_template_host templates;
	struct st_rip3_clipboard_selection clipboard;
	struct st_rip3_random random;
	struct st_rip3_field_context field_context;
	bool field_program;
	int field_program_id;
	struct st_rip3_frame frame;
	struct st_rip3_stream *stream;
	struct st_rip3_variables *session_values,*persistent_values;
	struct st_rip3_user_variables *owned_user;
	bool ready;
	bool text_active, input_locked;
	unsigned scene_revision, scene_charset;
	unsigned current_window;
	unsigned default_text_font;
	bool default_text_wrap;
	struct session_window windows[36];
	struct st_rip3_resident_query floating_window_query;
	struct session_window ansi_window;
	void *ansi_native;
	struct st_rip3_text_window_host window_host;
	struct st_rip3_text_window_operations window_operations;
	uint64_t revision;
	struct st_rip3_bitmap_error_ui bitmap_ui;
	struct st_rip3_bitmap_error_sink bitmap_errors;
};

static void
diagnose(void *context, enum st_rip3_command_diagnostic kind)
{
	struct st_rip3_session *session=context;
	if (session->sink.diagnostic!=NULL) session->sink.diagnostic(session->sink.context,kind);
}

static void
table_operation_failed(void *context,int status)
{
	diagnose(context,status==IO_ERROR_OUT_OF_MEMORY ? ST_RIP3_COMMAND_RESOURCE_FAILURE : ST_RIP3_COMMAND_OPERATION_FAILED);
}

static void
host_diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	enum st_rip3_command_diagnostic mapped=kind==ST_RIP3_HOST_RESOURCE_FAILURE ? ST_RIP3_COMMAND_RESOURCE_FAILURE
	    : kind==ST_RIP3_HOST_INVALID_REFERENCE ? ST_RIP3_COMMAND_INVALID
	    : kind==ST_RIP3_HOST_UNSUPPORTED_REFERENCE || kind==ST_RIP3_HOST_UNKNOWN_REFERENCE
	    ? ST_RIP3_COMMAND_UNSUPPORTED : ST_RIP3_COMMAND_OPERATION_FAILED;
	diagnose(context,mapped);
}

static void
user_diagnostic(void *context, enum st_rip3_user_variable_diagnostic kind)
{
	diagnose(context,kind==ST_RIP3_USER_VARIABLE_RESOURCE_FAILED ? ST_RIP3_COMMAND_RESOURCE_FAILURE
	    : kind==ST_RIP3_USER_VARIABLE_INVALID ? ST_RIP3_COMMAND_INVALID : ST_RIP3_COMMAND_OPERATION_FAILED);
}

static void
stream_diagnostic(void *context, enum st_rip3_stream_diagnostic kind)
{
	diagnose(context,kind==ST_RIP3_STREAM_RESOURCE_FAILURE ? ST_RIP3_COMMAND_RESOURCE_FAILURE : ST_RIP3_COMMAND_INVALID);
}

static void
text(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_session *session=context;
	if (session->sink.text!=NULL) session->sink.text(session->sink.context,bytes,length);
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_session *session=context;
	if (session->sink.reply!=NULL) session->sink.reply(session->sink.context,bytes,length);
}

static bool
line_start(void *context)
{
	struct st_rip3_session *session=context;
	return session->text_active && session->sink.at_line_start!=NULL && session->sink.at_line_start(session->sink.context);
}

static int
define_text_window(void *context, const struct st_rip3_text_window_layout *layout)
{
	struct st_rip3_session *session=context;
	if (session->sink.text_window==NULL) return IO_ERROR_NOT_SUPPORTED;
	unsigned number=session->current_window;
	if (session->windows[number].info.protected) return IO_ERROR_PERMISSION_DENIED;
	bool same=session->windows[number].has_layout && st_rip3_text_window_same(&session->windows[number].layout,layout);
	uint32_t clear_rgb=0;int status=0;
	if (layout->extended && (layout->flags&16)) {
		struct st_rip3_palette palette;
		status=st_rip3_palettes_current_palette(session->commands.palettes,&palette);
		if (status==0) clear_rgb=palette.rgb[0];
	}
	if (status==0) status=session->sink.text_window(session->sink.context,layout,same,clear_rgb);
	if (status==0) {
		session->windows[number].layout=*layout;session->windows[number].has_layout=true;
		session->text_active=!layout->extended || !(layout->flags&4);
		if (layout->extended && (layout->flags&16)) {
			struct st_rip3_rect area;
			status=st_rip3_text_window_bounds(layout,session->frame.device_width,session->frame.device_height,&area);
			if (status==0 && area.x0<=area.x1 && area.y0<=area.y1)
				status=st_rip3_ports_screen_fill(session->commands.ports,&area,0);
		}
		/* The working TW0 cannot be protected. This ordered request does
		 * not undo the completed definition, clear or activation flags. */
		if (layout->extended && (layout->flags&32)) {
			if (number==0) diagnose(session,ST_RIP3_COMMAND_OPERATION_FAILED);
			else session->windows[number].info.protected=true;
		}
	}
	return status;
}

static int
deactivate_window(void *context)
{
	struct st_rip3_session *session=context;
	if (session->windows[session->current_window].info.protected) return IO_ERROR_PERMISSION_DENIED;
	session->text_active=false;return 0;
}

static int
window_info(void *context, unsigned number, struct st_rip3_text_window_info *result)
{
	struct st_rip3_session *session=context;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	*result=session->windows[number].info;
	result->extended=session->windows[number].has_layout && session->windows[number].layout.extended;
	if (number==session->current_window) result->active=session->text_active;
	return 0;
}

static int
window_snapshot(void *context, unsigned number, struct st_rip3_text_window_snapshot *result)
{
	struct st_rip3_session *session=context;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (session->sink.text_snapshot==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_text_window_snapshot value;
	int status=session->sink.text_snapshot(session->sink.context,number,&value);
	if (status!=0) return status;
	value.font_known=session->windows[number].has_layout;
	if (value.font_known) value.layout=session->windows[number].layout;
	*result=value;return 0;
}

void
st_rip3_session_ansi_window(struct st_rip3_session *session,bool restore)
{
	if (session==NULL) return;
	struct session_window *current=&session->windows[session->current_window];
	if (!current->info.defined || !session->text_active || (restore && current->info.protected)) return;
	if (session->sink.text_ansi_capture==NULL || session->sink.text_ansi_restore==NULL
	    || session->sink.text_ansi_destroy==NULL) return;
	if (restore && session->ansi_native==NULL) return;
	struct session_window prepared=restore ? session->ansi_window : *current;
	prepared.query=(struct st_rip3_resident_query){0};
	int status=st_rip3_resident_query_copy(&prepared.query,restore ? &session->ansi_window.query : &current->query);
	void *native=NULL;
	if (status==0 && !restore) {
		status=session->sink.text_ansi_capture(session->sink.context,current->has_layout,&native);
		if (status==0 && native==NULL) status=IO_ERROR_INVALID_FORMAT;
	}
	if (status==0 && restore) status=session->sink.text_ansi_restore(session->sink.context,session->ansi_native);
	if (status!=0) {
		st_rip3_resident_query_clear(&prepared.query);session->sink.text_ansi_destroy(native);
		table_operation_failed(session,status);return;
	}
	prepared.info.active=true;
	if (restore) {
		if (session->current_window==0) {prepared.info.defined=true;prepared.info.protected=false;}
		st_rip3_resident_query_clear(&current->query);*current=prepared;session->text_active=true;
	}
	else {
		st_rip3_resident_query_clear(&session->ansi_window.query);
		session->sink.text_ansi_destroy(session->ansi_native);
		session->ansi_window=prepared;session->ansi_native=native;
	}
}

struct window_save {
	unsigned current;bool active;
	struct session_window windows[36];
	struct st_rip3_resident_query floating;
	void *native;void (*native_destroy)(void *);
};
static void
destroy_window_save(void *object)
{
	struct window_save *save=object;if (save==NULL) return;
	for (unsigned i=0;i<36;i++) st_rip3_resident_query_clear(&save->windows[i].query);
	st_rip3_resident_query_clear(&save->floating);
	if (save->native!=NULL && save->native_destroy!=NULL) save->native_destroy(save->native);
	free(save);
}
static int
copy_window_records(struct window_save *save,const struct session_window windows[36],
    const struct st_rip3_resident_query *floating)
{
	int status=0;
	for (unsigned i=0;status==0 && i<36;i++) {
		save->windows[i]=windows[i];save->windows[i].query=(struct st_rip3_resident_query){0};
		status=st_rip3_resident_query_copy(&save->windows[i].query,&windows[i].query);
	}
	if (status==0) status=st_rip3_resident_query_copy(&save->floating,floating);
	return status;
}
static int
capture_window_save(void *context,void **result)
{
	struct st_rip3_session *session=context;
	if (session->sink.text_backup_capture==NULL || session->sink.text_backup_restore==NULL || session->sink.text_backup_destroy==NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct window_save *save=calloc(1,sizeof(*save));if (save==NULL) return IO_ERROR_OUT_OF_MEMORY;
	save->current=session->current_window;save->active=session->text_active;
	int status=copy_window_records(save,session->windows,&session->floating_window_query);
	save->windows[save->current].info.active=save->active;
	bool defined[36];for (unsigned i=0;i<36;i++) defined[i]=save->windows[i].info.defined;
	save->native_destroy=session->sink.text_backup_destroy;
	if (status==0) status=session->sink.text_backup_capture(session->sink.context,defined,&save->native);
	if (status!=0 || save->native==NULL) {destroy_window_save(save);return status!=0 ? status : IO_ERROR_INVALID_FORMAT;}
	*result=save;return 0;
}
static int
restore_window_save(void *context,const void *object)
{
	struct st_rip3_session *session=context;const struct window_save *saved=object;
	if (session->sink.text_backup_restore==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct window_save *prepared=calloc(1,sizeof(*prepared));if (prepared==NULL) return IO_ERROR_OUT_OF_MEMORY;
	int status=copy_window_records(prepared,saved->windows,&saved->floating);
	if (status==0) status=session->sink.text_backup_restore(session->sink.context,saved->native,saved->current,saved->active);
	if (status!=0) {destroy_window_save(prepared);return status;}
	for (unsigned i=0;i<36;i++) {
		st_rip3_resident_query_clear(&session->windows[i].query);session->windows[i]=prepared->windows[i];
		prepared->windows[i].query=(struct st_rip3_resident_query){0};
	}
	st_rip3_resident_query_clear(&session->floating_window_query);session->floating_window_query=prepared->floating;
	prepared->floating=(struct st_rip3_resident_query){0};
	session->current_window=saved->current;session->text_active=saved->active;
	destroy_window_save(prepared);return 0;
}

static int
copy_window_entries(void *context,unsigned source,const unsigned *destinations,size_t count)
{
	struct st_rip3_session *session=context;
	if (destinations==NULL || count==0) return IO_ERROR_INVALID_ARGUMENT;
	if (source>=36) return IO_ERROR_OUT_OF_RANGE;
	if (!session->windows[source].info.defined) return IO_ERROR_NOT_FOUND;
	for (size_t i=0;i<count;i++) {
		unsigned number=destinations[i];
		if (number>=36) return IO_ERROR_OUT_OF_RANGE;
		if (number==source) return IO_ERROR_INVALID_ARGUMENT;
		if (session->windows[number].info.protected) return IO_ERROR_PERMISSION_DENIED;
	}
	if (session->sink.text_copy==NULL) return IO_ERROR_NOT_SUPPORTED;
	bool active=source==session->current_window ? session->text_active : session->windows[source].info.active;
	for (size_t i=0;i<count;i++) {
		unsigned number=destinations[i];
		int status=session->sink.text_copy(session->sink.context,source,number,active);
		if (status!=0) {table_operation_failed(session,status);continue;}
		st_rip3_resident_query_clear(&session->windows[number].query);
		session->windows[number]=session->windows[source];
		session->windows[number].query=(struct st_rip3_resident_query){0};
		session->windows[number].info.active=active;
		if (number==0) session->windows[number].info.protected=false;
		if (number==session->current_window) session->text_active=active;
		/* The shipped copy commits the record before allocating its query;
		 * failure leaves an empty destination query and the caller continues. */
		status=st_rip3_resident_query_copy(&session->windows[number].query,&session->windows[source].query);
		if (status!=0) table_operation_failed(session,status);
	}
	return 0;
}

static int
protect_window(void *context, unsigned number, bool protect)
{
	struct st_rip3_session *session=context;
	if (number>=36 || (number==0 && protect)) return IO_ERROR_OUT_OF_RANGE;
	if (session->windows[number].info.defined) session->windows[number].info.protected=protect;
	return 0;
}

static int
window_resident_query(void *context,bool floating,unsigned number,const uint8_t *bytes,size_t length,bool converted)
{
	struct st_rip3_session *session=context;
	if (floating) return st_rip3_resident_query_set(&session->floating_window_query,bytes,length,converted);
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (!session->windows[number].info.defined) return IO_ERROR_NOT_FOUND;
	if (session->windows[number].info.protected) return IO_ERROR_PERMISSION_DENIED;
	return st_rip3_resident_query_set(&session->windows[number].query,bytes,length,converted);
}

static int
select_text_window(struct st_rip3_session *session, unsigned number, bool clone)
{
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (number==session->current_window) return 0;
	if (session->sink.text_select==NULL) return IO_ERROR_NOT_SUPPORTED;
	unsigned previous=session->current_window;
	bool first=!session->windows[number].info.defined;
	struct st_rip3_text_window_layout fresh;
	bool defaults=first && !clone;
	int status=defaults ? st_rip3_text_window_normal(&fresh,&session->frame,0,0,UINT32_MAX,UINT32_MAX,
	    session->default_text_wrap,session->default_text_font) : 0;
	if (status!=0) return status;
	bool active=defaults || (first ? session->text_active : session->windows[number].info.active);
	struct st_rip3_resident_query resident={0};
	if (first && clone) status=st_rip3_resident_query_copy(&resident,&session->windows[previous].query);
	if (status==0) status=session->sink.text_select(session->sink.context,number,first,active,defaults ? &fresh : NULL);
	if (status!=0) {st_rip3_resident_query_clear(&resident);return status;}
	session->windows[previous].info.active=session->text_active;
	if (first) {
		if (clone) {session->windows[number]=session->windows[previous];session->windows[number].query=resident;}
		else {
			session->windows[number].layout=fresh;session->windows[number].has_layout=true;
			session->windows[number].info=(struct st_rip3_text_window_info){.defined=true,.active=true};
		}
		session->windows[number].info.protected=false;
	}
	session->current_window=number;session->text_active=active;
	return 0;
}

static int
select_window(void *context, unsigned number)
{
	return select_text_window(context,number,true);
}

static int
select_default_window(void *context, unsigned number)
{
	return select_text_window(context,number,false);
}

static int
reset_scene(struct st_rip3_session *session, bool hard)
{
	if (session->sink.text_reset==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_text_window_layout layout;
	int status=st_rip3_text_window_normal(&layout,&session->frame,0,0,UINT32_MAX,UINT32_MAX,
	    session->default_text_wrap,session->default_text_font);
	if (status!=0) return status;
	status=st_rip3_templates_preserve(session->templates.slots);
	if (status!=0) return status;
	/* Full-scene reset invalidates retained text and any old suspended
	 * graphical consumer. Table/viewport operations never use this path. */
	st_rip3_graphical_cancel(session->graphical);session->graphical_job=GRAPHICAL_NONE;
	session->metric=(struct st_rip3_metric_state){0};
	/* These services neither replace nor clear their independent save areas.
	 * The physical black clear precedes PAL reset and keeps PAL selection. */
	st_rip3_mouse_kill(session->commands.mouse);
	st_rip3_mouse_clear_queries(session->commands.mouse);
	st_rip3_refresh_clear(&session->refresh);
	status=st_rip3_ports_reset(session->commands.ports,hard);
	if (status==0 && session->clipboard.set) {
		struct st_rip3_port_info info;
		status=st_rip3_ports_info(session->commands.ports,session->clipboard.port,&info);
		if (status==0 && !info.defined) session->clipboard=(struct st_rip3_clipboard_selection){0};
	}
	if (status==0) status=st_rip3_ports_screen_clear_black(session->commands.ports);
	if (status==0) status=st_rip3_palettes_reset(session->commands.palettes,hard);
	if (status==0) status=st_rip3_styles_soft_reset(session->commands.styles);
	if (status==0) status=st_rip3_button_styles_reset_table(session->commands.button_styles);
	if (status==0) status=st_rip3_button_styles_select(session->commands.button_styles,0);
	if (status==0) status=st_rip3_environments_soft_reset(session->commands.environments);
	if (status!=0) return status;
	st_rip3_outline_cache_reset(session->text.outline_cache);
	bool keep[36]={false};
	for (unsigned i=1;i<36;i++) keep[i]=!hard && session->windows[i].info.protected;
	status=session->sink.text_reset(session->sink.context,&layout,keep,session->text_active,hard);
	if (status!=0) return status;
	session->windows[session->current_window].info.active=session->text_active;
	for (unsigned i=1;i<36;i++) if (!keep[i]) {
		st_rip3_resident_query_clear(&session->windows[i].query);
		session->windows[i].info=(struct st_rip3_text_window_info){0};
		session->windows[i].has_layout=false;
	}
	st_rip3_resident_query_clear(&session->windows[0].query);
	st_rip3_resident_query_clear(&session->floating_window_query);
	session->windows[0].info=(struct st_rip3_text_window_info){.defined=true,.active=true};
	session->windows[0].layout=layout;session->windows[0].has_layout=true;
	session->current_window=0;session->text_active=true;
	/* Query/template/variable owners and the borrowed audio player are separate
	 * from live drawing state. Do not cancel the query invoking RESET(SOFT). */
	return 0;
}

static int
soft_reset(void *context)
{
	return reset_scene(context, false);
}

static void
set_input_lock(struct st_rip3_session *session, bool locked)
{
	if (locked && !session->input_locked)
		st_rip3_mouse_boundary(session->commands.mouse);
	session->input_locked = locked;
}

static int
hard_reset(void *context)
{
	struct st_rip3_session *session = context;
	/* Acceptance clears the interpreter save even if scene setup fails. */
	if (session->ansi_native != NULL)
		session->sink.text_ansi_destroy(session->ansi_native);
	session->ansi_native = NULL;
	st_rip3_resident_query_clear(&session->ansi_window.query);
	session->ansi_window = (struct session_window){0};
	st_rip3_refresh_clear(&session->refresh);
	set_input_lock(session, false);
	session->scene_charset = 0;
	for (unsigned i = 0; i < ST_RIP3_TABLE_COUNT; ++i)
		(void)st_rip3_backup_clear_all(session->tables.tables[i].backup, true);
	for (unsigned i = 1; i < 36; ++i) {
		(void)st_rip3_button_styles_protect(session->commands.button_styles, i, false);
		(void)st_rip3_styles_protect(session->commands.styles, i, false);
		(void)st_rip3_environments_protect(session->commands.environments, i, false);
	}
	if (session->commands.audio != NULL) {
		int status = st_rip3_audio_stop(session->commands.audio);
		if (status != 0) table_operation_failed(session, status);
	}
	return reset_scene(session, true);
}

static int
reset_window_entry(void *context,unsigned number)
{
	struct st_rip3_session *session=context;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (!session->windows[number].info.defined || session->windows[number].info.protected) return 0;
	if (session->sink.text_reset_entry==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_text_window_layout fresh;
	int status=number==0 ? st_rip3_text_window_normal(&fresh,&session->frame,0,0,UINT32_MAX,UINT32_MAX,
	    session->default_text_wrap,session->default_text_font) : 0;
	if (status==0) status=session->sink.text_reset_entry(session->sink.context,number,
	    number==0 ? &fresh : NULL,session->windows[0].info.active);
	if (status!=0) return status;
	/* Native preparation has committed. No allocation/provider follows it,
	 * so a failed target cannot publish half a definition or lose its query. */
	st_rip3_resident_query_clear(&session->windows[number].query);
	session->windows[number]=(struct session_window){0};
	if (number==0) {
		session->windows[0].info=(struct st_rip3_text_window_info){.defined=true,.active=true};
		session->windows[0].layout=fresh;session->windows[0].has_layout=true;
		if (session->current_window==0) session->text_active=true;
	}
	else if (session->current_window==number) {
		session->current_window=0;session->text_active=session->windows[0].info.active;
	}
	return 0;
}

static bool
window_active(struct st_rip3_session *session, unsigned number)
{
	return session->windows[number].info.defined
	    && (number==session->current_window ? session->text_active : session->windows[number].info.active);
}

static int
position_window_cursor(void *context, unsigned number, const struct st_rip3_text_cursor_position *position)
{
	struct st_rip3_session *session=context;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	if (!window_active(session,number)) return 0;
	return session->sink.text_cursor==NULL ? IO_ERROR_NOT_SUPPORTED
	    : session->sink.text_cursor(session->sink.context,number,position);
}

static int
position_text_cursor(void *context, uint32_t column, uint32_t row)
{
	const struct st_rip3_text_cursor_position position={true,true,column,row};
	struct st_rip3_session *session=context;
	return position_window_cursor(context,session->current_window,&position);
}

static int
show_window_cursors(void *context, const unsigned *numbers, size_t count, bool visible)
{
	struct st_rip3_session *session=context;
	for (size_t i=0;i<count;i++) if (numbers[i]>=36) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0;i<count;i++) if (window_active(session,numbers[i])) {
		int status=session->sink.text_cursor_visible==NULL ? IO_ERROR_NOT_SUPPORTED
		    : session->sink.text_cursor_visible(session->sink.context,numbers[i],visible);
		if (status!=0) return status;
	}
	return 0;
}

static int
erase_window(struct st_rip3_session *session, unsigned number, bool whole)
{
	if (!window_active(session,number)) return 0;
	if (session->sink.text_erase==NULL) return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_palette palette;uint8_t ink=0;
	int status=st_rip3_palettes_current_palette(session->commands.palettes,&palette);
	if (!whole && status==0) {
		struct st_rip3_environment environment;struct st_rip3_style style;unsigned number;
		status=st_rip3_environments_current(session->commands.environments,&number);
		if (status==0) status=st_rip3_environments_get(session->commands.environments,number,&environment);
		if (status==0) status=st_rip3_styles_current(session->commands.styles,&number);
		if (status==0) status=st_rip3_styles_get(session->commands.styles,number,&style);
		if (status==0) status=st_rip3_palettes_color_bits(session->commands.palettes,
		    environment.rgb_bits,style.background_color,&ink);
	}
	struct st_rip3_rect area={0,0,-1,-1};
	if (status==0) status=session->sink.text_erase(session->sink.context,number,whole,palette.rgb[ink],&area);
	if (status==0 && area.x0<=area.x1 && area.y0<=area.y1)
		status=st_rip3_ports_screen_fill(session->commands.ports,&area,ink);
	return status;
}

static int
erase_text_window(void *context, bool whole)
{
	struct st_rip3_session *session=context;
	return erase_window(session,session->current_window,whole);
}

static int
erase_text_windows(void *context, const unsigned *numbers, size_t count)
{
	for (size_t i=0;i<count;i++) if (numbers[i]>=36) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0;i<count;i++) {
		int status=erase_window(context,numbers[i],true);
		if (status!=0) return status;
	}
	return 0;
}

static int
erase_window_lines(void *context, const unsigned *numbers, size_t count)
{
	struct st_rip3_session *session=context;
	for (size_t i=0;i<count;i++) if (numbers[i]>=36) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0;i<count;i++) if (window_active(session,numbers[i])) {
		int status=session->sink.text_erase_eol==NULL ? IO_ERROR_NOT_SUPPORTED
		    : session->sink.text_erase_eol(session->sink.context,numbers[i]);
		if (status!=0) return status;
	}
	return 0;
}

static int
current_text_window(void *context, unsigned *number)
{
	struct st_rip3_session *session=context;*number=session->current_window;return 0;
}

static int
activate_text_windows(void *context, const unsigned *numbers, size_t count, bool active)
{
	struct st_rip3_session *session=context;
	for (size_t i=0;i<count;i++) if (numbers[i]>=36) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i=0;i<count;i++) if (session->windows[numbers[i]].info.defined
	    && !session->windows[numbers[i]].info.protected) {
		session->windows[numbers[i]].info.active=active;
		if (numbers[i]==session->current_window) session->text_active=active;
	}
	return 0;
}

bool
st_rip3_session_text_active(const struct st_rip3_session *session)
{
	return session!=NULL && session->text_active;
}

static void
selection_required(void *context, unsigned group)
{
	struct st_rip3_session *session=context;
	if (session->sink.selection_required!=NULL) session->sink.selection_required(session->sink.context,group);
}

static uint64_t
now(void *context)
{
	struct st_rip3_session *session=context;
	return session->source.now_ns(session->source.context);
}

static bool
next(void *context, uint8_t *byte)
{
	struct st_rip3_session *session=context;
	return session->source.next(session->source.context,byte);
}

static void
consume(void *context, uint8_t byte)
{
	struct st_rip3_session *session=context;
	st_rip3_stream_process(session->stream,&byte,1);
}

static bool
prepare(void *context)
{
	struct st_rip3_session *session=context;
	bool pending=st_rip3_session_pending(session);
	struct st_rip3_field_context previous=session->field_context;
	if (session->field_program) session->field_context=(struct st_rip3_field_context){true,session->field_program_id};
	/* An active refresh is a separate reply. Resume it before the
	 * expression that requested it and before reading the next command. */
	st_rip3_query_process(session->refresh.query, 64);
	st_rip3_query_process(session->commands.query,64);
	session->field_context=previous;
	if (!st_rip3_query_pending(session->commands.query)) session->field_program=false;
	if (session->graphical_job) {
		bool retained=session->graphical_job==GRAPHICAL_METRIC ? session->metric_argument.retained : session->text_retained;
		if (!retained) st_rip3_graphical_process(session->graphical,64);
		if (!st_rip3_graphical_pending(session->graphical)) {
			const struct st_rip3_condition_bytes *result=st_rip3_graphical_result(session->graphical);
			if (retained || result!=NULL) {
				bool operation=false;
				const struct st_rip3_command_sink sink={session,diagnose};
				int status;
				if (session->graphical_job==GRAPHICAL_METRIC) {
					operation=true;
					status=st_rip3_metric_complete(&session->metric,&session->commands,&session->metric_argument,
					    retained ? NULL : result->bytes,retained ? 0 : result->length,
					    session->graphical_codepage_converted,&session->host_sink);
				}
				else status=st_rip3_text_commands_expanded(&session->commands,&session->text_argument,
				    retained ? session->metric.bytes : result->bytes,retained ? session->metric.length : result->length,&operation);
				st_rip3_command_result(&sink,status,operation);
			}
			st_rip3_graphical_cancel(session->graphical);session->graphical_job=GRAPHICAL_NONE;
		}
	}
	if (!st_rip3_query_pending(session->commands.query) && !session->graphical_job
	    && !st_rip3_refresh_pending(&session->refresh)) {
		st_rip3_button_renderer_process(session->commands.buttons,64);
		st_rip3_mouse_resume(session->commands.mouse);
	}
	if (pending) ++session->revision;
	return !st_rip3_session_pending(session);
}

static void
header_result(struct st_rip3_session *session, int status)
{
	if (status != 0) {
		const struct st_rip3_command_sink sink = {session, diagnose};
		st_rip3_command_result(&sink, status, true);
	}
}

static void
header_backups(struct st_rip3_session *session, enum st_rip3_table_type family, unsigned flags)
{
	struct st_rip3_backup *backup = session->tables.tables[family].backup;
	if (flags & 1)
		header_result(session, st_rip3_backup_clear(backup, ST_RIP3_BACKUP_BASE, 0));
	if (flags & 2) {
		struct st_rip3_backup_info info;
		int status = st_rip3_backup_info(backup, &info);
		if (status == 0) {
			for (unsigned i = 0; i < 10; ++i) {
				if (info.slot_used[i] && !info.slot_protected[i]) {
					status = st_rip3_backup_clear(backup, ST_RIP3_BACKUP_SLOT, i);
					break;
				}
			}
		}
		header_result(session, status);
	}
	if (flags & 4)
		header_result(session, st_rip3_backup_clear(backup, ST_RIP3_BACKUP_STACK, 0));
}

static void
header_viewports(struct st_rip3_session *session, unsigned flags)
{
	unsigned windows[36], current = 0;
	for (unsigned i = 0; i < 36; ++i) windows[i] = i;
	struct st_rip3_ports *ports = session->commands.ports;
	(void)st_rip3_ports_current(ports, &current);
	if (flags & 128) header_result(session, st_rip3_ports_operate_views(ports, windows, 36, ST_RIP3_VIEW_DEACTIVATE, 0));
	if (flags & 256) header_result(session, st_rip3_ports_operate_views(ports, &current, 1, ST_RIP3_VIEW_DEACTIVATE, 0));
	if (flags & 512) header_result(session, st_rip3_ports_operate_views(ports, windows, 36, ST_RIP3_VIEW_ACTIVATE, 0));
	if (flags & 1024) header_result(session, st_rip3_ports_operate_views(ports, &current, 1, ST_RIP3_VIEW_ACTIVATE, 0));
	/* Like MVP, HEADER bounds reset skips inactive viewports. RESET(VIEW)
	 * also changes activation and is a different operation. */
	if (flags & 2048) header_result(session, st_rip3_ports_operate_views(ports, windows, 36, ST_RIP3_VIEW_MAXIMIZE, 0));
	if (flags & 4096) header_result(session, st_rip3_ports_operate_views(ports, &current, 1, ST_RIP3_VIEW_MAXIMIZE, 0));
	if (flags & 8192) header_result(session, st_rip3_ports_operate_views(ports, windows, 36, ST_RIP3_PORT_CLEAR_BLACK, 0));
	if (flags & 16384) header_result(session, st_rip3_ports_operate_views(ports, &current, 1, ST_RIP3_PORT_CLEAR_BLACK, 0));
	if (flags & 32768) st_rip3_ports_header_clear_queries(ports);
}

static void
header_window_geometry(struct st_rip3_session *session, unsigned number)
{
	struct session_window *window = &session->windows[number];
	if (!window_active(session, number) || window->info.protected) return;
	if (session->sink.text_maximize == NULL) {
		header_result(session, IO_ERROR_NOT_SUPPORTED);
		return;
	}
	struct st_rip3_text_window_snapshot snapshot;
	int status = window_snapshot(session, number, &snapshot);
	if (status == 0) {
		/* The entry console can have no MicroANSI mode yet. Use the default
		 * mode's nominal grid with its existing metrics and font binding. */
		if (!snapshot.font_known) snapshot.layout.font = session->default_text_font;
		status = st_rip3_text_window_maximize(&snapshot.layout);
	}
	if (status == 0) status = session->sink.text_maximize(session->sink.context, number, &snapshot.layout);
	if (status == 0 && window->has_layout) window->layout = snapshot.layout;
	header_result(session, status);
}

static void
header_text_windows(struct st_rip3_session *session, unsigned flags, unsigned slot)
{
	if (flags & 1) {
		for (unsigned i = 1; i < 36; ++i)
			header_result(session, reset_window_entry(session, i));
		header_result(session, select_text_window(session, 0, false));
		header_window_geometry(session, 0);
	}
	if (flags & 2) header_result(session, select_text_window(session, slot, true));
	if (flags & 4) {
		for (unsigned i = 1; i < 36; ++i)
			if (i != session->current_window)
				header_result(session, reset_window_entry(session, i));
		if (session->current_window != 0)
			header_window_geometry(session, 0);
	}
	header_backups(session, ST_RIP3_TABLE_TW, flags >> 3);
	unsigned all[36], current = session->current_window;
	for (unsigned i = 0; i < 36; ++i) all[i] = i;
	if (flags & 64) (void)activate_text_windows(session, all, 36, false);
	if (flags & 128) (void)activate_text_windows(session, &current, 1, false);
	if (flags & 256) (void)activate_text_windows(session, all, 36, true);
	if (flags & 512) (void)activate_text_windows(session, &current, 1, true);
	if (flags & 1024)
		for (unsigned i = 0; i < 36; ++i) header_window_geometry(session, i);
	if (flags & 2048) header_window_geometry(session, current);
	if (flags & 4096)
		for (unsigned i = 0; i < 36; ++i) header_result(session, erase_window(session, i, true));
	if (flags & 8192) header_result(session, erase_window(session, current, true));
	if (flags & 16384) {
		st_rip3_resident_query_clear(&session->floating_window_query);
		for (unsigned i = 0; i < 36; ++i)
			if (!session->windows[i].info.protected)
				st_rip3_resident_query_clear(&session->windows[i].query);
	}
	if (flags & 32768)
		for (unsigned i = 0; i < 36; ++i) header_result(session, show_window_cursors(session, &i, 1, false));
	if (flags & 65536) header_result(session, show_window_cursors(session, &current, 1, false));
	if (flags & 131072)
		for (unsigned i = 0; i < 36; ++i) header_result(session, show_window_cursors(session, &i, 1, true));
	if (flags & 262144) header_result(session, show_window_cursors(session, &current, 1, true));
}

static void
header_segment(struct st_rip3_session *session, const struct st_rip3_header_segment *segment)
{
	struct st_rip3_command_host *host = &session->commands;
	unsigned flags = segment->flags, slot = segment->slot, current = 0;
	switch (segment->type) {
	case 0:
		if (flags & 1) {
			(void)st_rip3_button_styles_select(host->button_styles, 0);
			st_rip3_button_styles_header_prune(host->button_styles, 0);
		}
		if (flags & 2) header_result(session, st_rip3_button_styles_select(host->button_styles, slot));
		(void)st_rip3_button_styles_current(host->button_styles, &current);
		if (flags & 4) st_rip3_button_styles_header_prune(host->button_styles, current);
		header_backups(session, ST_RIP3_TABLE_BUT, flags >> 3);
		if (flags & 192) st_rip3_button_styles_header_defaults(host->button_styles, (flags & 128) != 0);
		break;
	case 1:
		if (flags & 1) {
			(void)st_rip3_styles_switch(host->styles, 0);
			st_rip3_styles_header_prune(host->styles, 0);
		}
		if (flags & 2) header_result(session, st_rip3_styles_switch(host->styles, slot));
		(void)st_rip3_styles_current(host->styles, &current);
		if (flags & 4) st_rip3_styles_header_prune(host->styles, current);
		header_backups(session, ST_RIP3_TABLE_STYLE, flags >> 3);
		if (flags & 192) st_rip3_styles_header_defaults(host->styles, (flags & 128) != 0);
		break;
	case 2:
		if (flags & 1) header_result(session, st_rip3_ports_header_prune(host->ports, false));
		if (flags & 2) header_result(session, st_rip3_ports_switch(host->ports, slot));
		if (flags & 4) header_result(session, IO_ERROR_NOT_SUPPORTED);
		if (flags & 8) header_result(session, st_rip3_ports_header_prune(host->ports, true));
		header_backups(session, ST_RIP3_TABLE_PORT, flags >> 4);
		header_viewports(session, flags);
		break;
	case 3:
		header_text_windows(session, flags, slot);
		break;
	case 4:
		if (flags & 1) {
			(void)st_rip3_palettes_switch(host->palettes, 0);
			st_rip3_palettes_header_prune(host->palettes, 0);
		}
		if (flags & 2) header_result(session, st_rip3_palettes_switch(host->palettes, slot));
		(void)st_rip3_palettes_current(host->palettes, &current);
		if (flags & 4) st_rip3_palettes_header_prune(host->palettes, current);
		header_backups(session, ST_RIP3_TABLE_PAL, flags >> 3);
		/* Active indexed pixels already use the current palette on presentation.
		 * The undocumented pre-array guard for bit 128 has no safe state. */
		if (flags & 128) header_result(session, IO_ERROR_NOT_SUPPORTED);
		break;
	case 5:
		st_rip3_environments_header(host->environments, flags & 7, slot, 0, 0);
		header_backups(session, ST_RIP3_TABLE_ENV, flags >> 3);
		st_rip3_environments_header(host->environments, flags & ~63u, slot, segment->coordinate_width, segment->rgb_bits);
		break;
	case 6:
		if (flags & 3) {
			const struct st_rip3_rect all = {0, 0, INT32_MAX, INT32_MAX};
			const unsigned remove = (flags & 3) == 3 ? 7 : (flags & 1) ? 15 : 23;
			header_result(session, st_rip3_mouse_remove(host->mouse, &all, remove, NULL));
		}
		header_backups(session, ST_RIP3_TABLE_MOUSE, flags >> 2);
		break;
	case 7:
		if ((flags & 1) && host->audio != NULL)
			header_result(session, st_rip3_audio_stop(host->audio));
		break;
	case 8:
		if (flags & 1) header_result(session, st_rip3_ports_screen_clear_black(host->ports));
		header_backups(session, ST_RIP3_TABLE_SCREEN, flags >> 1);
		break;
	case 9:
		session->scene_charset = flags;
		break;
	}
}

static void
scene_header(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	struct st_rip3_header header;
	int status = st_rip3_header_decode(&header, bytes, length);
	if (status != 0) {
		diagnose(session, ST_RIP3_COMMAND_INVALID);
		return;
	}
	if (header.flags & ST_RIP3_HEADER_HARD) header_result(session, hard_reset(session));
	else if (header.flags & ST_RIP3_HEADER_SOFT) header_result(session, soft_reset(session));
	session->scene_revision = header.revision;
	set_input_lock(session, false);
	if (header.flags & ST_RIP3_HEADER_LOCK_INPUT) set_input_lock(session, true);
	if (header.flags & ST_RIP3_HEADER_SELECT_ZERO) {
		header_result(session, select_text_window(session, 0, false));
		header_result(session, st_rip3_button_styles_select(session->commands.button_styles, 0));
		header_result(session, st_rip3_styles_switch(session->commands.styles, 0));
		header_result(session, st_rip3_ports_switch(session->commands.ports, 0));
		header_result(session, st_rip3_palettes_switch(session->commands.palettes, 0));
		header_result(session, st_rip3_environments_activate(session->commands.environments, 0));
	}
	if (header.flags & ST_RIP3_HEADER_CLEAR_REFRESH) st_rip3_refresh_clear(&session->refresh);
	for (unsigned i = 0; i < header.count; ++i)
		header_segment(session, &header.segments[i]);
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_session *session=context;
	if (session->sink.before_command!=NULL) session->sink.before_command(session->sink.context);
	const struct st_rip3_command_sink sink={session,diagnose};
	if (length != 0 && bytes[0] == 'h') scene_header(session, bytes, length);
	else if (length != 0 && bytes[0] == '#') {
		if (length == 1) set_input_lock(session, false);
		else diagnose(session, ST_RIP3_COMMAND_INVALID);
	}
	else if (length!=0 && (bytes[0]=='T' || bytes[0]=='@')) {
		size_t offset;
		int status=st_rip3_text_argument_prepare(&session->commands,bytes,length,&session->text_argument,&offset);
		if (status!=0) st_rip3_command_result(&sink,status,false);
		else {
			session->text_retained=bytes[0]=='@' && st_rip3_metric_sentinel(bytes+offset,length-offset);
			if (!session->text_retained)
				st_rip3_graphical_start(session->graphical,ST_RIP3_GRAPHICAL_TEXT,bytes+offset,length-offset);
			session->graphical_job=GRAPHICAL_TEXT;
		}
	}
	else if (length!=0 && bytes[0]=='r') {
		size_t offset;
		int status=st_rip3_metric_argument_prepare(&session->commands,bytes,length,&session->metric_argument,&offset);
		if (status!=0) st_rip3_command_result(&sink,status,false);
		else {
			if (!session->metric_argument.retained)
				st_rip3_graphical_start(session->graphical,ST_RIP3_GRAPHICAL_METRIC,bytes+offset,length-offset);
			session->graphical_job=GRAPHICAL_METRIC;
		}
	}
	else st_rip3_commands_process(&session->commands,&sink,bytes,length);
	++session->revision;
}

uint64_t
st_rip3_session_revision(const struct st_rip3_session *session)
{
	return session==NULL ? 0 : session->revision;
}

int
st_rip3_session_damage(struct st_rip3_session *session, uint8_t *damage, size_t length)
{
	return session==NULL ? IO_ERROR_INVALID_ARGUMENT
	    : st_rip3_ports_screen_damage(session->commands.ports,damage,length);
}

void
st_rip3_session_cancel(struct st_rip3_session *session)
{
	if (session==NULL) return;
	session->input_locked = false;
	st_rip3_button_renderer_cancel(session->commands.buttons);
	st_rip3_mouse_abort(session->commands.mouse);
	st_rip3_query_cancel(session->commands.query);
	st_rip3_query_cancel(session->refresh.query);
	st_rip3_graphical_cancel(session->graphical);session->graphical_job=GRAPHICAL_NONE;
	session->field_program=false;
	st_rip3_input_reset(session->commands.input);
	st_rip3_stream_reset(session->stream);
	if (session->ready && session->commands.audio!=NULL) (void)st_rip3_audio_stop(session->commands.audio);
}

void
st_rip3_session_destroy(struct st_rip3_session *session)
{
	if (session==NULL) return;
	st_rip3_session_cancel(session);
	/* Query destruction precedes every borrowed processor/store/table and the
	 * input gate. Saves own independent snapshots and precede live-table teardown. */
	st_rip3_query_destroy(session->commands.query);
	st_rip3_query_destroy(session->refresh.query);
	st_rip3_refresh_clear(&session->refresh);
	for (unsigned i=0; i<ST_RIP3_TABLE_COUNT; i++) st_rip3_backup_destroy(session->tables.tables[i].backup);
	st_rip3_mouse_destroy(session->commands.mouse);
	st_rip3_button_renderer_destroy(session->commands.buttons);
	st_rip3_graphical_destroy(session->graphical);
	st_rip3_input_destroy(session->commands.input);
	st_rip3_stream_destroy(session->stream);
	st_rip3_user_variables_destroy(session->owned_user);
	st_rip3_variables_destroy(session->session_values);st_rip3_variables_destroy(session->persistent_values);
	st_rip3_templates_destroy(session->templates.slots);
	st_rip3_outline_cache_destroy(session->text.outline_cache);
	st_rip3_environments_destroy(session->commands.environments);
	st_rip3_styles_destroy(session->commands.styles);
	st_rip3_button_styles_destroy(session->commands.button_styles);
	st_rip3_palettes_destroy(session->commands.palettes);
	st_rip3_ports_destroy(session->commands.ports);
	for (unsigned i=0;i<36;i++) st_rip3_resident_query_clear(&session->windows[i].query);
	st_rip3_resident_query_clear(&session->floating_window_query);
	st_rip3_resident_query_clear(&session->ansi_window.query);
	if (session->sink.text_ansi_destroy!=NULL) session->sink.text_ansi_destroy(session->ansi_native);
	free(session);
}

static void
mouse_invert(void *context, const struct st_rip3_rect *area)
{
	struct st_rip3_session *session=context;
	if (session->sink.mouse_invert!=NULL) session->sink.mouse_invert(session->sink.context,area);
}

static void
mouse_resource_failure(void *context)
{
	diagnose(context,ST_RIP3_COMMAND_RESOURCE_FAILURE);
}

static void
mouse_render_failure(void *context)
{
	diagnose(context,ST_RIP3_COMMAND_OPERATION_FAILED);
}

static void
mouse_changed(void *context)
{
	struct st_rip3_session *session=context;
	++session->revision;
	if (session->sink.mouse_redraw!=NULL) session->sink.mouse_redraw(session->sink.context);
}

static void
mouse_activate(void *context, const uint8_t *program, size_t length, bool reset, int identity, bool pointer, int32_t x, int32_t y)
{
	struct st_rip3_session *session=context;
	if (st_rip3_query_pending(session->commands.query) || session->graphical_job
	    || st_rip3_button_renderer_pending(session->commands.buttons)) {
		diagnose(session,ST_RIP3_COMMAND_OPERATION_FAILED);return;
	}
	if (reset && soft_reset(session)!=0) {diagnose(session,ST_RIP3_COMMAND_OPERATION_FAILED);return;}
	if (pointer && session->sink.mouse_capture!=NULL) session->sink.mouse_capture(session->sink.context,x,y);
	struct st_rip3_field_context previous=session->field_context;
	session->field_context=(struct st_rip3_field_context){true,identity};
	int status=st_rip3_query_start_owned(session->commands.query,program,length,st_rip3_mouse_callback_owner(session->commands.mouse));
	session->field_context=previous;
	session->field_program=status==0 && st_rip3_query_pending(session->commands.query);
	session->field_program_id=identity;
	if (status!=0) diagnose(session,status==IO_ERROR_OUT_OF_MEMORY
	    ? ST_RIP3_COMMAND_RESOURCE_FAILURE : ST_RIP3_COMMAND_OPERATION_FAILED);
	++session->revision;
}

static bool
mouse_busy(void *context)
{
	struct st_rip3_session *session=context;
	return session->graphical_job || st_rip3_query_pending(session->commands.query)
	    || st_rip3_refresh_pending(&session->refresh) || st_rip3_button_renderer_pending(session->commands.buttons);
}

bool
st_rip3_session_input_locked(const struct st_rip3_session *session)
{
	return session != NULL && session->input_locked;
}

bool
st_rip3_session_mouse_active(const struct st_rip3_session *session)
{
	return session!=NULL && (session->input_locked || st_rip3_mouse_active(session->commands.mouse));
}

bool
st_rip3_session_mouse_pointer(const struct st_rip3_session *session,unsigned *result)
{
	if (session==NULL || result==NULL) return false;
	if (session->input_locked) { *result = 6; return true; }
	unsigned current;struct st_rip3_environment env;
	if (st_rip3_environments_current(session->commands.environments,&current)!=0
	    || st_rip3_environments_get(session->commands.environments,current,&env)!=0) return false;
	*result=env.mouse_pointer;return true;
}

bool
st_rip3_session_mouse(struct st_rip3_session *session, int32_t x, int32_t y, bool left)
{
	return session!=NULL && (session->input_locked || st_rip3_mouse_event(session->commands.mouse,x,y,left));
}

void
st_rip3_session_mouse_move(struct st_rip3_session *session,int32_t x,int32_t y)
{
	if (session!=NULL && !session->input_locked) st_rip3_mouse_move(session->commands.mouse,x,y);
}

bool
st_rip3_session_key(struct st_rip3_session *session, uint8_t byte)
{
	return session!=NULL && (session->input_locked || st_rip3_mouse_key(session->commands.mouse,byte));
}

uint64_t
st_rip3_session_press_token(const struct st_rip3_session *session)
{
	return session==NULL ? 0 : st_rip3_mouse_press_token(session->commands.mouse);
}

void
st_rip3_session_pointer_boundary(struct st_rip3_session *session)
{
	if (session!=NULL) st_rip3_mouse_boundary(session->commands.mouse);
}

void
st_rip3_session_mouse_cancel(struct st_rip3_session *session)
{
	if (session!=NULL) st_rip3_mouse_cancel_silent(session->commands.mouse);
}

int
st_rip3_session_create(struct st_rip3_session **result, const struct st_rip3_session_config *config)
{
	if (result==NULL || config==NULL || (config->graphical_codepage_converted && config->encoding!=ST_RIP3_TEXT_UTF8)
	    || config->source.now_ns==NULL || config->source.next==NULL
	    || config->default_text_font>=5
	    || (config->encoding!=ST_RIP3_TEXT_CP437 && config->encoding!=ST_RIP3_TEXT_UTF8)) return IO_ERROR_INVALID_ARGUMENT;
	if ((config->sink.screen_capture==NULL)!=(config->sink.screen_restore==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	bool text_ansi=config->sink.text_ansi_capture!=NULL;
	if ((config->sink.text_ansi_restore!=NULL)!=text_ansi || (config->sink.text_ansi_destroy!=NULL)!=text_ansi)
		return IO_ERROR_INVALID_ARGUMENT;
	bool text_backup=config->sink.text_backup_capture!=NULL;
	if ((config->sink.text_backup_restore!=NULL)!=text_backup || (config->sink.text_backup_destroy!=NULL)!=text_backup)
		return IO_ERROR_INVALID_ARGUMENT;
	if (config->services!=NULL && (config->services->frame!=NULL || config->services->tables!=NULL
	    || config->services->input!=NULL || config->services->templates!=NULL
	    || config->services->text_windows!=NULL || config->services->mouse!=NULL
	    || config->services->field_context!=NULL || config->services->refresh!=NULL)) return IO_ERROR_INVALID_ARGUMENT;
	bool device=config->device_width!=0 || config->device_height!=0;
	struct st_rip3_frame frame;
	int status=device ? st_rip3_frame_init_device(&frame,config->device_width,config->device_height)
	    : st_rip3_frame_init(&frame,config->canvas);
	if (status!=0) return status;
	struct st_rip3_session *session=calloc(1,sizeof(*session));
	if (session==NULL) return IO_ERROR_OUT_OF_MEMORY;
	session->source=config->source;session->sink=config->sink;session->frame=frame;
	session->commands.reset_context=session;session->commands.soft_reset=soft_reset;
	session->tables.reset_context=session;session->tables.soft_reset=soft_reset;
	session->tables.hard_reset=hard_reset;
	session->default_text_font=config->default_text_font;session->default_text_wrap=config->default_text_wrap;
	session->text_active=true;session->commands.text_active=&session->text_active;
	session->windows[0].info=(struct st_rip3_text_window_info){.defined=true,.active=true};
	session->window_host=(struct st_rip3_text_window_host){.context=session,.define=define_text_window,
	    .position=position_text_cursor,.erase=erase_text_window,.select=select_window,.deactivate=deactivate_window,
	    .resident_query=window_resident_query};
	session->commands.text_window=&session->window_host;
	session->commands.audio=config->audio;session->commands.resources=config->resources;
	session->commands.offscreen_pixel_budget=config->offscreen_pixel_budget;session->commands.bitmap_errors=config->bitmap_errors;
	session->commands.clipboard=&session->clipboard;session->commands.text=&session->text;
	session->text.encoding=config->encoding;session->text.font=config->classic_font;session->text.outline_font=config->outline_font;
	session->graphical_codepage_converted=config->graphical_codepage_converted;
	session->commands.codepage_converted=config->graphical_codepage_converted;
	if (config->services!=NULL) session->variables=*config->services;
	session->variables.frame=&session->frame;session->variables.tables=&session->tables;
	session->variables.field_context=&session->field_context;
	session->variables.refresh=&session->refresh;
	session->commands.refresh=&session->refresh;
	session->window_operations=(struct st_rip3_text_window_operations){.context=session,
	    .current=current_text_window,.select=select_default_window,.info=window_info,.snapshot=window_snapshot,
	    .protect=protect_window,.activate=activate_text_windows,
	    .position=position_window_cursor,.cursor=show_window_cursors,.erase=erase_text_windows,
	    .erase_eol=erase_window_lines,.copy_entries=copy_window_entries,.reset_entry=reset_window_entry,
	    .backup_capture=capture_window_save,.backup_restore=restore_window_save,
	    .backup_destroy=destroy_window_save};
	session->variables.text_windows=&session->window_operations;
	session->tables.tables[ST_RIP3_TABLE_TW].live=&session->window_operations;
	session->random.state=config->random_seed;
	if (session->variables.random==NULL) session->variables.random=&session->random;
	session->templates=(struct st_rip3_template_host){.context=session,.selection_required=selection_required};
	session->variables.templates=&session->templates;
	/* A missing client reply sink stays unsupported in query_start, before
	 * active host actions. Do not install a pretend successful discard sink. */
	session->host_sink=(struct st_rip3_host_sink){session,config->sink.reply!=NULL ? reply : NULL,host_diagnostic};
	struct st_rip3_style initial;status=st_rip3_style_defaults(&initial);
	if (status==0) status=device ? st_rip3_environments_create_device(&session->commands.environments,frame.device_width,frame.device_height)
	    : st_rip3_environments_create(&session->commands.environments,config->canvas);
	if (status==0) status=st_rip3_styles_create(&session->commands.styles,&initial);
	if (status==0) status=st_rip3_button_styles_create(&session->commands.button_styles);
	const struct st_rip3_mouse_sink mouse_sink={.context=session,.busy=mouse_busy,
	    .invert=config->sink.mouse_invert!=NULL ? mouse_invert : NULL,
	    .activate=mouse_activate,.resource_failure=mouse_resource_failure,
	    .render_failure=mouse_render_failure,.changed=mouse_changed};
	if (status==0) status=st_rip3_mouse_create(&session->commands.mouse,&mouse_sink);
	session->variables.mouse=session->commands.mouse;
	if (status==0) status=st_rip3_palettes_create(&session->commands.palettes);
	if (status==0) status=device ? st_rip3_ports_create_device(&session->commands.ports,frame.device_width,frame.device_height)
	    : st_rip3_ports_create(&session->commands.ports,config->canvas);
	if (status==0) st_rip3_ports_bind_clipboard(session->commands.ports,&session->clipboard);
	if (status==0) st_rip3_ports_bind_screen_reader(session->commands.ports,config->sink.context,config->sink.screen_read);
	if (config->builtin_bitmap_errors && config->bitmap_errors==NULL) {
		session->bitmap_ui=(struct st_rip3_bitmap_error_ui){.ports=session->commands.ports,.palettes=session->commands.palettes};
		session->bitmap_errors=(struct st_rip3_bitmap_error_sink){&session->bitmap_ui,st_rip3_bitmap_error_show};
		session->commands.bitmap_errors=&session->bitmap_errors;
	}
	if (status==0) status=st_rip3_environments_backup_create(&session->tables.tables[ST_RIP3_TABLE_ENV].backup);
	if (status==0) status=st_rip3_styles_backup_create(&session->tables.tables[ST_RIP3_TABLE_STYLE].backup);
	if (status==0) status=st_rip3_button_styles_backup_create(&session->tables.tables[ST_RIP3_TABLE_BUT].backup);
	if (status==0) status=st_rip3_palettes_backup_create(&session->tables.tables[ST_RIP3_TABLE_PAL].backup);
	if (status==0) status=st_rip3_ports_backup_create(&session->tables.tables[ST_RIP3_TABLE_PORT].backup);
	if (status==0) status=st_rip3_mouse_backup_create(&session->tables.tables[ST_RIP3_TABLE_MOUSE].backup);
	if (status==0) status=st_rip3_screen_backup_create(&session->tables.tables[ST_RIP3_TABLE_SCREEN].backup);
	if (status==0) status=st_rip3_text_window_backup_create(&session->tables.tables[ST_RIP3_TABLE_TW].backup);
	session->screen=(struct st_rip3_screen){.ports=session->commands.ports,.palettes=session->commands.palettes,
	    .context=session->sink.context,.capture=session->sink.screen_capture,.restore=session->sink.screen_restore};
	session->tables.tables[ST_RIP3_TABLE_SCREEN].live=&session->screen;
	session->tables.operation_context=session;session->tables.operation_failed=table_operation_failed;
	session->tables.tables[ST_RIP3_TABLE_MOUSE].live=session->commands.mouse;
	session->tables.tables[ST_RIP3_TABLE_ENV].live=session->commands.environments;
	session->tables.tables[ST_RIP3_TABLE_STYLE].live=session->commands.styles;
	session->tables.tables[ST_RIP3_TABLE_BUT].live=session->commands.button_styles;
	session->tables.tables[ST_RIP3_TABLE_PAL].live=session->commands.palettes;
	session->tables.tables[ST_RIP3_TABLE_PORT].live=session->commands.ports;
	if (status==0) status=st_rip3_outline_cache_create(&session->text.outline_cache);
	if (status==0) status=st_rip3_templates_create(&session->templates.slots);
	if (status==0) st_rip3_mouse_bind_templates(session->commands.mouse,session->templates.slots);
	if (status==0 && session->variables.user_variables==NULL) {
		status=st_rip3_variables_create(&session->session_values);
		if (status==0) status=st_rip3_variables_create(&session->persistent_values);
		const struct st_rip3_user_variable_host user={.session=session->session_values,.persistent=session->persistent_values,
		    .context=session,.diagnostic=user_diagnostic};
		if (status==0) status=st_rip3_user_variables_create(&session->owned_user,&user);
		session->variables.user_variables=session->owned_user;
	}
	const struct st_rip3_input_source source={session,now,next,consume,prepare};
	if (status==0) status=st_rip3_input_create(&session->commands.input,&source);
	session->variables.input=session->commands.input;
	if (status==0) status=st_rip3_query_create(&session->commands.query,&session->variables,&session->host_sink);
	session->refresh_variables = session->variables;
	/* Refresh uses ordinary host expansion without template slots. Keep the
	 * same stores, delay gate and client reply sink for both evaluators. */
	session->refresh_variables.templates = NULL;
	if (status == 0)
		status = st_rip3_query_create(&session->refresh.query, &session->refresh_variables, &session->host_sink);
	if (status==0) status=st_rip3_graphical_create(&session->graphical,&session->variables,&session->host_sink,config->graphical_codepage_converted);
	const struct st_rip3_command_sink button_sink={session,diagnose};
	if (status==0) status=st_rip3_button_renderer_create(&session->commands.buttons,&session->commands,&button_sink,
	    session->graphical,config->graphical_codepage_converted);
	const struct st_rip3_stream_sink sink={.context=session,.text=text,.command=command,.at_line_start=line_start,.diagnostic=stream_diagnostic};
	if (status==0) status=st_rip3_stream_create(&session->stream,&sink);
	if (status!=0) {st_rip3_session_destroy(session);return status;}
	session->ready=true;*result=session;return 0;
}

void
st_rip3_session_process(struct st_rip3_session *session, size_t budget)
{
	if (session!=NULL) st_rip3_input_process(session->commands.input,budget);
}

void
st_rip3_session_request_refresh(struct st_rip3_session *session)
{
	if (session == NULL || session->input_locked || st_rip3_session_pending(session))
		return;
	header_result(session, st_rip3_refresh_start(&session->refresh));
	++session->revision;
}

uint64_t
st_rip3_session_wait_ns(const struct st_rip3_session *session)
{
	return session==NULL ? 0 : st_rip3_input_wait_ns(session->commands.input);
}

bool
st_rip3_session_graphical_pending(const struct st_rip3_session *session)
{
	return session!=NULL && (session->graphical_job || st_rip3_button_renderer_pending(session->commands.buttons)
	    || st_rip3_mouse_pending(session->commands.mouse));
}

void
st_rip3_session_graphical_cancel(struct st_rip3_session *session)
{
	if (session==NULL) return;
	st_rip3_button_renderer_cancel(session->commands.buttons);
	st_rip3_graphical_cancel(session->graphical);session->graphical_job=GRAPHICAL_NONE;
	st_rip3_mouse_resume(session->commands.mouse);++session->revision;
}

bool
st_rip3_session_pending(const struct st_rip3_session *session)
{
	return session!=NULL && (st_rip3_query_pending(session->commands.query)
	    || st_rip3_refresh_pending(&session->refresh) || session->graphical_job
	    || st_rip3_button_renderer_pending(session->commands.buttons) || st_rip3_mouse_pending(session->commands.mouse));
}

bool
st_rip3_session_prompt(const struct st_rip3_session *session, struct st_rip3_session_prompt *result)
{
	if (session==NULL || result==NULL || (!st_rip3_query_prompt_pending(session->commands.query)
	    && !st_rip3_query_prompt_pending(session->refresh.query) && st_rip3_graphical_state(session->graphical)!=ST_RIP3_GRAPHICAL_PROMPT)) return false;
	const struct st_rip3_variable_reference *reference=st_rip3_user_variables_prompt(session->variables.user_variables);
	if (reference==NULL) return false;
	*result=(struct st_rip3_session_prompt){reference,st_rip3_user_variables_initial(session->variables.user_variables)};
	return true;
}

void
st_rip3_session_respond(struct st_rip3_session *session, const uint8_t *bytes, size_t length, bool cancelled)
{
	if (session==NULL) return;
	struct st_rip3_session_prompt prompt;
	if (!st_rip3_session_prompt(session,&prompt)) {diagnose(session,ST_RIP3_COMMAND_OPERATION_FAILED);return;}
	st_rip3_user_variables_respond(session->variables.user_variables,bytes,length,cancelled);
	(void)prepare(session);
	++session->revision;
}

int
st_rip3_session_capture(const struct st_rip3_session *session, struct st_rip3_session_capture *result)
{
	if (session==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_session_capture value={0};struct st_rip3_port_info port;struct st_rip3_palette palette;
	struct st_rip3_environment environment;
	int status=st_rip3_environments_current(session->commands.environments,&value.environment);
	if (status==0) status=st_rip3_environments_get(session->commands.environments,value.environment,&environment);
	if (status==0) {
		/* Observation is valid even when WORLD stores a zero dimension.
		 * Drawing still obtains a checked nonzero projection frame. */
		value.frame=session->frame;value.frame.world_width=environment.world_width;
		value.frame.world_height=environment.world_height;
	}
	if (status==0) status=st_rip3_styles_current(session->commands.styles,&value.style);
	if (status==0) status=st_rip3_ports_current(session->commands.ports,&value.port);
	if (status==0) status=st_rip3_ports_info(session->commands.ports,value.port,&port);
	if (status==0) status=st_rip3_palettes_current(session->commands.palettes,&value.palette);
	if (status==0) status=st_rip3_palettes_current_palette(session->commands.palettes,&palette);
	if (status==0) status=st_rip3_ports_snapshot(session->commands.ports,&palette,&value.pixels);
	if (status!=0) return status;
	value.cursor_x=port.cursor.x;value.cursor_y=port.cursor.y;value.viewport_active=port.viewport_active;
	value.scene_revision = session->scene_revision;
	value.scene_charset = session->scene_charset;
	value.query_pending=st_rip3_session_pending(session);value.wait_ns=st_rip3_session_wait_ns(session);
	value.outline_cache_valid=st_rip3_outline_cache_valid(session->text.outline_cache);
	*result=value;return 0;
}

void
st_rip3_session_capture_dispose(struct st_rip3_session_capture *capture)
{
	if (capture==NULL) return;
	st_pixel_buffer_destroy(capture->pixels);*capture=(struct st_rip3_session_capture){0};
}
