/* Fail every session-owned construction boundary and count released objects. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_session.h"
#include "st_rip3_text_window.h"
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
#include "st_rip3_button_render.h"
#include "st_rip3_input.h"
#include "st_rip3_audio.h"
#include "st_rip3_resources.h"
#include "io_error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

size_t st_rip3_session_test_fail_at=SIZE_MAX;
size_t st_rip3_session_test_attempts,st_rip3_session_test_live,st_rip3_session_test_stops;
unsigned st_rip3_session_test_lookups;
int st_rip3_session_test_lookup_status;
char st_rip3_session_test_lookup_name[1021];

static int
test_resource_exists(const struct st_rip3_resource_paths *paths, const char *name)
{
	st_rip3_session_test_lookups++;
	(void)snprintf(st_rip3_session_test_lookup_name, sizeof(st_rip3_session_test_lookup_name), "%s", name);
	st_rip3_session_test_lookup_status = st_rip3_resource_exists(paths, name);
	return st_rip3_session_test_lookup_status;
}

static bool
allowed(void)
{
	return st_rip3_session_test_attempts++!=st_rip3_session_test_fail_at;
}

static void *
test_calloc(size_t count, size_t size)
{
	if (!allowed()) return NULL;
	void *result=calloc(count,size);if (result!=NULL) st_rip3_session_test_live++;return result;
}

static void
test_free(void *object)
{
	if (object!=NULL) st_rip3_session_test_live--;free(object);
}

#define FACTORY(name,parameters,arguments) \
static int test_##name parameters { \
	if (!allowed()) return IO_ERROR_OUT_OF_MEMORY; \
	int status=name arguments;if (status==0) st_rip3_session_test_live++;return status; \
}
#define RELEASE(name,type) \
static void test_##name(struct type *object) { \
	if (object!=NULL) st_rip3_session_test_live--;name(object); \
}

FACTORY(st_rip3_environments_create,(struct st_rip3_environments **p,enum st_rip3_canvas c),(p,c))
FACTORY(st_rip3_environments_create_device,(struct st_rip3_environments **p,uint32_t w,uint32_t h),(p,w,h))
FACTORY(st_rip3_styles_create,(struct st_rip3_styles **p,const struct st_rip3_style *s),(p,s))
FACTORY(st_rip3_button_styles_create,(struct st_rip3_button_styles **p),(p))
FACTORY(st_rip3_mouse_create,(struct st_rip3_mouse **p,const struct st_rip3_mouse_sink *s),(p,s))
FACTORY(st_rip3_palettes_create,(struct st_rip3_palettes **p),(p))
FACTORY(st_rip3_ports_create,(struct st_rip3_ports **p,enum st_rip3_canvas c),(p,c))
FACTORY(st_rip3_ports_create_device,(struct st_rip3_ports **p,uint32_t w,uint32_t h),(p,w,h))
FACTORY(st_rip3_environments_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_styles_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_button_styles_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_palettes_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_ports_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_mouse_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_screen_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_text_window_backup_create,(struct st_rip3_backup **p),(p))
FACTORY(st_rip3_outline_cache_create,(struct st_rip3_outline_cache **p),(p))
FACTORY(st_rip3_templates_create,(struct st_rip3_templates **p,bool converted),(p,converted))
FACTORY(st_rip3_variables_create,(struct st_rip3_variables **p),(p))
FACTORY(st_rip3_user_variables_create,(struct st_rip3_user_variables **p,const struct st_rip3_user_variable_host *h),(p,h))
FACTORY(st_rip3_input_create,(struct st_rip3_input **p,const struct st_rip3_input_source *s),(p,s))
FACTORY(st_rip3_query_create,(struct st_rip3_query **p,const struct st_rip3_host_variables *v,const struct st_rip3_host_sink *s),(p,v,s))
FACTORY(st_rip3_button_renderer_create,(struct st_rip3_button_renderer **p,const struct st_rip3_command_host *h,const struct st_rip3_command_sink *s,struct st_rip3_graphical *w,bool c),(p,h,s,w,c))
FACTORY(st_rip3_graphical_create,(struct st_rip3_graphical **p,const struct st_rip3_host_variables *v,const struct st_rip3_host_sink *s,bool c),(p,v,s,c))
FACTORY(st_rip3_stream_create,(struct st_rip3_stream **p,const struct st_rip3_stream_sink *s,const struct st_rip3_environments *e),(p,s,e))
RELEASE(st_rip3_environments_destroy,st_rip3_environments)
RELEASE(st_rip3_styles_destroy,st_rip3_styles)
RELEASE(st_rip3_button_styles_destroy,st_rip3_button_styles)
RELEASE(st_rip3_mouse_destroy,st_rip3_mouse)
RELEASE(st_rip3_palettes_destroy,st_rip3_palettes)
RELEASE(st_rip3_ports_destroy,st_rip3_ports)
RELEASE(st_rip3_backup_destroy,st_rip3_backup)
RELEASE(st_rip3_outline_cache_destroy,st_rip3_outline_cache)
RELEASE(st_rip3_templates_destroy,st_rip3_templates)
RELEASE(st_rip3_variables_destroy,st_rip3_variables)
RELEASE(st_rip3_user_variables_destroy,st_rip3_user_variables)
RELEASE(st_rip3_input_destroy,st_rip3_input)
RELEASE(st_rip3_query_destroy,st_rip3_query)
RELEASE(st_rip3_graphical_destroy,st_rip3_graphical)
RELEASE(st_rip3_button_renderer_destroy,st_rip3_button_renderer)
RELEASE(st_rip3_stream_destroy,st_rip3_stream)

static int
test_audio_stop(struct st_rip3_audio *player)
{
	st_rip3_session_test_stops++;return st_rip3_audio_stop(player);
}

#define calloc test_calloc
#define free test_free
#define st_rip3_environments_create test_st_rip3_environments_create
#define st_rip3_environments_create_device test_st_rip3_environments_create_device
#define st_rip3_styles_create test_st_rip3_styles_create
#define st_rip3_button_styles_create test_st_rip3_button_styles_create
#define st_rip3_mouse_create test_st_rip3_mouse_create
#define st_rip3_palettes_create test_st_rip3_palettes_create
#define st_rip3_ports_create test_st_rip3_ports_create
#define st_rip3_ports_create_device test_st_rip3_ports_create_device
#define st_rip3_environments_backup_create test_st_rip3_environments_backup_create
#define st_rip3_styles_backup_create test_st_rip3_styles_backup_create
#define st_rip3_button_styles_backup_create test_st_rip3_button_styles_backup_create
#define st_rip3_palettes_backup_create test_st_rip3_palettes_backup_create
#define st_rip3_ports_backup_create test_st_rip3_ports_backup_create
#define st_rip3_mouse_backup_create test_st_rip3_mouse_backup_create
#define st_rip3_screen_backup_create test_st_rip3_screen_backup_create
#define st_rip3_text_window_backup_create test_st_rip3_text_window_backup_create
#define st_rip3_outline_cache_create test_st_rip3_outline_cache_create
#define st_rip3_templates_create test_st_rip3_templates_create
#define st_rip3_variables_create test_st_rip3_variables_create
#define st_rip3_user_variables_create test_st_rip3_user_variables_create
#define st_rip3_input_create test_st_rip3_input_create
#define st_rip3_query_create test_st_rip3_query_create
#define st_rip3_graphical_create test_st_rip3_graphical_create
#define st_rip3_button_renderer_create test_st_rip3_button_renderer_create
#define st_rip3_stream_create test_st_rip3_stream_create
#define st_rip3_environments_destroy test_st_rip3_environments_destroy
#define st_rip3_styles_destroy test_st_rip3_styles_destroy
#define st_rip3_button_styles_destroy test_st_rip3_button_styles_destroy
#define st_rip3_mouse_destroy test_st_rip3_mouse_destroy
#define st_rip3_palettes_destroy test_st_rip3_palettes_destroy
#define st_rip3_ports_destroy test_st_rip3_ports_destroy
#define st_rip3_backup_destroy test_st_rip3_backup_destroy
#define st_rip3_outline_cache_destroy test_st_rip3_outline_cache_destroy
#define st_rip3_templates_destroy test_st_rip3_templates_destroy
#define st_rip3_variables_destroy test_st_rip3_variables_destroy
#define st_rip3_user_variables_destroy test_st_rip3_user_variables_destroy
#define st_rip3_input_destroy test_st_rip3_input_destroy
#define st_rip3_query_destroy test_st_rip3_query_destroy
#define st_rip3_graphical_destroy test_st_rip3_graphical_destroy
#define st_rip3_button_renderer_destroy test_st_rip3_button_renderer_destroy
#define st_rip3_stream_destroy test_st_rip3_stream_destroy
#define st_rip3_audio_stop test_audio_stop
#define st_rip3_resource_exists test_resource_exists
#include "../st_rip3_session.c"

/* Exercise full-scene replacement while the public consumer owns a prompt.
 * No production reset API or callback reentry is added for this injection. */
int st_rip3_session_test_soft_reset(struct st_rip3_session *session)
{
	return soft_reset(session);
}

void st_rip3_session_test_overflow_frontier(struct st_rip3_session *session, unsigned article, int32_t page)
{
	session->columns.overflow.articles[article].frontier = page;
}

const struct st_rip3_resident_query *
st_rip3_session_test_resident(struct st_rip3_session *session,unsigned mode,unsigned number)
{
	if (mode==1 || mode==3) return st_rip3_ports_query(session->commands.ports,mode==1,number);
	if (mode==2) return &session->floating_window_query;
	return mode==4 && number<36 && session->windows[number].info.defined ? &session->windows[number].query : NULL;
}
