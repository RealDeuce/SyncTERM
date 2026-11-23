/* The single-threaded owner of the separate RIP3 engine's represented state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_session.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_host_internal.h"
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
#include "st_rip3_columns.h"
#include "st_rip3_query_internal.h"
#include "st_rip3_input.h"
#include "st_rip3_table_host.h"
#include "st_rip3_audio.h"
#include "st_rip3_file_query.h"
#include "st_rip3_resources.h"
#include "st_rip3_bitmap_error_ui.h"
#include "st_rip3_text_window.h"
#include "st_rip3_maf.h"
#include "st_rip3_resident_query.h"
#include "st_rip3_refresh.h"
#include "st_pixels.h"
#include "io_error.h"
#include "string_util.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum graphical_job {
	GRAPHICAL_NONE, GRAPHICAL_TEXT, GRAPHICAL_METRIC, GRAPHICAL_DIRECTORY, GRAPHICAL_SCENE,
	GRAPHICAL_COLUMN_SEARCH,
};

enum { SCENE_LIMIT = 8 };

struct local_scene {
	void *caller_input;
	uint32_t playback_rate, group_size, group_count;
	uint64_t group_delay_ns, group_started;
	bool waiting_for_group;
	struct st_rip3_resource file;
	struct st_rip3_stream *stream;
	/* Prepared child evaluators until start, then the suspended caller. */
	struct st_rip3_query *query, *refresh;
	bool queries_active;
	bool field_program;
	int field_program_id;
	uint8_t next_byte;
	bool byte_ready;
	bool normalized;
};

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
	struct st_rip3_columns columns;
	struct st_rip3_column_definition column_definition;
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
	struct local_scene scenes[SCENE_LIMIT];
	size_t scene_count;
	struct st_rip3_variables *session_values,*persistent_values;
	struct st_rip3_user_variables *owned_user;
	bool defining_variable, definition_reject_blank;
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
	if (kind==ST_RIP3_HOST_ABORTED) return;
	enum st_rip3_command_diagnostic mapped=kind==ST_RIP3_HOST_RESOURCE_FAILURE ? ST_RIP3_COMMAND_RESOURCE_FAILURE
	    : kind==ST_RIP3_HOST_INVALID_REFERENCE ? ST_RIP3_COMMAND_INVALID
	    : kind==ST_RIP3_HOST_UNSUPPORTED_REFERENCE || kind==ST_RIP3_HOST_UNKNOWN_REFERENCE
	    ? ST_RIP3_COMMAND_UNSUPPORTED : ST_RIP3_COMMAND_OPERATION_FAILED;
	diagnose(context,mapped);
}

static int
overflow_reference(void *context, const struct st_rip3_builtin_reference *reference,
    char *result, size_t capacity)
{
	struct st_rip3_session *session = context;
	struct st_rip3_columns *columns = &session->columns;
	struct st_rip3_overflow *overflow = &columns->overflow;
	struct st_rip3_builtin_reference words;
	int status = st_rip3_builtin_reference_words(&words, reference);
	if (status != 0)
		return status;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	result[0] = '\0';
	if (strcmp(words.name, "RESET") == 0) {
		if (words.count != 1 || ascii_case_compare(words.parameters[0].text, "OVERFLOW") != 0)
			return IO_ERROR_NOT_SUPPORTED;
		return st_rip3_columns_reset(columns, true);
	}
	if (words.count > 3)
		return IO_ERROR_INVALID_FORMAT;
	unsigned article = columns->article;
	if (words.count != 0 && ascii_case_compare(words.parameters[0].text, "CUR") != 0) {
		status = st_rip3_host_number(words.parameters[0].text, 1, ST_RIP3_ARTICLE_COUNT - 1, &article);
		if (status != 0)
			return status;
	}
	const char *operation = words.count >= 2 ? words.parameters[1].text : "CUR";
	const char *mode = words.count == 3 ? words.parameters[2].text : "";
	struct st_rip3_overflow_article *state = &overflow->articles[article];
	bool query = ascii_case_compare(mode, "QUERY") == 0;
	bool current = ascii_case_compare(operation, "CUR") == 0;
	bool page_number = ascii_case_compare(operation, "PAGE") == 0;
	bool next = ascii_case_compare(operation, "NEXT") == 0;
	bool prev = ascii_case_compare(operation, "PREV") == 0;
	int32_t frontier = st_rip3_overflow_frontier(overflow, article);
	if (current || page_number || ((next || prev) && query)) {
		if ((current || page_number) && words.count == 3 && !query)
			return IO_ERROR_INVALID_FORMAT;
		if (page_number) {
			int size = snprintf(result, capacity, "%" PRId32, state->page + 1);
			return size < 0 || (size_t)size >= capacity ? IO_ERROR_OUT_OF_RANGE : 0;
		}
		if (!state->enabled || (next && state->page >= frontier)) {
			if (capacity < sizeof("<NONE>"))
				return IO_ERROR_OUT_OF_RANGE;
			memcpy(result, "<NONE>", sizeof("<NONE>"));
			return 0;
		}
		/* The shipped PREV,QUERY returns the next filename even at page 0. */
		return st_rip3_overflow_name(overflow, article, state->page + !current, result, capacity);
	}
	if (ascii_case_compare(operation, "RESET") == 0) {
		return words.count == 2 ? st_rip3_overflow_reset_article(overflow, article)
		    : IO_ERROR_INVALID_FORMAT;
	}
	if (!state->enabled)
		return IO_ERROR_INVALID_FORMAT;
	int32_t destination;
	if (next || prev) {
		bool verbose = ascii_case_compare(mode, "SETVERBOSE") == 0;
		if (*mode != '\0' && !verbose && ascii_case_compare(mode, "SET") != 0)
			return IO_ERROR_INVALID_FORMAT;
		if ((next && state->page >= frontier) || (prev && state->page <= 0)) {
			if (verbose && session->sink.notice != NULL)
				session->sink.notice(session->sink.context, "Cannot Continue",
				    next ? "End of document." : "Beginning of document.");
			return IO_ERROR_CANCELLED;
		}
		destination = state->page + (next ? 1 : -1);
	}
	else {
		if (words.count != 2 || frontier < 0)
			return IO_ERROR_INVALID_FORMAT;
		unsigned page;
		status = st_rip3_host_number(operation, 0, (unsigned)frontier, &page);
		if (status != 0)
			return status;
		/* The original assigns the validated index through a signed word. */
		destination = (uint16_t)page;
		if (destination > INT16_MAX)
			destination -= INT32_C(65536);
	}
	status = st_rip3_overflow_close(overflow, article);
	if (status == 0)
		state->page = destination;
	return status;
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
	if (st_rip3_columns_raw_active(&session->columns)) {
		bool changed = false;
		int status = st_rip3_columns_feed(&session->columns, &session->commands, bytes, length, &changed);
		if (changed)
			++session->revision;
		const struct st_rip3_command_sink sink = {session, diagnose};
		st_rip3_command_result(&sink, status, true);
	}
	else if (session->sink.text!=NULL) session->sink.text(session->sink.context,bytes,length);
}

