/* BIG1.03 I/R state composition; reference27/54/80, ordinary VGA profile. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_state.h"
#include "st_big_number.h"
#include "st_big_fill.h"
#include "st_big_draw.h"
#include "st_bgi_data.h"
#include "io_error.h"
#include <string.h>

/* Original DS00bb restore data, SHA256 de22d2481ea21ce9f5b44f0a5e663099
 * 76a20b3adf0a82a09af8b5a6c35fe2f2. Startup copies these twelve rows once. */
const uint8_t st_big_fill_restore[12][8] = {
	{0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55},
	{0x33,0x33,0xcc,0xcc,0x33,0x33,0xcc,0xcc},
	{0xf0,0xf0,0xf0,0xf0,0x0f,0x0f,0x0f,0x0f},
	{0x00,0x10,0x28,0x44,0x28,0x10,0x00,0x00},
	{0x00,0x00,0x38,0x38,0x54,0x92,0x00,0x00},
	{0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
	{0x00,0x00,0x3c,0x3c,0x3c,0x3c,0x00,0x00},
	{0x00,0x7e,0x7e,0x7e,0x7e,0x7e,0x7e,0x00},
	{0x00,0x00,0x22,0x08,0x00,0x22,0x1c,0x00},
	{0xff,0x7e,0x3c,0x18,0x18,0x3c,0x7e,0xff},
	{0x81,0xc3,0xc3,0xe7,0x66,0x66,0x7e,0x3c},
	{0x20,0x50,0x8c,0x42,0x22,0x22,0x1e,0x00},
};
static bool valid_sink(const struct st_big_state_sink *sink)
{
	return sink != NULL && sink->install != NULL && sink->palette != NULL && sink->page != NULL;
}
static int synchronous(int status) { return status == IO_ERROR_IN_PROGRESS ? IO_ERROR_PROTOCOL : status; }
static void viewport(struct st_big_state *s)
{
	s->bgi.viewport[0] = s->bgi.viewport[1] = 0;
	s->bgi.viewport[2] = (uint16_t)(s->bgi.width-1);
	s->bgi.viewport[3] = (uint16_t)(s->bgi.height-1);
	s->bgi.clip = 1; s->bgi.x = s->bgi.y = 0;
}
static int defaults(struct st_big_state *s, const struct st_big_state_sink *sink)
{
	if (s->bgi.state == 0) { s->bgi.error = -1; return IO_ERROR_PROTOCOL; }
	viewport(s);
	memcpy(s->bgi.palette,st_bgi_ega_defaults,16);
	if (sink != NULL) {
		int status = synchronous(sink->palette(sink->context,0,s->bgi.palette,16,true));
		if (status != 0) return status;
	}
	s->bgi.background = 0;
	static const uint8_t full[8] = {255,255,255,255,255,255,255,255};
	int status = st_big_draw_color(s,15);
	if (status == 0) status = st_big_fill_custom(s,full,15);
	if (status == 0) status = st_big_fill_builtin(s,1,15);
	if (status != 0) return status;
	s->bgi.line_style = s->bgi.line_user = 0;
	s->bgi.line_mask = 0xffff; s->bgi.line_width = 1;
	s->bgi.ddo_line_width = 1; s->bgi.device_line_width = 1;
	s->bgi.font = s->bgi.direction = 0; s->bgi.text_size = 1;
	s->bgi.justify_x = 0; s->bgi.justify_y = 2;
	s->bgi.write_mode = 0;
	s->bgi.x = s->bgi.y = s->bgi.ddo_x = s->bgi.ddo_y = 0;
	s->bgi.device_x = s->bgi.device_y = 0;
	return 0;
}
void st_big_state_enter(struct st_big_state *s)
{
	if (s == NULL) return;
	s->memory_page = 1; s->active_page = s->visual_page = 0;
	/* A defined entry snapshot replaces the original uninitialized X8 local.
	 * I/R do not refresh it while the interpreter frame remains active. */
	s->local_palette[0] = 16; memcpy(s->local_palette+1,s->bgi.palette,16);
}
void st_big_state_init(struct st_big_state *s)
{
	if (s == NULL) return;
	memset(s,0,sizeof(*s));
	s->bgi.state = 3; s->bgi.driver = 9; s->bgi.mode = 1; s->bgi.max_mode = 2;
	s->bgi.width = 640; s->bgi.height = 350; s->bgi.line_distance = 1;
	(void)defaults(s,NULL);
	s->application.marker_size = 1; s->application.fill = 1;
	for (size_t i = 0; i < 4; ++i) s->application.colors[i] = 15;
	memcpy(s->application.patterns,st_big_fill_restore,sizeof(s->application.patterns));
	st_big_state_enter(s);
}
int st_big_state_install(struct st_big_state *s, unsigned resolution, const struct st_big_state_sink *sink)
{
	const struct st_big_mode *mode = st_big_mode(resolution);
	if (s == NULL || mode == NULL || !valid_sink(sink)) return IO_ERROR_INVALID_ARGUMENT;
	if (s->bgi.state == 2) return 0;
	if ((int32_t)(resolution-1) > st_big_number_signed(s->bgi.max_mode)) { s->bgi.error = -10; return 0; }
	s->bgi.mode = (uint16_t)(resolution-1);
	int status = synchronous(sink->install(sink->context,mode));
	if (status != 0) return status;
	s->bgi.width = mode->width; s->bgi.height = mode->height;
	s->bgi.active_page = s->bgi.visual_page = 0;
	return defaults(s,sink);
}
static int single_palette(struct st_big_state *s, size_t pen, uint8_t value, const struct st_big_state_sink *sink)
{
	if (s->bgi.state == 0) return 0;
	s->bgi.palette[pen] = value;
	if (pen == 0) s->bgi.background = 0;
	return synchronous(sink->palette(sink->context,pen,&s->bgi.palette[pen],1,false));
}
int st_big_state_initialize(struct st_big_state *s, uint16_t selector, uint8_t *cache, const struct st_big_state_sink *sink)
{
	if (s == NULL || cache == NULL || !valid_sink(sink)) return IO_ERROR_INVALID_ARGUMENT;
	int32_t op = st_big_number_signed(selector);
	if (op < 0 || op > 1) op = 0;
	s->application.hollow = 0;
	if (op == 0) {
		int status = 0;
		if (s->bgi.driver == 9) status = st_big_state_install(s,2,sink);
		if (status == 0 && s->bgi.driver == 3) status = st_big_state_install(s,2,sink);
		if (status == 0) status = defaults(s,sink);
		if (status != 0) return status;
		s->bgi.visual_page = 0;
		status = synchronous(sink->page(sink->context,true,0));
		if (status != 0) return status;
		s->bgi.active_page = 0;
		status = synchronous(sink->page(sink->context,false,0));
		if (status != 0) return status;
		s->application.marker = 0; s->application.marker_size = 1;
		for (size_t i = 0; i < 4; ++i) s->application.colors[i] = 15;
		s->application.fill = 1; s->application.write_mode = 0; *cache = 2;
	}
	if (op == 1) {
		for (size_t i = 0; i < 16; ++i) {
			int status = single_palette(s,i,st_bgi_ega_defaults[i],sink);
			if (status != 0) return status;
		}
	}
	return 0;
}
int st_big_state_resolution(struct st_big_state *s, uint16_t requested, uint8_t *cache, const struct st_big_state_sink *sink)
{
	if (s == NULL || cache == NULL || !valid_sink(sink)) return IO_ERROR_INVALID_ARGUMENT;
	int32_t op = st_big_number_signed(requested);
	if (op < 1 || op > 3) op = 1;
	if (s->bgi.driver == 3) {
		if (op == 3) op = 2;
		if (op == 1 || op == 2) {
			int status = st_big_state_install(s,(unsigned)op,sink);
			if (status != 0) return status;
			*cache = (uint8_t)op;
		}
	}
	if (s->bgi.driver == 9) {
		int status = st_big_state_install(s,(unsigned)op,sink);
		if (status != 0) return status;
		*cache = (uint8_t)op;
	}
	viewport(s);
	return 0;
}
int st_big_state_write_mode(struct st_big_state *s, uint16_t requested)
{
	if (s == NULL) return IO_ERROR_INVALID_ARGUMENT;
	uint16_t mode = requested == 3 ? 2 : requested;
	mode = (uint16_t)(mode-1u);
	if (st_big_number_signed(mode) < 0 || st_big_number_signed(mode) > 1) mode = 0;
	if (s->bgi.state == 0) { s->bgi.error = -1; return IO_ERROR_PROTOCOL; }
	/* The original setter only retains the device's AL&1 line-mode bit.
	 * Individual raster consumers decide whether to use it. */
	s->bgi.write_mode = mode;
	s->application.write_mode = mode;
	return 0;
}
int st_big_state_color(struct st_big_state *s, uint16_t selector, uint16_t color)
{
	if (s == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (st_big_number_signed(selector) < 0 || st_big_number_signed(selector) > 3) selector = 0;
	s->application.colors[selector] = color;
	return 0;
}
