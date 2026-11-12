/* Independent I/R state/call-order expectations from BIG chapters27/54/80. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_state.h"
#include "st_bgi_data.h"
#include "io_error.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static const uint8_t palette[16] = {0,1,2,3,4,5,20,7,56,57,58,59,60,61,62,63};
struct fixture {
	struct st_big_state state;
	struct st_big_state_sink sink;
	uint8_t cache;
	char events[128]; size_t count;
	unsigned installs, palettes, singles, pages;
	int fail_at;
	uint16_t change_driver;
};
static int event(struct fixture *f, char c)
{
	CHECK(f->count < sizeof(f->events)); f->events[f->count++] = c;
	return f->fail_at != 0 && f->count == (size_t)f->fail_at ? IO_ERROR_IO : 0;
}
static int install(void *context, const struct st_big_mode *mode)
{
	struct fixture *f = context; ++f->installs;
	CHECK(f->state.bgi.mode == mode->resolution-1); /* Recorded before native work. */
	if (f->change_driver != 0) f->state.bgi.driver = f->change_driver;
	return event(f,'M');
}
static int colors(void *context, size_t first, const uint8_t *codes, size_t count, bool bulk)
{
	struct fixture *f = context;
	if (bulk) {
		++f->palettes; CHECK(first == 0 && count == 16 && memcmp(codes,palette,16) == 0);
		CHECK(f->state.bgi.x == 0 && f->state.bgi.y == 0 && f->state.bgi.clip == 1);
	} else {
		CHECK(first == f->singles && count == 1 && codes[0] == palette[first]); ++f->singles;
	}
	CHECK(memcmp(f->state.bgi.palette+first,codes,count) == 0);
	return event(f,bulk ? 'B' : 'P');
}
static int page(void *context, bool visual, uint16_t value)
{
	struct fixture *f = context; ++f->pages; CHECK(value == 0);
	CHECK((visual ? f->state.bgi.visual_page : f->state.bgi.active_page) == 0);
	/* Application tail is after BOTH page setters. */
	CHECK(f->state.application.marker == 7 && f->state.application.colors[0] == 3);
	return event(f,visual ? 'V' : 'A');
}
static void prepare(struct fixture *f)
{
	memset(f,0,sizeof(*f)); st_big_state_init(&f->state); f->cache = 3;
	f->sink = (struct st_big_state_sink){f,install,colors,page};
	f->state.application.hollow = 1; f->state.application.marker = 7;
	f->state.application.marker_size = 9; f->state.application.fill = 18;
	f->state.application.write_mode = 1;
	for (size_t i = 0; i < 4; ++i) f->state.application.colors[i] = (uint16_t)(i+3);
	memset(f->state.application.patterns,0x69,sizeof(f->state.application.patterns));
	f->state.bgi.write_mode = 1; f->state.bgi.color = 2; f->state.bgi.background = 4;
	f->state.bgi.font = 3; f->state.bgi.text_size = 4;
	f->state.bgi.x = 23; f->state.bgi.y = 47;
	f->state.bgi.saved_ratio[0] = 77; f->state.bgi.effective_ratio[3] = 88;
	f->state.memory_page = 3; f->state.active_page = 2; f->state.visual_page = 1;
	memset(f->state.local_palette,0x5a,sizeof(f->state.local_palette));
	f->state.saved_palette[7] = 0x1234;
}
static int retained(const struct fixture *f)
{
	CHECK(f->state.memory_page == 3 && f->state.active_page == 2 && f->state.visual_page == 1);
	CHECK(f->state.local_palette[0] == 0x5a && f->state.local_palette[16] == 0x5a);
	CHECK(f->state.saved_palette[7] == 0x1234 && f->state.application.patterns[11][7] == 0x69);
	CHECK(f->state.bgi.saved_ratio[0] == 77 && f->state.bgi.effective_ratio[3] == 88);
	return 0;
}
static int defaults(const struct fixture *f, unsigned height)
{
	const struct st_big_state *s = &f->state;
	CHECK(s->bgi.width == 640 && s->bgi.height == height);
	CHECK(s->bgi.viewport[0] == 0 && s->bgi.viewport[1] == 0 && s->bgi.viewport[2] == 639 && s->bgi.viewport[3] == height-1);
	CHECK(s->bgi.clip == 1 && s->bgi.x == 0 && s->bgi.y == 0);
	CHECK(s->bgi.color == 15 && s->bgi.background == 0 && s->bgi.fill_style == 1 && s->bgi.fill_color == 15);
	CHECK(s->bgi.line_style == 0 && s->bgi.line_user == 0 && s->bgi.line_mask == 0xffff && s->bgi.line_width == 1);
	CHECK(s->bgi.font == 0 && s->bgi.direction == 0 && s->bgi.text_size == 1);
	CHECK(s->bgi.justify_x == 0 && s->bgi.justify_y == 2 && s->bgi.write_mode == 0);
	for (size_t i = 0; i < 8; ++i) CHECK(s->bgi.fill_user[i] == 255);
	return 0;
}
static int initialize(void)
{
	const uint16_t selectors[] = {0,2,0x100,0xffff};
	for (size_t i = 0; i < sizeof(selectors)/sizeof(*selectors); ++i) {
		struct fixture f; prepare(&f);
		CHECK(st_big_state_initialize(&f.state,selectors[i],&f.cache,&f.sink) == 0);
		CHECK(f.count == 5 && memcmp(f.events,"MBBVA",5) == 0 && f.cache == 2);
		CHECK(f.state.application.hollow == 0 && f.state.application.marker == 0 && f.state.application.marker_size == 1);
		CHECK(f.state.application.fill == 1 && f.state.application.write_mode == 0);
		for (size_t j = 0; j < 4; ++j) CHECK(f.state.application.colors[j] == 15);
		CHECK(defaults(&f,350) == 0 && retained(&f) == 0);
	}
	struct fixture f; prepare(&f);
	CHECK(st_big_state_initialize(&f.state,1,&f.cache,&f.sink) == 0);
	CHECK(f.singles == 16 && f.installs == 0 && f.pages == 0 && f.cache == 3);
	CHECK(f.state.application.hollow == 0 && f.state.application.marker == 7 && f.state.application.fill == 18);
	CHECK(f.state.bgi.font == 3 && f.state.bgi.write_mode == 1 && f.state.bgi.x == 23 && f.state.bgi.y == 47);
	CHECK(f.state.bgi.background == 0); /* Public setpalette(0) side effect. */
	CHECK(retained(&f) == 0);
	prepare(&f); f.state.bgi.state = 0;
	CHECK(st_big_state_initialize(&f.state,1,&f.cache,&f.sink) == 0);
	CHECK(f.count == 0 && f.state.application.hollow == 0 && f.state.bgi.error == 0);
	prepare(&f); f.state.bgi.state = 2;
	CHECK(st_big_state_initialize(&f.state,0,&f.cache,&f.sink) == 0);
	CHECK(f.count == 3 && memcmp(f.events,"BVA",3) == 0 && f.cache == 2);
	prepare(&f); f.change_driver = 3;
	CHECK(st_big_state_initialize(&f.state,0,&f.cache,&f.sink) == 0);
	CHECK(f.count == 7 && memcmp(f.events,"MBMBBVA",7) == 0); /* Independent driver tests. */
	return 0;
}
static int resolutions(void)
{
	const uint16_t requests[] = {0,1,2,3,4,0x103,0xffff};
	const unsigned heights[] = {200,200,350,480,200,200,200};
	for (size_t i = 0; i < sizeof(requests)/sizeof(*requests); ++i) {
		struct fixture f; prepare(&f);
		CHECK(st_big_state_resolution(&f.state,requests[i],&f.cache,&f.sink) == 0);
		CHECK(f.count == 2 && memcmp(f.events,"MB",2) == 0 && f.pages == 0);
		CHECK(f.cache == (requests[i] == 2 ? 2 : requests[i] == 3 ? 3 : 1));
		CHECK(f.state.application.hollow == 1 && f.state.application.write_mode == 1 && f.state.application.colors[0] == 3);
		CHECK(defaults(&f,heights[i]) == 0 && retained(&f) == 0);
	}
	struct fixture f; prepare(&f); f.state.bgi.driver = 3; f.state.bgi.max_mode = 1;
	CHECK(st_big_state_resolution(&f.state,3,&f.cache,&f.sink) == 0 && f.cache == 2);
	CHECK(f.state.bgi.height == 350 && f.installs == 1);
	prepare(&f); f.state.bgi.driver = 3; f.change_driver = 9;
	CHECK(st_big_state_resolution(&f.state,3,&f.cache,&f.sink) == 0 && f.cache == 2);
	CHECK(f.installs == 2 && f.state.bgi.height == 350); /* Mutated request remains2. */
	prepare(&f); f.state.bgi.driver = 77;
	CHECK(st_big_state_resolution(&f.state,1,&f.cache,&f.sink) == 0 && f.cache == 3);
	CHECK(f.count == 0 && f.state.bgi.x == 0 && f.state.bgi.viewport[3] == 349);
	prepare(&f); f.state.bgi.state = 2;
	CHECK(st_big_state_resolution(&f.state,1,&f.cache,&f.sink) == 0 && f.cache == 1);
	CHECK(f.installs == 0 && f.state.bgi.height == 350 && f.state.bgi.font == 3);
	prepare(&f); f.state.bgi.max_mode = 0; f.cache = 1;
	CHECK(st_big_state_resolution(&f.state,3,&f.cache,&f.sink) == 0 && f.cache == 3);
	CHECK(f.installs == 0 && f.state.bgi.height == 350 && f.state.bgi.error == -10 && f.state.bgi.x == 0);
	return 0;
}
static int failures_and_entry(void)
{
	struct fixture f; prepare(&f); f.fail_at = 1; f.cache = 1;
	CHECK(st_big_state_resolution(&f.state,3,&f.cache,&f.sink) == IO_ERROR_IO);
	CHECK(f.state.bgi.mode == 2 && f.state.bgi.height == 350 && f.cache == 1 && f.state.application.hollow == 1);
	prepare(&f); f.fail_at = 3;
	CHECK(st_big_state_initialize(&f.state,0,&f.cache,&f.sink) == IO_ERROR_IO);
	CHECK(f.count == 3 && f.state.application.hollow == 0 && f.state.application.marker == 7 && f.cache == 3);
	prepare(&f); f.state.bgi.state = 2; f.state.bgi.active_page = 9; f.fail_at = 2;
	CHECK(st_big_state_initialize(&f.state,0,&f.cache,&f.sink) == IO_ERROR_IO);
	CHECK(f.state.bgi.visual_page == 0 && f.state.bgi.active_page == 9 && f.state.application.marker == 7);
	prepare(&f); f.state.bgi.state = 0; f.state.bgi.driver = 77;
	CHECK(st_big_state_initialize(&f.state,0,&f.cache,&f.sink) == IO_ERROR_PROTOCOL && f.state.application.hollow == 0);
	CHECK(f.state.application.marker == 7 && f.pages == 0);
	prepare(&f); st_big_state_enter(&f.state);
	CHECK(f.state.memory_page == 1 && f.state.active_page == 0 && f.state.visual_page == 0);
	CHECK(f.state.local_palette[0] == 16 && memcmp(f.state.local_palette+1,f.state.bgi.palette,16) == 0);
	CHECK(f.state.saved_palette[7] == 0x1234 && f.state.application.marker == 7);
	return 0;
}
static int writing_modes(void)
{
	struct st_big_state s; st_big_state_init(&s);
	s.bgi.error=-11; s.bgi.color=9; s.bgi.ddo_x=777;
	/* Independent normalization oracle: exactly these two original inputs
	 * select XOR, including the legacy3 alias and all decrement wraps. */
	for (unsigned word=0;word<=65535;++word) {
		s.bgi.write_mode=9; s.application.write_mode=7;
		CHECK(st_big_state_write_mode(&s,(uint16_t)word)==0);
		uint16_t expected=word==2 || word==3?1:0;
		CHECK(s.bgi.write_mode==expected && s.application.write_mode==expected);
		CHECK(s.bgi.error==-11 && s.bgi.color==9 && s.bgi.ddo_x==777);
	}
	s.bgi.state=0; s.bgi.write_mode=9; s.application.write_mode=7;
	const uint16_t requests[]={0,1,2,3,32768,32769,65535};
	for (size_t i=0;i<sizeof(requests)/sizeof(requests[0]);++i) {
		CHECK(st_big_state_write_mode(&s,requests[i])==IO_ERROR_PROTOCOL);
		CHECK(s.bgi.write_mode==9 && s.application.write_mode==7 && s.bgi.error==-1);
	}
	CHECK(st_big_state_write_mode(NULL,2)==IO_ERROR_INVALID_ARGUMENT);
	return 0;
}
int main(void)
{
	CHECK(initialize() == 0); CHECK(resolutions() == 0); CHECK(failures_and_entry() == 0); CHECK(writing_modes() == 0); return 0;
}
