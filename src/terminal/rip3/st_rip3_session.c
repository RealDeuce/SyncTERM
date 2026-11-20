/* The single-threaded owner of the separate RIP3 engine's represented state. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_session.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_backup.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_templates.h"
#include "st_rip3_user_variables.h"
#include "st_rip3_stream.h"
#include "st_rip3_query.h"
#include "st_rip3_input.h"
#include "st_rip3_table_host.h"
#include "st_rip3_audio.h"
#include "st_rip3_bitmap_error_ui.h"
#include "st_rip3_text_window.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdlib.h>

struct st_rip3_session {
	struct st_rip3_session_source source;
	struct st_rip3_session_sink sink;
	struct st_rip3_command_host commands;
	struct st_rip3_host_variables variables;
	struct st_rip3_table_host tables;
	struct st_rip3_host_sink host_sink;
	struct st_rip3_text_context text;
	struct st_rip3_template_host templates;
	struct st_rip3_clipboard_selection clipboard;
	struct st_rip3_random random;
	struct st_rip3_frame frame;
	struct st_rip3_stream *stream;
	struct st_rip3_variables *session_values,*persistent_values;
	struct st_rip3_user_variables *owned_user;
	bool ready;
	bool text_active;
	bool has_text_window;
	struct st_rip3_text_window_layout text_window;
	struct st_rip3_text_window_host window_host;
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
	bool same=session->has_text_window && st_rip3_text_window_same(&session->text_window,layout);
	int status=session->sink.text_window(session->sink.context,layout,same);
	if (status==0) {session->text_window=*layout;session->has_text_window=true;session->text_active=true;}
	return status;
}

static int
position_text_cursor(void *context, uint32_t column, uint32_t row)
{
	struct st_rip3_session *session=context;
	if (!session->text_active) return 0;
	return session->sink.text_cursor==NULL ? IO_ERROR_NOT_SUPPORTED
	    : session->sink.text_cursor(session->sink.context,column,row);
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
	bool pending=st_rip3_query_pending(session->commands.query);
	st_rip3_query_process(session->commands.query,64);
	if (pending) ++session->revision;
	return !st_rip3_query_pending(session->commands.query);
}

static void
command(void *context, const uint8_t *bytes, size_t length)
{
	struct st_rip3_session *session=context;
	if (session->sink.before_command!=NULL) session->sink.before_command(session->sink.context);
	const struct st_rip3_command_sink sink={session,diagnose};
	st_rip3_commands_process(&session->commands,&sink,bytes,length);
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
	st_rip3_query_cancel(session->commands.query);
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
	st_rip3_input_destroy(session->commands.input);
	st_rip3_stream_destroy(session->stream);
	st_rip3_user_variables_destroy(session->owned_user);
	st_rip3_variables_destroy(session->session_values);st_rip3_variables_destroy(session->persistent_values);
	st_rip3_templates_destroy(session->templates.slots);
	st_rip3_outline_cache_destroy(session->text.outline_cache);
	for (unsigned i=0; i<ST_RIP3_TABLE_COUNT; i++) st_rip3_backup_destroy(session->tables.tables[i].backup);
	st_rip3_environments_destroy(session->commands.environments);
	st_rip3_styles_destroy(session->commands.styles);
	st_rip3_palettes_destroy(session->commands.palettes);
	st_rip3_ports_destroy(session->commands.ports);
	free(session);
}

int
st_rip3_session_create(struct st_rip3_session **result, const struct st_rip3_session_config *config)
{
	if (result==NULL || config==NULL || config->source.now_ns==NULL || config->source.next==NULL
	    || (config->encoding!=ST_RIP3_TEXT_CP437 && config->encoding!=ST_RIP3_TEXT_UTF8)) return IO_ERROR_INVALID_ARGUMENT;
	if (config->services!=NULL && (config->services->frame!=NULL || config->services->tables!=NULL
	    || config->services->input!=NULL || config->services->templates!=NULL)) return IO_ERROR_INVALID_ARGUMENT;
	bool device=config->device_width!=0 || config->device_height!=0;
	struct st_rip3_frame frame;
	int status=device ? st_rip3_frame_init_device(&frame,config->device_width,config->device_height)
	    : st_rip3_frame_init(&frame,config->canvas);
	if (status!=0) return status;
	struct st_rip3_session *session=calloc(1,sizeof(*session));
	if (session==NULL) return IO_ERROR_OUT_OF_MEMORY;
	session->source=config->source;session->sink=config->sink;session->frame=frame;
	session->text_active=true;session->commands.text_active=&session->text_active;
	session->window_host=(struct st_rip3_text_window_host){session,define_text_window,position_text_cursor};
	session->commands.text_window=&session->window_host;
	session->commands.audio=config->audio;session->commands.resources=config->resources;
	session->commands.offscreen_pixel_budget=config->offscreen_pixel_budget;session->commands.bitmap_errors=config->bitmap_errors;
	session->commands.clipboard=&session->clipboard;session->commands.text=&session->text;
	session->text.encoding=config->encoding;session->text.font=config->classic_font;session->text.outline_font=config->outline_font;
	if (config->services!=NULL) session->variables=*config->services;
	session->variables.frame=&session->frame;session->variables.tables=&session->tables;
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
	if (status==0) status=st_rip3_palettes_create(&session->commands.palettes);
	if (status==0) status=device ? st_rip3_ports_create_device(&session->commands.ports,frame.device_width,frame.device_height)
	    : st_rip3_ports_create(&session->commands.ports,config->canvas);
	if (config->builtin_bitmap_errors && config->bitmap_errors==NULL) {
		session->bitmap_ui=(struct st_rip3_bitmap_error_ui){.ports=session->commands.ports,.palettes=session->commands.palettes};
		session->bitmap_errors=(struct st_rip3_bitmap_error_sink){&session->bitmap_ui,st_rip3_bitmap_error_show};
		session->commands.bitmap_errors=&session->bitmap_errors;
	}
	if (status==0) status=st_rip3_environments_backup_create(&session->tables.tables[ST_RIP3_TABLE_ENV].backup);
	if (status==0) status=st_rip3_styles_backup_create(&session->tables.tables[ST_RIP3_TABLE_STYLE].backup);
	if (status==0) status=st_rip3_palettes_backup_create(&session->tables.tables[ST_RIP3_TABLE_PAL].backup);
	if (status==0) status=st_rip3_ports_backup_create(&session->tables.tables[ST_RIP3_TABLE_PORT].backup);
	session->tables.tables[ST_RIP3_TABLE_ENV].live=session->commands.environments;
	session->tables.tables[ST_RIP3_TABLE_STYLE].live=session->commands.styles;
	session->tables.tables[ST_RIP3_TABLE_PAL].live=session->commands.palettes;
	session->tables.tables[ST_RIP3_TABLE_PORT].live=session->commands.ports;
	if (status==0) status=st_rip3_outline_cache_create(&session->text.outline_cache);
	if (status==0) status=st_rip3_templates_create(&session->templates.slots);
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

uint64_t
st_rip3_session_wait_ns(const struct st_rip3_session *session)
{
	return session==NULL ? 0 : st_rip3_input_wait_ns(session->commands.input);
}

bool
st_rip3_session_pending(const struct st_rip3_session *session)
{
	return session!=NULL && st_rip3_query_pending(session->commands.query);
}

bool
st_rip3_session_prompt(const struct st_rip3_session *session, struct st_rip3_session_prompt *result)
{
	if (session==NULL || result==NULL || !st_rip3_query_prompt_pending(session->commands.query)) return false;
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
	st_rip3_query_process(session->commands.query,64);
	++session->revision;
}

int
st_rip3_session_capture(const struct st_rip3_session *session, struct st_rip3_session_capture *result)
{
	if (session==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_session_capture value={0};struct st_rip3_port_info port;struct st_rip3_palette palette;
	int status=st_rip3_environments_current(session->commands.environments,&value.environment);
	if (status==0) status=st_rip3_environments_frame(session->commands.environments,value.environment,&value.frame);
	if (status==0) status=st_rip3_styles_current(session->commands.styles,&value.style);
	if (status==0) status=st_rip3_ports_current(session->commands.ports,&value.port);
	if (status==0) status=st_rip3_ports_info(session->commands.ports,value.port,&port);
	if (status==0) status=st_rip3_palettes_current(session->commands.palettes,&value.palette);
	if (status==0) status=st_rip3_palettes_current_palette(session->commands.palettes,&palette);
	if (status==0) status=st_rip3_ports_snapshot(session->commands.ports,&palette,&value.pixels);
	if (status!=0) return status;
	value.cursor_x=port.cursor.x;value.cursor_y=port.cursor.y;value.viewport_active=port.viewport_active;
	value.query_pending=st_rip3_query_pending(session->commands.query);value.wait_ns=st_rip3_session_wait_ns(session);
	value.outline_cache_valid=st_rip3_outline_cache_valid(session->text.outline_cache);
	*result=value;return 0;
}

void
st_rip3_session_capture_dispose(struct st_rip3_session_capture *capture)
{
	if (capture==NULL) return;
	st_pixel_buffer_destroy(capture->pixels);*capture=(struct st_rip3_session_capture){0};
}
