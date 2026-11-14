/* BIG t backpressure through the real connection/session pacing boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_term.h"
#include "cterm.h"
#include "cterm_big_internal.h"
#include "st_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
struct observations { uint8_t wire[128],reply[32]; size_t count,replies; };
static uint64_t local_ns;
static int now(void *context, uint64_t *value) { (void)context; *value=local_ns; return 0; }
static int capture(void *context, const uint8_t *bytes, size_t size)
{
	struct observations *o=context; CHECK(size<=sizeof(o->wire)-o->count);
	memcpy(o->wire+o->count,bytes,size); o->count+=size; return 0;
}
static void output(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	struct observations *o=context; (void)origin; CHECK(size<=sizeof(o->reply)-o->replies);
	memcpy(o->reply+o->replies,bytes,size); o->replies+=size;
}
static void pump(st_term_session_t *session)
{
	enum st_term_disposition disposition; bool finished;
	CHECK(st_term_session_pump(session,&disposition,&finished)==0 && !finished);
}
static void run(bool utf8, bool paced)
{
	const struct st_console_display_config display={.backend=ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry={.columns=80,.rows=25,.cell_width=8,.cell_height=14};
	st_console_t *console=NULL; st_font_catalog_t *fonts=NULL; st_connection_t *connection=NULL; st_term_session_t *session=NULL;
	CHECK(st_console_create(&console,&display,&geometry)==0 && st_font_catalog_create(&fonts,console)==0);
	struct st_profile profile; st_profile_init(&profile); profile.connection_type=ST_CONNECTION_SHELL;
	strcpy(profile.address,"printf 'I#t 1:ABCDEFGHIJKLMNOPQRSTUVWXYZ\\33[6n'; read line");
	CHECK(st_term_connect_profile(&connection,&profile,console,NULL,NULL)==0);
	struct observations o={0}; local_ns=1000;
	const struct st_term_config config={.console=console,.fonts=fonts,.connection=connection,
	    .screen_mode=ST_SCREEN_MODE_EGA_80X25,.graphics_mode=ST_CTERM_GRAPHICS_BIG_103,
	    .encoding=utf8?ST_CTERM_ENCODING_UTF8:ST_CTERM_ENCODING_CODEPAGE,.codepage=ST_CODEPAGE_CP437,
	    .hide_status=true,.throttle_supported=true,.wire_capture=capture,.capture_context=&o};
	CHECK(st_term_session_create(&session,&config)==0);
	st_cterm_t *t=st_term_session_cterm(session); t->big_engine->numeric.io.now_ns=now;
	t->api_output=output; t->api_output_context=&o;
	uint64_t start,current; CHECK(st_time_monotonic_ms(&start)==0);
	do { pump(session); CHECK(st_time_monotonic_ms(&current)==0); }
	while (!st_cterm_input_paused(t) && current-start<2000);
	CHECK(st_cterm_input_paused(t) && o.count==6 && o.replies==0 && t->xpos==1);
	st_term_session_set_throttle_speed(session,paced?1000:0);
	/* Real pump ticks accumulate enough time to expose a 100 ms credit
	 * bank. No sleeping inside the parser or a framebuffer callback. */
	for (unsigned i=0;i<20;++i) { pump(session); CHECK(o.count==6 && o.replies==0); }
	local_ns+=UINT64_C(1000000000); pump(session);
	CHECK(!st_cterm_input_paused(t));
	if (paced) CHECK(o.count<=7 && t->xpos<=2 && o.replies==0);
	else CHECK(o.count==36 && o.replies==7 && memcmp(o.reply,"\033[1;27R",7)==0);
	st_term_session_set_throttle_speed(session,0);
	for (unsigned i=0;i<10 && o.replies==0;++i) pump(session);
	CHECK(o.count==36 && memcmp(o.wire,"I#t 1:ABCDEFGHIJKLMNOPQRSTUVWXYZ\033[6n",36)==0);
	CHECK(o.replies==7 && memcmp(o.reply,"\033[1;27R",7)==0 && t->xpos==27 && t->ypos==1);
	st_term_session_destroy(&session); CHECK(st_connection_close(connection)==0);
	st_profile_dispose(&profile); st_font_catalog_destroy(fonts); st_console_destroy(console);
}
int main(void) { for (unsigned utf8=0;utf8<2;++utf8) for (unsigned paced=0;paced<2;++paced) run(utf8,paced); return 0; }