static void
scene_text(void *context, const uint8_t *bytes, size_t length)
{
	static const uint8_t crlf[] = {'\r', '\n'};
	if (length == 1 && bytes[0] == '\n')
		text(context, crlf, sizeof(crlf));
	else
		text(context, bytes, length);
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_session *session=context;
	if (session->sink.reply!=NULL)
		session->sink.reply(session->sink.context,bytes,length);
}

static bool
line_start(void *context)
{
	struct st_rip3_session *session=context;
	return !st_rip3_columns_raw_active(&session->columns) && session->text_active
	    && session->sink.at_line_start!=NULL && session->sink.at_line_start(session->sink.context);
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
	if (hard) {
		struct st_rip3_maf_font font;
		status=st_rip3_maf_builtin_font_exact(&font,session->frame.device_width,
		    session->frame.device_height,(enum st_rip3_maf_mode)layout.font);
		if (status==0) {
			layout.cell_width=font.width;layout.cell_height=font.height;
			layout.bound_width=(uint64_t)layout.columns*font.width;
			layout.bound_height=(uint64_t)layout.rows*font.height;
		}
		else if (status!=IO_ERROR_NOT_FOUND) return status;
		/* Remove transient feedback without evaluating a button label or
		 * retargeting an already committed field's owned response. */
		st_rip3_mouse_boundary(session->commands.mouse);
		st_rip3_button_renderer_cancel(session->commands.buttons);
	}
	else {
		status=st_rip3_templates_preserve(session->templates.slots);
		if (status!=0) return status;
	}
	/* Full-scene reset invalidates retained text and any old suspended
	 * graphical consumer. Table/viewport operations never use this path. */
	st_rip3_graphical_cancel(session->graphical);session->graphical_job=GRAPHICAL_NONE;
	session->metric=(struct st_rip3_metric_state){0};
	int overflow_status=st_rip3_columns_reset(&session->columns,hard);
	if (overflow_status!=0) table_operation_failed(session,overflow_status);
	/* These services neither replace nor clear their independent save areas.
	 * The physical black clear precedes PAL reset and keeps PAL selection. */
	st_rip3_mouse_kill(session->commands.mouse);
	st_rip3_mouse_clear_queries(session->commands.mouse);
	if (hard) st_rip3_templates_clear(session->templates.slots);
	st_rip3_refresh_clear(&session->refresh);
	status=st_rip3_ports_reset(session->commands.ports,hard);
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
	/* The invoking query keeps its working text and variable stores. */
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
	session->scene_revision = 0;
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

static void command(void *context, const uint8_t *bytes, size_t length);
static int playback(void *context, enum st_rip3_playback_kind kind, const char *filename);

static int
create_query(struct st_rip3_session *session, struct st_rip3_query **result,
    const struct st_rip3_host_variables *variables)
{
	int status = st_rip3_query_create(result, variables, &session->host_sink);
	if (status == 0) {
		st_rip3_query_set_codepage_converted(*result, session->graphical_codepage_converted);
		st_rip3_query_bind_playback(*result, session, playback);
	}
	return status;
}

static int
create_stream(struct st_rip3_session *session, struct st_rip3_stream **result, bool playback_file)
{
	const struct st_rip3_stream_sink sink={.context=session,.text=playback_file ? scene_text : text,.command=command,
	    .at_line_start=line_start,.diagnostic=stream_diagnostic,.local_playback=playback_file};
	return st_rip3_stream_create(result, &sink);
}

static uint32_t
scene_playback_group_bytes(uint32_t rate)
{
	static const struct {uint32_t limit, bytes;} groups[] = {
		{500,3}, {1000,5}, {2500,7}, {5000,10}, {7500,13}, {10000,15},
		{15000,20}, {20000,25}, {25000,30}, {30000,33}, {40000,40},
		{50000,50}, {60000,60}, {75000,75}, {90000,90}, {105000,105},
		{130000,130}, {155000,155}, {180000,180}, {250000,250}, {300000,300},
		{350000,350}, {400000,400}, {500000,500}, {1000000,1000},
		{2000000,2000}, {4000000,4000}, {10000000,10000},
	};
	for (size_t i = 0; i < sizeof(groups) / sizeof(*groups); ++i)
		if (rate < groups[i].limit)
			return groups[i].bytes;
	return 15000;
}

static void
scene_playback_rate(struct local_scene *scene, uint32_t rate)
{
	scene->playback_rate = rate;
	scene->group_size = scene_playback_group_bytes(rate);
	scene->group_count = 0;
	/* RIPtel converts ten bit times per byte to 1/16384-second ticks,
	 * truncates, and then truncates again when converting to milliseconds. */
	uint64_t ticks = UINT64_C(163840) * scene->group_size / rate;
	scene->group_delay_ns = (ticks * 1000 / 16384) * UINT64_C(1000000);
}

static int
open_scene(struct st_rip3_session *session, const char *filename, bool suspend_query)
{
	if (session->commands.resources == NULL || session->scene_count == SCENE_LIMIT)
		return 0;
	size_t length = strlen(filename);
	size_t start = st_rip3_filename_tail((const uint8_t *)filename, length);
	if (start == length)
		return 0;
	const char *name = filename + start;
	char *with_extension = NULL;
	if (strchr(name, '.') == NULL) {
		length -= start;
		with_extension = malloc(length + sizeof(".RIP"));
		if (with_extension == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		memcpy(with_extension, name, length);
		memcpy(with_extension + length, ".RIP", sizeof(".RIP"));
		name = with_extension;
	}
	struct local_scene scene = {0};
	int status = st_rip3_overflow_open(&session->columns.overflow, name, &scene.file, &scene.normalized);
	if (!scene.normalized && st_rip3_resource_name_valid(name)) {
		bool matched;
		status = st_rip3_resource_open_first(&scene.file, &matched, session->commands.resources, name);
	}
	free(with_extension);
	if (status == IO_ERROR_NOT_FOUND)
		return 0;
	if (status != 0)
		return status;
	if (!scene.normalized)
		status = create_stream(session, &scene.stream, true);
	if (status == 0 && suspend_query)
		status = create_query(session, &scene.query, &session->variables);
	if (status == 0 && suspend_query)
		status = create_query(session, &scene.refresh, &session->refresh_variables);
	if (status == 0 && session->sink.before_command != NULL)
		session->sink.before_command(session->sink.context);
	if (status == 0 && session->sink.scene_begin != NULL)
		status = session->sink.scene_begin(session->sink.context, &scene.caller_input);
	if (status == 0) {
		uint32_t rate = st_rip3_environments_playback_rate(session->commands.environments);
		if (rate != 0)
			scene_playback_rate(&scene, rate);
		session->scenes[session->scene_count++] = scene;
		return suspend_query ? IO_ERROR_IN_PROGRESS : 0;
	}
	st_rip3_query_destroy(scene.query);
	st_rip3_query_destroy(scene.refresh);
	st_rip3_stream_destroy(scene.stream);
	st_rip3_resource_dispose(&scene.file);
	return status;
}

static bool
activate_scene_queries(struct st_rip3_session *session)
{
	if (session->scene_count == 0)
		return false;
	struct local_scene *scene = &session->scenes[session->scene_count - 1];
	if (scene->query == NULL || scene->queries_active)
		return false;
	struct st_rip3_query *parent = session->commands.query;
	session->commands.query = scene->query;
	scene->query = parent;
	parent = session->refresh.query;
	session->refresh.query = scene->refresh;
	scene->refresh = parent;
	scene->field_program = session->field_program;
	scene->field_program_id = session->field_program_id;
	session->field_program = false;
	scene->queries_active = true;
	return true;
}

static void
close_scene(struct st_rip3_session *session)
{
	struct local_scene *scene = &session->scenes[session->scene_count - 1];
	if (session->sink.scene_end != NULL)
		session->sink.scene_end(session->sink.context, scene->caller_input);
	--session->scene_count;
	if (scene->queries_active) {
		st_rip3_query_destroy(session->commands.query);
		st_rip3_query_destroy(session->refresh.query);
		session->commands.query = scene->query;
		session->refresh.query = scene->refresh;
		session->field_program = scene->field_program;
		session->field_program_id = scene->field_program_id;
	}
	else {
		st_rip3_query_destroy(scene->query);
		st_rip3_query_destroy(scene->refresh);
	}
	st_rip3_stream_destroy(scene->stream);
	st_rip3_resource_dispose(&scene->file);
}

static uint64_t
scene_wait_ns(const struct st_rip3_session *session, const struct local_scene *scene)
{
	if (!scene->waiting_for_group)
		return 0;
	uint64_t elapsed = session->source.now_ns(session->source.context) - scene->group_started;
	return elapsed >= scene->group_delay_ns ? 0 : scene->group_delay_ns - elapsed;
}

static bool
scene_byte_due(struct st_rip3_session *session, struct local_scene *scene)
{
	uint32_t rate = st_rip3_environments_playback_rate(session->commands.environments);
	if (scene->waiting_for_group) {
		if (scene_wait_ns(session, scene) != 0)
			return false;
		scene->waiting_for_group = false;
	}
	else if (rate != 0 && ++scene->group_count >= scene->group_size) {
		scene->group_count = 0;
		if (scene->group_delay_ns != 0) {
			scene->group_started = now(session);
			scene->waiting_for_group = true;
			return false;
		}
	}
	/* A changed rate starts the next group after accounting for this
	 * reader byte at the previously cached rate. */
	if (rate != 0 && rate != scene->playback_rate)
		scene_playback_rate(scene, rate);
	return true;
}

static int
next_scene_byte(struct local_scene *scene)
{
	int value = fgetc(scene->file.stream);
	if (scene->normalized)
		return value;
	if (value == 0x1a)
		return EOF;
	if (!scene->normalized && value == '\r') {
		/* A binary resource stream needs the same text conversion on every
		 * platform. Use one character of pushback for a bare CR. */
		int following = fgetc(scene->file.stream);
		if (following == '\n')
			value = '\n';
		else if (following != EOF)
			(void)ungetc(following, scene->file.stream);
	}
	return value;
}

static bool
next(void *context, uint8_t *byte)
{
	struct st_rip3_session *session=context;
	while (session->scene_count != 0) {
		struct local_scene *scene = &session->scenes[session->scene_count - 1];
		if (!scene->byte_ready) {
			int value = next_scene_byte(scene);
			if (value == EOF) {
				if (ferror(scene->file.stream))
					diagnose(session, ST_RIP3_COMMAND_OPERATION_FAILED);
				/* Flush buffered text and its replies before resuming the caller. */
				if (session->sink.before_command != NULL)
					session->sink.before_command(session->sink.context);
				bool resume_query = scene->queries_active;
				close_scene(session);
				if (resume_query)
					return false;
				continue;
			}
			scene->next_byte = (uint8_t)value;
			scene->byte_ready = true;
		}
		if (!scene_byte_due(session, scene))
			return false;
		*byte = scene->next_byte;
		scene->byte_ready = false;
		return true;
	}
	return session->source.next(session->source.context,byte);
}

static void
consume(void *context, uint8_t byte)
{
	struct st_rip3_session *session=context;
	if (session->scene_count == 0) {
		st_rip3_stream_process(session->stream, &byte, 1);
		return;
	}
	struct local_scene *scene = &session->scenes[session->scene_count - 1];
	if (!scene->normalized && byte == 0)
		return;
	const uint8_t *data = &byte;
	size_t size = 1;
	uint8_t output[4];
	if (!scene->normalized && byte >= 0x80 && session->sink.scene_decode != NULL) {
		size = session->sink.scene_decode(session->sink.context, byte, output);
		if (size > sizeof(output))
			return;
		data = output;
	}
	if (scene->normalized)
		text(session, data, size);
	else
		st_rip3_stream_process(scene->stream, data, size);
}

static bool
continuation_pending(const struct st_rip3_session *session)
{
	return session->defining_variable || st_rip3_query_pending(session->commands.query) || st_rip3_refresh_pending(&session->refresh)
	    || session->graphical_job || st_rip3_button_renderer_pending(session->commands.buttons)
	    || st_rip3_mouse_pending(session->commands.mouse);
}

static bool
prepare(void *context)
{
	struct st_rip3_session *session=context;
	if (session->defining_variable)
		return false;
	(void)activate_scene_queries(session);
	bool pending=continuation_pending(session);
	if (pending)
		st_rip3_columns_invalidate(&session->columns);
	struct st_rip3_field_context previous=session->field_context;
	if (session->field_program) session->field_context=(struct st_rip3_field_context){true,session->field_program_id};
	/* An active refresh is a separate reply. Resume it before the
	 * expression that requested it and before reading the next command. */
	st_rip3_query_process(session->refresh.query, 64);
	if (activate_scene_queries(session)) {
		session->field_context=previous;
		++session->revision;
		return true;
	}
	st_rip3_query_process(session->commands.query,64);
	session->field_context=previous;
	if (activate_scene_queries(session)) {
		++session->revision;
		return true;
	}
	if (!st_rip3_query_pending(session->commands.query)) session->field_program=false;
	if (session->graphical_job) {
		bool retained=session->graphical_job==GRAPHICAL_METRIC ? session->metric_argument.retained
		    : session->graphical_job==GRAPHICAL_TEXT && session->text_retained;
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
				else if (session->graphical_job==GRAPHICAL_DIRECTORY)
					status=st_rip3_directory_select(session->variables.directory,result->bytes,result->length,true);
				else if (session->graphical_job==GRAPHICAL_SCENE) {
					operation=true;
					status=open_scene(session,(const char *)result->bytes,false);
				}
				else if (session->graphical_job==GRAPHICAL_COLUMN_SEARCH)
					status=st_rip3_columns_define(&session->columns,&session->commands,&session->column_definition,
					    result->bytes,result->length,session->graphical_codepage_converted);
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
	return !continuation_pending(session);
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

static int
maximize_window(void *context, unsigned number)
{
	struct st_rip3_session *session = context;
	if (number >= 36) return IO_ERROR_OUT_OF_RANGE;
	struct session_window *window = &session->windows[number];
	if (!window_active(session, number) || window->info.protected) return 0;
	if (session->sink.text_maximize == NULL) return IO_ERROR_NOT_SUPPORTED;
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
	return status;
}

static void
header_text_windows(struct st_rip3_session *session, unsigned flags, unsigned slot)
{
	if (flags & 1) {
		for (unsigned i = 1; i < 36; ++i)
			header_result(session, reset_window_entry(session, i));
		header_result(session, select_text_window(session, 0, false));
		header_result(session, maximize_window(session, 0));
	}
	if (flags & 2) header_result(session, select_text_window(session, slot, true));
	if (flags & 4) {
		for (unsigned i = 1; i < 36; ++i)
			if (i != session->current_window)
				header_result(session, reset_window_entry(session, i));
		if (session->current_window != 0)
			header_result(session, maximize_window(session, 0));
	}
	header_backups(session, ST_RIP3_TABLE_TW, flags >> 3);
	unsigned all[36], current = session->current_window;
	for (unsigned i = 0; i < 36; ++i) all[i] = i;
	if (flags & 64) (void)activate_text_windows(session, all, 36, false);
	if (flags & 128) (void)activate_text_windows(session, &current, 1, false);
	if (flags & 256) (void)activate_text_windows(session, all, 36, true);
	if (flags & 512) (void)activate_text_windows(session, &current, 1, true);
	if (flags & 1024)
		for (unsigned i = 0; i < 36; ++i) header_result(session, maximize_window(session, i));
	if (flags & 2048) header_result(session, maximize_window(session, current));
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
		if (flags & 2) header_result(session, st_rip3_ports_select(host->ports, slot));
		if (flags & 4) header_result(session, st_rip3_ports_header_delete_clipboard(host->ports));
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
		/* The indexed renderer resolves the current palette on presentation.
		 * Uploading its existing entries under bit 64 or 128 is idempotent. */
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
file_query(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	const struct st_rip3_command_sink sink = {session,diagnose};
	struct st_rip3_environment env;
	unsigned current;
	uint64_t mode, reserved;
	int status = IO_ERROR_INVALID_FORMAT;

	if (length > 8 && memchr(bytes+8,0,length-8) == NULL) {
		status = st_rip3_command_environment(&session->commands,&current,&env);
		if (status == 0) status = st_rip3_command_field(&mode,bytes+2,2,env.base_math);
		if (status == 0) status = st_rip3_command_field(&reserved,bytes+4,4,env.base_math);
		if (status == 0 && mode > 4) status = IO_ERROR_OUT_OF_RANGE;
	}
	if (status != 0) {
		st_rip3_command_result(&sink,status,false);
		return;
	}
	if (session->sink.reply == NULL || session->commands.resources == NULL) return;
	char *output;
	size_t size;
	status = st_rip3_file_query(session->commands.resources,(unsigned)mode,bytes+8,length-8,&output,&size);
	if (size != 0) reply(session,(const uint8_t *)output,size);
	free(output);
	st_rip3_command_result(&sink,status,true);
}

static int
switch_directory(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	const struct st_rip3_host_directory *directory = session->variables.directory;
	if (directory == NULL) return 0;
	if (length <= 6 || memchr(bytes+6,0,length-6) != NULL) return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_environment env;
	unsigned current;
	uint64_t reserved;
	int status = st_rip3_command_environment(&session->commands,&current,&env);
	if (status == 0) status = st_rip3_command_field(&reserved,bytes+2,4,env.base_math);
	if (status != 0) return status;
	uint8_t *name = malloc(length-6);
	if (name == NULL) return IO_ERROR_OUT_OF_MEMORY;
	memcpy(name,bytes+6,length-6);
	size_t size = st_rip3_directory_normalize(name,length-6);
	if (size == 0 || (size == 5 && memcmp(name,"$OFF$",5) == 0))
		status = st_rip3_directory_select(directory,name,0,size == 0);
	else {
		/* This command shares the ordinary graphical expansion flags with T. */
		st_rip3_graphical_start(session->graphical,ST_RIP3_GRAPHICAL_TEXT,name,size);
		session->graphical_job=GRAPHICAL_DIRECTORY;
	}
	free(name);
	return status;
}

static int
define_variable(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	struct st_rip3_environment environment;
	unsigned current;
	uint64_t flags, reserved;
	struct st_rip3_variable_reference definition;
	struct st_rip3_user_variables *user = session->variables.user_variables;
	bool utf8 = session->text.encoding == ST_RIP3_TEXT_UTF8;

	if (length <= 7 || memchr(bytes + 7, 0, length - 7) != NULL)
		return IO_ERROR_INVALID_FORMAT;
	int status = st_rip3_command_environment(&session->commands, &current, &environment);
	if (status == 0)
		status = st_rip3_command_field(&flags, bytes + 2, 3, environment.base_math);
	if (status == 0)
		status = st_rip3_command_field(&reserved, bytes + 5, 2, environment.base_math);
	if (status == 0)
		status = st_rip3_variable_definition_parse(&definition, bytes + 7, length - 7,
		    utf8, session->graphical_codepage_converted);
	if (status != 0)
		return status;
	if (user == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (st_rip3_user_variables_state(user) == ST_RIP3_USER_VARIABLE_PROMPT)
		return IO_ERROR_BUSY;
	definition.directives = ST_RIP3_VARIABLE_SET
	    | ((flags & 1) ? ST_RIP3_VARIABLE_PERSISTENT : ST_RIP3_VARIABLE_SESSION);
	st_rip3_user_variables_define(user, &definition, (flags & 4) == 0, utf8, session->graphical_codepage_converted);
	session->defining_variable = st_rip3_user_variables_state(user) == ST_RIP3_USER_VARIABLE_PROMPT;
	session->definition_reject_blank = (flags & 2) != 0;
	if (!session->defining_variable)
		st_rip3_user_variables_abort(user);
	return 0;
}

static void
read_scene(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	struct st_rip3_environment env;
	unsigned current;
	uint64_t reserved;
	const struct st_rip3_command_sink sink={session,diagnose};
	int status = st_rip3_command_environment(&session->commands, &current, &env);
	if (status == 0 && (length <= 10 || memchr(bytes + 10, 0, length - 10) != NULL))
		status = IO_ERROR_INVALID_FORMAT;
	if (status == 0)
		status = st_rip3_command_field(&reserved, bytes + 2, 4, env.base_math);
	if (status == 0)
		status = st_rip3_command_field(&reserved, bytes + 6, 4, env.base_math);
	if (status != 0) {
		st_rip3_command_result(&sink, status, false);
		return;
	}
	st_rip3_graphical_start(session->graphical, ST_RIP3_GRAPHICAL_SCENE, bytes + 10, length - 10);
	session->graphical_job = GRAPHICAL_SCENE;
}

static int
define_column(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	size_t tail;
	struct st_rip3_column_definition *definition = &session->column_definition;
	int status = st_rip3_columns_decode(definition, &session->commands, bytes, length, &tail);
	if (status != 0 || definition->suppressed)
		return status;
	if ((definition->flags & 2) == 0)
		return st_rip3_columns_define(&session->columns, &session->commands, definition, NULL, 0, false);
	st_rip3_graphical_start(session->graphical, ST_RIP3_GRAPHICAL_TEXT, bytes + tail, length - tail);
	session->graphical_job = GRAPHICAL_COLUMN_SEARCH;
	return 0;
}

static int
select_article(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	if (length != 8)
		return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_environment env;
	unsigned current;
	uint64_t article, reserved;
	int status = st_rip3_command_environment(&session->commands, &current, &env);
	if (status == 0)
		status = st_rip3_command_field(&article, bytes + 2, 2, env.base_math);
	if (status == 0)
		status = st_rip3_command_field(&reserved, bytes + 4, 4, env.base_math);
	if (status != 0)
		return status;
	if (article >= ST_RIP3_ARTICLE_COUNT)
		return IO_ERROR_OUT_OF_RANGE;
	return st_rip3_columns_select(&session->columns, &session->commands, (unsigned)article);
}

static int
region_text(struct st_rip3_session *session, const uint8_t *bytes, size_t length)
{
	if (length < 3)
		return IO_ERROR_INVALID_FORMAT;
	struct st_rip3_environment env;
	unsigned current;
	uint64_t justify;
	int status = st_rip3_command_environment(&session->commands, &current, &env);
	if (status == 0)
		status = st_rip3_command_field(&justify, bytes + 2, 1, env.base_math);
	if (status != 0 || justify > 1)
		return status;
	return st_rip3_columns_line(&session->columns, &session->commands, bytes + 3, length - 3, justify == 1);
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_session *session=context;
	st_rip3_columns_invalidate(&session->columns);
	if (session->sink.before_command!=NULL) session->sink.before_command(session->sink.context);
	const struct st_rip3_command_sink sink={session,diagnose};
	if (length != 0 && bytes[0] == 'h') scene_header(session, bytes, length);
	else if (length != 0 && bytes[0] == '#') {
		if (length == 1) {
			set_input_lock(session, false);
			st_rip3_command_result(&sink, st_rip3_columns_end(&session->columns,&session->commands), true);
		}
		else diagnose(session, ST_RIP3_COMMAND_INVALID);
	}
	else if (length >= 2 && bytes[0] == '2' && bytes[1] == 0x1b)
		st_rip3_command_result(&sink,switch_directory(session,bytes,length),false);
	else if (length >= 2 && bytes[0] == '1' && bytes[1] == 'F')
		file_query(session,bytes,length);
	else if (length >= 2 && bytes[0] == '1' && bytes[1] == 'D')
		st_rip3_command_result(&sink, define_variable(session, bytes, length), false);
	else if (length >= 2 && bytes[0] == '1' && bytes[1] == 'R')
		read_scene(session,bytes,length);
	else if (length >= 2 && bytes[0] == '1' && (bytes[1] == 'T' || bytes[1] == 'e'))
		st_rip3_command_result(&sink, define_column(session,bytes,length), false);
	else if (length >= 2 && bytes[0] == '1' && bytes[1] == 'A')
		st_rip3_command_result(&sink, select_article(session,bytes,length), false);
	else if (length >= 2 && bytes[0] == '1' && bytes[1] == 't')
		st_rip3_command_result(&sink, region_text(session,bytes,length), false);
	else if (length >= 2 && bytes[0] == '1' && bytes[1] == 'E') {
		if (length == 2)
			st_rip3_command_result(&sink, st_rip3_columns_end(&session->columns,&session->commands), true);
		else
			diagnose(session, ST_RIP3_COMMAND_INVALID);
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
	while (session->scene_count != 0)
		close_scene(session);
	st_rip3_query_cancel(session->commands.query);
	st_rip3_query_cancel(session->refresh.query);
	st_rip3_session_abort_definition(session);
	st_rip3_graphical_cancel(session->graphical);session->graphical_job=GRAPHICAL_NONE;
	st_rip3_columns_dispose(&session->columns);
	session->field_program=false;
	st_rip3_input_reset(session->commands.input);
	st_rip3_stream_reset(session->stream);
	if (session->ready && session->commands.audio != NULL) {
		st_rip3_audio_stop_tones(session->commands.audio);
		(void)st_rip3_audio_stop(session->commands.audio);
	}
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
	st_rip3_columns_invalidate(&session->columns);
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

static bool
tones_pending(void *context)
{
	return st_rip3_audio_tones_pending(context);
}

static void
play_tones(void *context, const struct st_rip3_tones *tones)
{
	struct st_rip3_session *session = context;
	if (st_rip3_audio_play_tones(session->commands.audio, tones))
		st_rip3_input_wait_for(session->commands.input, tones_pending, session->commands.audio);
}

static int
playback(void *context, enum st_rip3_playback_kind kind, const char *filename)
{
	struct st_rip3_session *session=context;
	if (kind == ST_RIP3_PLAYBACK_SCENE)
		return open_scene(session, filename, true);
	if (kind==ST_RIP3_PLAYBACK_WAVE) {
		if (session->commands.audio==NULL || session->commands.resources==NULL)
			return IO_ERROR_NOT_SUPPORTED;
		return st_rip3_audio_play(session->commands.audio,session->commands.resources,filename);
	}
	if (kind==ST_RIP3_PLAYBACK_BITMAP) return st_rip3_command_bitmap_playback(&session->commands,filename);
	if (kind!=ST_RIP3_PLAYBACK_JPEG) return IO_ERROR_NOT_SUPPORTED;
	bool operation=false;
	return st_rip3_command_jpeg(&session->commands,(const uint8_t *)filename,strlen(filename),&operation);
}

int
st_rip3_session_create(struct st_rip3_session **result, const struct st_rip3_session_config *config)
{
	if (result==NULL || config==NULL || (config->graphical_codepage_converted && config->encoding!=ST_RIP3_TEXT_UTF8)
	    || config->source.now_ns==NULL || config->source.next==NULL
	    || config->default_text_font>=5
	    || (config->encoding!=ST_RIP3_TEXT_CP437 && config->encoding!=ST_RIP3_TEXT_UTF8)) return IO_ERROR_INVALID_ARGUMENT;
	if ((config->sink.screen_capture==NULL)!=(config->sink.screen_restore==NULL)) return IO_ERROR_INVALID_ARGUMENT;
	if ((config->sink.scene_begin == NULL) != (config->sink.scene_end == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
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
	session->variables.offscreen_pixel_budget=config->offscreen_pixel_budget;
	session->variables.refresh=&session->refresh;
	session->commands.refresh=&session->refresh;
	session->window_operations=(struct st_rip3_text_window_operations){.context=session,
	    .current=current_text_window,.select=select_default_window,.info=window_info,.snapshot=window_snapshot,
	    .protect=protect_window,.activate=activate_text_windows,.maximize=maximize_window,
	    .position=position_window_cursor,.cursor=show_window_cursors,.erase=erase_text_windows,
	    .erase_eol=erase_window_lines,.copy_entries=copy_window_entries,.reset_entry=reset_window_entry,
	    .backup_capture=capture_window_save,.backup_restore=restore_window_save,
	    .backup_destroy=destroy_window_save};
	session->variables.text_windows=&session->window_operations;
	session->tables.tables[ST_RIP3_TABLE_TW].live=&session->window_operations;
	session->random.state=config->random_seed;
	session->columns.overflow.instance=config->random_seed & UINT16_MAX;
	session->variables.overflow_context=session;session->variables.overflow=overflow_reference;
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
	if (status==0) st_rip3_ports_bind_screen_writer(session->commands.ports,config->sink.context,config->sink.screen_write);
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
	session->variables.tones_context = session;
	session->variables.tones = play_tones;
	if (status==0) status=create_query(session,&session->commands.query,&session->variables);
	session->refresh_variables = session->variables;
	/* Refresh uses ordinary host expansion without template slots. Keep the
	 * same stores, delay gate and client reply sink for both evaluators. */
	session->refresh_variables.templates = NULL;
	if (status == 0)
		status = create_query(session, &session->refresh.query, &session->refresh_variables);
	if (status==0) status=st_rip3_graphical_create(&session->graphical,&session->variables,&session->host_sink,config->graphical_codepage_converted);
	const struct st_rip3_command_sink button_sink={session,diagnose};
	if (status==0) status=st_rip3_button_renderer_create(&session->commands.buttons,&session->commands,&button_sink,
	    session->graphical,config->graphical_codepage_converted);
	if (status==0) status=create_stream(session,&session->stream,false);
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
	st_rip3_columns_invalidate(&session->columns);
	header_result(session, st_rip3_refresh_start(&session->refresh));
	++session->revision;
}

uint64_t
st_rip3_session_wait_ns(const struct st_rip3_session *session)
{
	if (session == NULL)
		return 0;
	uint64_t wait = st_rip3_input_wait_ns(session->commands.input);
	if (wait == 0 && session->scene_count != 0)
		wait = scene_wait_ns(session, &session->scenes[session->scene_count - 1]);
	return wait;
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
	return session!=NULL && (session->scene_count != 0 || continuation_pending(session));
}

bool
st_rip3_session_local_scene(const struct st_rip3_session *session)
{
	return session != NULL && session->scene_count != 0;
}

bool
st_rip3_session_prompt(const struct st_rip3_session *session, struct st_rip3_session_prompt *result)
{
	if (session==NULL || result==NULL || (!session->defining_variable && !st_rip3_query_prompt_pending(session->commands.query)
	    && !st_rip3_query_prompt_pending(session->refresh.query) && st_rip3_graphical_state(session->graphical)!=ST_RIP3_GRAPHICAL_PROMPT)) return false;
	const struct st_rip3_variable_reference *reference=st_rip3_user_variables_prompt(session->variables.user_variables);
	if (reference==NULL) return false;
	*result = (struct st_rip3_session_prompt){
		.reference = reference,
		.initial = st_rip3_user_variables_initial(session->variables.user_variables),
		.definition = session->defining_variable,
		.reject_blank = session->defining_variable && session->definition_reject_blank,
	};
	return true;
}

void
st_rip3_session_abort_definition(struct st_rip3_session *session)
{
	if (session != NULL && session->defining_variable) {
		st_rip3_user_variables_abort(session->variables.user_variables);
		session->defining_variable = false;
	}
}

void
st_rip3_session_respond(struct st_rip3_session *session, const uint8_t *bytes, size_t length, bool cancelled)
{
	if (session==NULL) return;
	struct st_rip3_session_prompt prompt;
	if (!st_rip3_session_prompt(session,&prompt)) {diagnose(session,ST_RIP3_COMMAND_OPERATION_FAILED);return;}
	if (prompt.definition) {
		if (!cancelled && prompt.reject_blank && length == 0)
			return;
		/* The 1D editor's cancellation stores an empty value. Transport
		 * failure and loss of the editor go through abort_definition. */
		st_rip3_user_variables_respond(session->variables.user_variables,
		    cancelled ? NULL : bytes, cancelled ? 0 : length, false);
		st_rip3_session_abort_definition(session);
	}
	else
		st_rip3_user_variables_respond(session->variables.user_variables,bytes,length,cancelled);
	(void)prepare(session);
	++session->revision;
}

static struct st_rip3_query *
pick_list_query(const struct st_rip3_session *session)
{
	if (session==NULL) return NULL;
	if (st_rip3_query_pick_list(session->refresh.query)!=NULL) return session->refresh.query;
	return session->commands.query;
}

const struct st_rip3_pick_list *
st_rip3_session_pick_list(const struct st_rip3_session *session, struct st_rip3_frame *frame)
{
	const struct st_rip3_pick_list *list=st_rip3_query_pick_list(pick_list_query(session));
	if (list!=NULL && frame!=NULL) {
		*frame=session->frame;
		unsigned current;
		struct st_rip3_environment environment;
		if (st_rip3_environments_current(session->commands.environments,&current)==0
		    && st_rip3_environments_get(session->commands.environments,current,&environment)==0) {
			frame->world_width=environment.world_width;
			frame->world_height=environment.world_height;
		}
	}
	return list;
}

bool
st_rip3_session_choose(struct st_rip3_session *session, size_t index)
{
	return st_rip3_query_choose(pick_list_query(session),index);
}

void
st_rip3_session_pick_list_failed(struct st_rip3_session *session, int status)
{
	st_rip3_query_pick_list_failed(pick_list_query(session),status);
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
