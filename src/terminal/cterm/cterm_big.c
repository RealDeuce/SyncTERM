/* BIG1.03 terminal I#/pair acquisition and I command (references8/27/54). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "st_console_test.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "st_bgi_data.h"
#include "st_big_draw.h"
#include "st_big_random.h"
#include "st_big_text.h"
#include "st_big_fill.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "io_error.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint8_t attention[] = {'I','#'};
static const struct st_graphics_dialect dialect = {
	.attention = attention, .attention_size = 2, .chain_indicator = true,
	.chain = '>', .number_digits = 4,
};
static const struct st_graphics_parameter point[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter flood[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y},
	{.kind=ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter arc[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y},
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter ellipse[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y},
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y},
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter line[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y},
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter relative[] = {
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter move[] = {
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X}};
static const struct st_graphics_parameter input_parameters[] = {
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter scalar[] = {{.kind = ST_GRAPHICS_NUMBER}};
static int style_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int move_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int bar_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int extended_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int image_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static const struct st_graphics_command commands[] = {{'I',scalar,1,NULL},{'R',scalar,1,NULL},
	{'<',input_parameters,3,NULL},{'T',scalar,1,style_next},{'P',point,2,NULL},
	{'L',line,4,NULL},{'D',point,2,NULL},{'d',relative,4,NULL},
	{'K',arc,5,NULL},{'J',ellipse,6,NULL},{'O',flood,3,NULL},{'Q',line,4,NULL},
	{'V',arc,5,NULL},{'Y',ellipse,6,NULL},
	{'B',line,4,bar_next},
	{'Z',line,4,NULL},
	{'F',flood,3,NULL},
	{'G',scalar,1,image_next},
	{'S',input_parameters,2,NULL},
	{'E',input_parameters,3,NULL},
	{'W',point,2,NULL},
	{'t',NULL,0,NULL},
	{'?',scalar,1,NULL},
	{'m',move,2,move_next},
	{'p',input_parameters,2,NULL},
	{'M',scalar,1,NULL},{'C',input_parameters,2,NULL},{'A',scalar,1,NULL},{'H',scalar,1,NULL},
	{'g',scalar,1,NULL},{'X',scalar,1,extended_next},{'&',NULL,0,NULL}};

static bool marker_kind(const struct st_graphics_command_view *view)
{
	return st_big_number_signed((uint16_t)view->arguments[0].value)<=1;
}
static int bar_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	if (view->count<4) return IO_ERROR_PROTOCOL;
	*parameter=(struct st_graphics_parameter){.kind=view->count==4?ST_GRAPHICS_NUMBER:ST_GRAPHICS_END};
	return 0;
}
static int image_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	uint16_t operation=(uint16_t)view->arguments[0].value;
	size_t count=operation==0 || operation==3?8:operation==1?6:operation==2?4:operation<=6?2:1;
	*parameter=(struct st_graphics_parameter){.kind=view->count<count?ST_GRAPHICS_NUMBER:ST_GRAPHICS_END};
	if (operation<4 && view->count>=2) parameter->axis=view->count%2==0?ST_GRAPHICS_X:ST_GRAPHICS_Y;
	return 0;
}
static int move_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	if (view->count<2 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	bool absolute=st_big_number_signed((uint16_t)view->arguments[0].value)>1;
	*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	if (view->count<(absolute?3u:4u))
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER,
		    .axis=absolute || view->count==3?ST_GRAPHICS_Y:ST_GRAPHICS_UNSCALED};
	return 0;
}
static int style_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	*parameter=(struct st_graphics_parameter){.kind=view->count<(marker_kind(view)?3u:5u)?ST_GRAPHICS_NUMBER:ST_GRAPHICS_END};
	return 0;
}
static int reseed(struct cterm_big *big)
{
	uint64_t seconds;
	int status=big->random_seconds(big,&seconds);
	if (status==IO_ERROR_IN_PROGRESS) return IO_ERROR_PROTOCOL;
	if (status!=0) return status;
	if (big->reset_pending) return IO_ERROR_INTERRUPTED;
	st_big_random_seed(&big->numeric.random_state,(uint16_t)seconds);
	return 0;
}
static int extended_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	struct cterm_big *big=((struct st_big_numeric *)context)->io.context;
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	if ((uint16_t)view->arguments[0].value==0) {
		if (view->count>1 && !view->arguments[view->count-1].has_value) return IO_ERROR_PROTOCOL;
		uint16_t value=view->count>1?(uint16_t)view->arguments[view->count-1].value:0;
		switch (view->count) {
		case 1: {
			int status=st_big_draw_color(&big->state,big->state.application.colors[0]);
			if (status==0) status=reseed(big);
			if (status!=0) return status;
			break;
		}
		case 2: big->spray.x=value; break;
		case 3: big->spray.y=value; break;
		case 4: big->spray.width=value; break;
		case 5: big->spray.height=value; break;
		case 6: big->spray.count=value; return 0;
		default: return IO_ERROR_PROTOCOL;
		}
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER,
		    .axis=view->count==5?ST_GRAPHICS_UNSCALED:view->count%2?ST_GRAPHICS_X:ST_GRAPHICS_Y};
		return 0;
	}
	if ((uint16_t)view->arguments[0].value==2) {
		if (view->count==1) {
			int status=reseed(big); if (status!=0) return status;
		} else {
			if (!view->arguments[view->count-1].has_value) return IO_ERROR_PROTOCOL;
			uint16_t value=(uint16_t)view->arguments[view->count-1].value;
			if (view->count==2) big->numeric.random_from=value;
			else if (view->count==3) {
				big->numeric.random_to=value;
				return st_big_random_repair(&big->numeric.random_from,&big->numeric.random_to);
			} else return IO_ERROR_PROTOCOL;
		}
		parameter->kind=ST_GRAPHICS_NUMBER;
		return 0;
	}
	if ((uint16_t)view->arguments[0].value==5) {
		if (view->count==1) {
			st_big_flow_settings_begin(&big->flow_settings);
			big->flow_settings_arguments=1;
		} else {
			if (view->count!=big->flow_settings_arguments+1 || !view->arguments[view->count-1].has_value)
				return IO_ERROR_PROTOCOL;
			int status=st_big_flow_settings_value(&big->flow_settings,&big->control,&big->numeric.loop,
			    (uint16_t)view->arguments[view->count-1].value);
			if (status!=0) return status;
			big->flow_settings_arguments=view->count;
		}
		bool read;
		int status=st_big_flow_settings_next(&big->flow_settings,&big->control,&read);
		if (status!=0) return status;
		if (read) parameter->kind=ST_GRAPHICS_NUMBER;
		return 0;
	}
	if ((uint16_t)view->arguments[0].value==1) {
		if (view->count<3) *parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER};
		else {
			if (!view->arguments[2].has_value) return IO_ERROR_PROTOCOL;
			if ((uint16_t)view->arguments[2].value==1 && view->count<11)
				*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER};
		}
	}
	if ((uint16_t)view->arguments[0].value==4 && view->count<7) {
		if (view->count>=2 && st_big_zone_id((uint16_t)view->arguments[1].value)>=9997) return 0;
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER};
		if (view->count>=2 && view->count<=5)
			parameter->axis=view->count%2==0?ST_GRAPHICS_X:ST_GRAPHICS_Y;
	}
	if ((uint16_t)view->arguments[0].value==6 && view->count<3)
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER,
		    .axis=view->count==1?ST_GRAPHICS_X:ST_GRAPHICS_Y};
	if ((uint16_t)view->arguments[0].value==8 && view->count<5)
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER};
	if ((uint16_t)view->arguments[0].value==3 && view->count<5)
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER};
	return 0;
}

static struct cterm_big *owner(void *context)
{
	return ((struct st_big_numeric *)context)->io.context;
}
static int now(struct cterm_big *big, uint64_t *value)
{
	return big->numeric.io.now_ns == NULL ? st_time_monotonic_ns(value)
	    : big->numeric.io.now_ns(big,value);
}
static int random_seconds(void *context, uint64_t *value)
{
	(void)context;
	/* time(NULL) is a local wall-clock seed observation. Its low WORD
	 * feeds the original generator; numeric waits remain monotonic. */
	time_t seconds=time(NULL);
	if (seconds==(time_t)-1) return IO_ERROR_IO;
	*value=(uint64_t)seconds;
	return 0;
}
static void bell(void *context)
{
	cterm_render_beep(((struct cterm_big *)context)->terminal);
}
static int text(void *context, const uint8_t *bytes, size_t size)
{
	struct cterm_big *big = owner(context); size_t used;
	int status = cterm_input_write(big->terminal,bytes,size,&used,CTERM_INPUT_WIRE);
	if (status == 0 && used != size) status = IO_ERROR_IO;
	return status == 0 && big->reset_pending ? IO_ERROR_INTERRUPTED : status;
}
static int boundary(void *context)
{
	struct cterm_big *big = owner(context);
	int status = cterm_text_boundary(big->terminal);
	if (status == 0 && big->terminal->status_sub != NULL)
		status = cterm_text_boundary(big->terminal->status_sub);
	if (status == 0 && big->reset_pending) return IO_ERROR_INTERRUPTED;
	if (status != 0) return status;
	/* New attention creates a frame; chaining keeps its page/palette locals. */
	if (big->starting_body) return 0;
	if (!st_graphics_parser_in_command(big->parser)) {
		st_big_state_enter(&big->state); st_big_control_enter(&big->control);
	}
	uint64_t sample;
	status = now(big,&sample);
	if (status != 0) return status;
	big->pair_deadline = UINT64_MAX-sample < ST_BIG_NUMERIC_TIMEOUT_NS
	    ? UINT64_MAX : sample+ST_BIG_NUMERIC_TIMEOUT_NS;
	big->pair_sample = sample; big->pair_wait = big->pair_body = true;
	return 0;
}
static void body_entry(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (!big->pair_body) return;
	big->control.frame.command=view->code; big->numeric.loop.command_read=false;
	big->pair_wait=big->pair_body=false;
}
static int field(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct cterm_big *big=owner(context);
	if (big->reset_pending) return IO_ERROR_INTERRUPTED;
	body_entry(big,view);
	if (view->code=='G' && view->count>=1) {
		if (view->count==1) st_big_state_g_entry(&big->state,big->numeric.context.resolution);
		int status=cterm_big_image_field(big,view); if (status!=0) return status;
	}
	if (view->code=='<' && view->count==1) {
		if (!view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		big->control.input.append_cr=(uint8_t)view->arguments[0].value;
	}
	if (view->code=='T' && view->count==2 && marker_kind(view)) {
		big->state.application.marker=(uint8_t)view->arguments[1].value;
		if (big->state.application.marker>7) big->state.application.marker=7;
	}
	if (view->count==0 && (view->code=='P' || view->code=='L' || view->code=='D' || view->code=='d' || view->code=='Q')) {
		int status=st_big_draw_color(&big->state,big->state.application.colors[view->code=='P'?0:1]);
		if (status!=0) return status;
	}
	if (view->code=='X' && view->count>=1 && view->arguments[0].has_value
	    && (uint16_t)view->arguments[0].value==4) {
		int status=cterm_big_zone_field(big,view);
		if (status!=0) return status;
	}
	if (view->code=='X' && view->count>=3 && view->arguments[0].has_value
	    && (uint16_t)view->arguments[0].value==1) {
		int status;
		if (view->count==3) {
			if (!view->arguments[1].has_value || !view->arguments[2].has_value) return IO_ERROR_PROTOCOL;
			status=st_big_fill_edit_begin(&big->fill_edit,(uint16_t)view->arguments[1].value,(uint16_t)view->arguments[2].value);
		} else {
			if (!view->arguments[view->count-1].has_value) return IO_ERROR_PROTOCOL;
			status=st_big_fill_edit_row(&big->fill_edit,&big->state,(uint16_t)view->arguments[view->count-1].value);
		}
		if (status!=0) return status;
	}
	/* X6 commits X before entering Y, even if Y later waits or fails. */
	if (view->code=='X' && view->count==2 && view->arguments[0].has_value
	    && (uint16_t)view->arguments[0].value==6) {
		if (!view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		big->control.pointer.step_x=(uint16_t)view->arguments[1].value;
	}
	return st_big_numeric_field(context,view,parameter);
}
static int palette(void *context, size_t first, const uint8_t *codes, size_t count, bool bulk)
{
	struct cterm_big *big = context; uint32_t rgb[16]; (void)bulk;
	if (first > 16 || count > 16-first) return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < count; ++i) {
		/* Cache bytes retain all eight bits; the ordinary attribute
		 * palette exposes six data bits in the declared VGA profile. */
		const uint16_t *c = st_bgi_ega_colors[codes[i]&63u];
		rgb[i] = (uint32_t)(c[0]>>8)<<16 | (uint32_t)(c[1]>>8)<<8 | (c[2]>>8);
	}
	return st_console_set_palette(big->terminal->screen.console,
	    (uint16_t)(CTERM_BIG_PALETTE_FIRST+first),rgb,count);
}
static int page(void *context, bool visual, uint16_t value)
{
	return cterm_big_page(context,visual,value);
}
static int clear_pixels(void *context, const struct st_pixel_buffer *pixels,
    struct st_console_rect *changed)
{
	(void)context;
	for (size_t i = 0; i < (size_t)pixels->width*pixels->height; ++i)
		pixels->primary[i] = pixels->alternate[i] = CTERM_BIG_PALETTE_FIRST;
	*changed = (struct st_console_rect){0,0,pixels->width,pixels->height};
	return 0;
}
static int install(void *context, const struct st_big_mode *mode)
{
	struct cterm_big *big = context; struct cterminal *t = big->terminal;
	struct st_console_config config = {
		.columns = mode->columns, .rows = mode->rows, .cell_width = 8,
		.cell_height = mode->cell_height, .pixel_width = mode->width,
		.pixel_height = mode->height, .aspect_width = 4, .aspect_height = 3,
		.wrap = true, .blink_mode = ST_CONSOLE_BLINK_PC,
		.preserve_window_size = true,
	};
	unsigned slot=mode->resolution-1;
	int status=0;
	if (!big->console_font_valid[slot]) {
		struct st_console_font font;
		if (!st_big_console_font(mode->resolution,&font)) return IO_ERROR_NOT_FOUND;
		status=st_console_register_static_font(t->screen.console,&font,&big->console_fonts[slot]);
		if (status != 0) return status;
		big->console_font_valid[slot]=true;
	}
	status = st_console_get_scaling_mode(t->screen.console,&config.scaling_mode);
	if (status == 0) status = st_console_get_color_emoji(t->screen.console,&config.color_emoji);
	if (status == 0) status = cterm_reconfigure_console(t,&config);
	if (status != 0) return status;
	cterm_big_video_reset(big);
	/* BIOS mode installation selects the original bank. A previously active
	 * size must not be stretched, nor a retained remote font substituted. */
	uint16_t identifier=big->console_fonts[slot];
	t->screen.font_identifiers[0]=identifier; t->screen.font_valid[0]=true;
	t->screen.font_codepages[0]=ST_CODEPAGE_CP437; t->screen.codepage=ST_CODEPAGE_CP437;
	t->screen.fonts=(struct st_console_font_selection){.normal=identifier, .bright=identifier,
	    .blink=identifier, .bright_blink=identifier};
	for (size_t i=0;i<5;++i) t->screen.selectors[i]=0;
	for (size_t i=0;i<4;++i) t->altfont[i]=0;
	cterm_text_forget(t); st_unicode_presentation_reset(t->screen.unicode);
	t->accumulator = t->saved_accumulator = NULL; t->sequence = 0;
	t->escbuf[0] = 0; t->negative = false;
	t->attr = 7;
	CTERM_FREE_AND_CLEAR(t->fg_tc_str); CTERM_FREE_AND_CLEAR(t->bg_tc_str);
	cterm_render_attribute_colours(t,t->attr,&t->fg_color,&t->bg_color);
	cterm_render_set_attribute(t,t->attr);
	cterm_render_set_colours(t,t->fg_color,t->bg_color);
	cterm_apply_window(t);
	cterm_render_clear_window(t); cterm_set_position(t,1,1);
	cterm_render_set_cursor(t,t->cursor);
	status = cterm_render_take_error(t);
	if (status == 0) status = st_console_modify_pixels(t->screen.console,clear_pixels,NULL);
	return status;
}
static int output_byte(void *context, enum st_big_output_kind kind, uint8_t byte)
{
	struct cterm_big *big=context;
	if (big->reset_pending) return IO_ERROR_INTERRUPTED;
	if (kind==ST_BIG_OUTPUT_QUERY) cterm_respond(big->terminal,(const char *)&byte,1);
	else if (kind!=ST_BIG_OUTPUT_REPLY) return IO_ERROR_INVALID_ARGUMENT;
	else if (big->terminal->keystroke_cb!=NULL)
		big->terminal->keystroke_cb((const char *)&byte,1,big->terminal->keystroke_cbdata);
	return big->reset_pending?IO_ERROR_INTERRUPTED:0;
}
static int begin_footer(struct cterm_big *big, const struct st_graphics_command_view *view)
{
	if (big->reset_pending) return IO_ERROR_INTERRUPTED;
	st_big_footer_begin(&big->footer,view->chained); big->footer_active=true;
	return IO_ERROR_IN_PROGRESS;
}
static int command(void *context, const struct st_graphics_command_view *view)
{
	struct cterm_big *big = owner(context);
	if (big->reset_pending) return IO_ERROR_INTERRUPTED;
	bool initial=big->pair_body; body_entry(big,view);
	if (view->code=='&') {
		if (!initial) return begin_footer(big,view);
		big->footer.chained=view->chained; cterm_big_setup_begin(big);
		return IO_ERROR_IN_PROGRESS;
	}
	if (view->code=='<') return cterm_big_collect_begin(big,view);
	if (view->code=='t') { cterm_big_pause_begin(big,view->chained); return IO_ERROR_IN_PROGRESS; }
	if (view->code=='?') return cterm_big_query_begin(big,view);
	if (view->code=='p') {
		if (view->count!=2 || !view->arguments[0].has_value || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		int status=cterm_big_console_position(big,(uint16_t)view->arguments[0].value,(uint16_t)view->arguments[1].value);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='K' || view->code=='J' || view->code=='O' || view->code=='Q' || view->code=='V' || view->code=='Y') {
		int status=cterm_big_draw_curve(big,view);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='B') return cterm_big_bar_begin(big,view);
	if (view->code=='F') return cterm_big_flood_begin(big,view);
	if (view->code=='W') {
		if (view->count!=2 || !view->arguments[0].has_value || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		struct cterm_big_text_output *output=&big->text_output;
		output->x=(uint16_t)view->arguments[0].value; output->y=(uint16_t)view->arguments[1].value;
		/* No colour publication until both coordinates return. The
		 * accepted colour precedes text waits and is not restored. */
		int status=st_big_draw_color(&big->state,big->state.application.colors[3]);
		if (status!=0) return status;
		status=st_big_stream_text(&output->stream,&big->numeric,&big->text,&big->control);
		if (status!=0) return status;
		output->active=true; output->chained=view->chained;
		return IO_ERROR_IN_PROGRESS;
	}
	if (view->code=='Z') {
		int status=cterm_big_draw_z(big,view);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='E') {
		uint16_t arguments[3];
		if (view->count!=3) return IO_ERROR_PROTOCOL;
		for (size_t i=0;i<3;++i) {
			if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
			arguments[i]=(uint16_t)view->arguments[i].value;
		}
		int status=st_big_text_e(&big->state,&big->fonts,arguments);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='S') {
		if (view->count!=2 || !view->arguments[0].has_value || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		int status=0;
		/* Original birch/plywood then pine/cone tests occur after BOTH
		 * class0 reads. Internal BGI setters do not inherit these gates. */
		if (big->feature_gates[2][0]==big->feature_gates[2][1]
		    && big->feature_gates[0][0]==big->feature_gates[0][1]) {
			const struct st_big_state_sink sink={.context=big,.palette=palette};
			status=st_big_state_palette(&big->state,(uint16_t)view->arguments[0].value,
			    (uint16_t)view->arguments[1].value,&sink);
		}
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='G') {
		if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		uint16_t operation=(uint16_t)view->arguments[0].value;
		if (view->count==1) {
			st_big_state_g_entry(&big->state,big->numeric.context.resolution);
			big->image_operation=operation;
			return begin_footer(big,view);
		}
		if (operation<4) {
			int status=cterm_big_image_command(big,view);
			return status==0?begin_footer(big,view):status;
		}
		if (view->count!=2 || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		const struct st_big_state_sink sink={big,install,palette,page};
		int status=st_big_state_g_page(&big->state,operation,(uint16_t)view->arguments[1].value,big->numeric.context.resolution,&sink);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='T') {
		int status=0;
		if (marker_kind(view)) {
			if (view->count!=3 || !view->arguments[2].has_value) return IO_ERROR_PROTOCOL;
			big->state.application.marker_size=(uint8_t)view->arguments[2].value;
			if (big->state.application.marker_size>8 || big->state.application.marker_size<1) big->state.application.marker_size=1;
		} else {
			if (view->count!=5 || !view->arguments[4].has_value) return IO_ERROR_PROTOCOL;
			int32_t style=st_big_number_signed((uint16_t)view->arguments[1].value);
			if (style<0 || style>4) style=4;
			uint16_t width=(uint16_t)view->arguments[4].value;
			if (width==1) width=3;
			if (width==0) width=1;
			int32_t factor=st_big_number_signed((uint16_t)view->arguments[3].value);
			if (st_big_number_signed(width)<0 || st_big_number_signed(width)>3) factor=0;
			uint16_t pattern=(uint16_t)(st_big_number_signed((uint16_t)view->arguments[2].value)*factor);
			status=st_big_draw_style(&big->state,(uint16_t)style,pattern,width);
		}
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='P') {
		int status=cterm_big_draw_p(big,view);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='L' || view->code=='D' || view->code=='d') {
		int status=cterm_big_draw_line(big,view);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='m') {
		if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		uint16_t operation=(uint16_t)view->arguments[0].value;
		bool absolute=st_big_number_signed(operation)>1;
		if (view->count!=(absolute?3u:4u)) return IO_ERROR_PROTOCOL;
		for (size_t i=1;i<view->count;++i) if (!view->arguments[i].has_value) return IO_ERROR_PROTOCOL;
		uint16_t x=(uint16_t)view->arguments[1].value, y=(uint16_t)view->arguments[2].value;
		if (!absolute) {
			if (operation!=0) x=(uint16_t)(0u-x);
			/* Like d, the original negates the Y flag, not deltaY.
			 * moverel reads current public Y before current public X. */
			y=(uint16_t)(big->state.bgi.y+(uint16_t)view->arguments[3].value);
			x=(uint16_t)(big->state.bgi.x+x);
		}
		int status=st_big_draw_move(&big->state,x,y);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='g') {
		if (view->count!=1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		big->numeric.context.scaling=(uint16_t)view->arguments[0].value;
		return begin_footer(big,view);
	}
	if (view->code=='H') {
		if (view->count!=1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		int status=st_big_state_hollow(&big->state,(uint16_t)view->arguments[0].value);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='M') {
		if (view->count!=1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		int status=st_big_state_write_mode(&big->state,(uint16_t)view->arguments[0].value);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='C') {
		if (view->count!=2 || !view->arguments[0].has_value || !view->arguments[1].has_value) return IO_ERROR_PROTOCOL;
		int status=st_big_state_color(&big->state,(uint16_t)view->arguments[0].value,(uint16_t)view->arguments[1].value);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='A') {
		if (view->count!=1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		int status=st_big_fill_select(&big->state,(uint16_t)view->arguments[0].value);
		return status==0?begin_footer(big,view):status;
	}
	if (view->code=='X') {
		if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		uint16_t selector=(uint16_t)view->arguments[0].value;
		if (selector==0) {
			int status=cterm_big_draw_spray(big);
			return status==0?begin_footer(big,view):status;
		}
		if (selector==8) return cterm_big_rotation_begin(big,view);
		if (selector==5) return begin_footer(big,view);
		if (selector==2) return begin_footer(big,view);
		if (selector==3) {
			if (view->count!=5) return IO_ERROR_PROTOCOL;
			uint16_t ratios[4];
			for (size_t i=0;i<4;++i) {
				if (!view->arguments[i+1].has_value) return IO_ERROR_PROTOCOL;
				ratios[i]=(uint16_t)view->arguments[i+1].value;
			}
			int status=st_big_text_x3(&big->state,ratios,NULL);
			return status==0?begin_footer(big,view):status;
		}
		if (selector==1) {
			int status;
			if (view->count==3 && view->arguments[1].has_value && view->arguments[2].has_value)
				status=st_big_fill_edit_begin(&big->fill_edit,(uint16_t)view->arguments[1].value,(uint16_t)view->arguments[2].value);
			else if (view->count==11 && view->arguments[10].has_value)
				status=st_big_fill_edit_row(&big->fill_edit,&big->state,(uint16_t)view->arguments[10].value);
			else return IO_ERROR_PROTOCOL;
			if (status==0) status=st_big_fill_edit_finish(&big->fill_edit,&big->state);
			if (status!=0) return status;
		} else if (selector==4) {
			int status=cterm_big_zone_begin(big,view);
			if (status!=0) return status;
		} else if (selector==6) {
			if (view->count!=3 || !view->arguments[2].has_value) return IO_ERROR_PROTOCOL;
			big->control.pointer.step_y=(uint16_t)view->arguments[2].value;
			if (st_big_number_signed(big->control.pointer.step_x)<1) big->control.pointer.step_x=1;
			if (st_big_number_signed(big->control.pointer.step_y)<1) big->control.pointer.step_y=1;
			if (st_big_number_signed(big->control.pointer.step_x)>15) big->control.pointer.step_x=15;
			if (st_big_number_signed(big->control.pointer.step_y)>15) big->control.pointer.step_y=15;
		} else if (selector<=8) {
			/* Retain the existing unsupported-command fallback until the
			 * other known selectors have their own implemented operators. */
			unsigned mask=1u<<('X'%8);
			if ((big->warned['X'/8]&mask)==0) {
				big->warned['X'/8]|=(uint8_t)mask;
				cterm_log(big->terminal,ST_CTERM_LOG_WARNING,"Unsupported BIG X selector %u",(unsigned)selector);
			}
		}
		return begin_footer(big,view);
	}
	const struct st_big_state_sink sink = {big,install,palette,page};
	if ((view->code!='I' && view->code!='R') || view->count!=1 || !view->arguments[0].has_value)
		return IO_ERROR_PROTOCOL;
	int status=view->code=='I'
	    ?st_big_state_initialize(&big->state,(uint16_t)view->arguments[0].value,&big->numeric.context.resolution,&sink)
	    :st_big_state_resolution(&big->state,(uint16_t)view->arguments[0].value,&big->numeric.context.resolution,&sink);
	return status==0?begin_footer(big,view):status;
}
static void unknown(void *context, uint8_t code)
{
	struct cterm_big *big = owner(context);
	unsigned mask = 1u<<(code%8);
	if ((big->warned[code/8]&mask) != 0) return;
	big->warned[code/8] |= (uint8_t)mask;
	cterm_log(big->terminal,ST_CTERM_LOG_WARNING,"Unsupported BIG command 0x%02x",(unsigned)code);
}
static int unknown_command(void *context, const struct st_graphics_command_view *view)
{
	struct cterm_big *big=owner(context); body_entry(big,view);
	return begin_footer(big,view);
}
static struct st_graphics_parser_sink callbacks(struct cterm_big *big)
{
	return (struct st_graphics_parser_sink){
		.context = &big->numeric, .text = text, .boundary = boundary,
		.command = command, .unknown = unknown, .unknown_command = unknown_command, .field = field,
		.number = st_big_numeric_number, .read_number = st_big_numeric_read_number,
	};
}
static void cancel(struct cterm_big *big)
{
	big->reset_pending=false;
	bool feeding=big->feeding; big->feeding=true;
	int status=cterm_big_collect_cancel(big);
	big->feeding=feeding; cterm_render_record_error(big->terminal,status);
	st_graphics_parser_cancel(big->parser); st_big_numeric_cancel(&big->numeric);
	st_big_fill_edit_cancel(&big->fill_edit);
	big->pair_wait = big->pair_body = big->reset_pending = false;
	big->footer_active=big->gang_selecting=big->gang_wire=big->starting_body=false;
	big->control.input.pending=0; big->control.frame.command=big->control.frame.did_loop=0;
	big->numeric.loop.active=0; big->numeric.loop.command_read=false;
	memset(&big->input,0,sizeof(big->input));
	memset(&big->setup,0,sizeof(big->setup)); big->text.active=0;
	memset(&big->text_output,0,sizeof(big->text_output));
	memset(&big->pause,0,sizeof(big->pause));
	memset(&big->query,0,sizeof(big->query));
	memset(&big->zone,0,sizeof(big->zone));
	memset(&big->bar,0,sizeof(big->bar));
	memset(&big->flood,0,sizeof(big->flood));
	memset(&big->rotation,0,sizeof(big->rotation));
	memset(&big->flow_settings,0,sizeof(big->flow_settings)); big->flow_settings_arguments=0;
}
void cterm_big_reset(struct cterm_big *big)
{
	if (big == NULL) return;
	if (big->feeding) { big->reset_pending = true; return; }
	cancel(big);
}
static int pair_expired(struct cterm_big *big, bool *expired)
{
	uint64_t sample; int status = now(big,&sample);
	if (status != 0) return status;
	if (sample < big->pair_sample) return IO_ERROR_PROTOCOL;
	big->pair_sample = sample; *expired = sample >= big->pair_deadline;
	return 0;
}
static int end_pair(struct cterm_big *big, const struct st_graphics_parser_sink *sink)
{
	big->pair_wait = false;
	if (big->numeric.io.bell != NULL) big->numeric.io.bell(big->numeric.io.context);
	return st_graphics_parser_complete_header(big->parser,sink,0,false);
}
static int start_body(struct cterm_big *big, const struct st_graphics_parser_sink *sink)
{
	big->starting_body=true;
	int status=st_graphics_parser_start_command(big->parser,sink,
	    (uint8_t)big->control.frame.command,big->footer.chained);
	big->starting_body=false;
	return status;
}
static int begin_repeat(struct cterm_big *big, const struct st_graphics_parser_sink *sink)
{
	big->numeric.loop.command_read=false;
	if (big->control.frame.gang_marker=='>') {
		big->gang_selecting=true; big->gang_wire=false;
		const struct st_graphics_command_view view={.code=(uint8_t)big->control.frame.command};
		return st_big_numeric_field(&big->numeric,&view,scalar);
	}
	return start_body(big,sink);
}
static int gang_selector(struct cterm_big *big, const struct st_graphics_parser_sink *sink,
    const uint8_t *bytes, size_t size, size_t *accepted)
{
	const struct st_graphics_command_view view={.code=(uint8_t)big->control.frame.command};
	bool complete=false; int value=0, status=0; *accepted=0;
	if (!big->gang_wire) {
		status=st_big_numeric_number(&big->numeric,&view,scalar,&value,&complete);
		if (status!=0) return status;
		if (!complete) big->gang_wire=true;
	}
	if (big->gang_wire) {
		status=st_big_numeric_read_number(&big->numeric,&view,scalar,bytes,size,accepted,&value,&complete);
		if (status!=0 || !complete) return status;
	}
	int32_t index=st_big_number_signed((uint16_t)value);
	if (index<0 || index>127) index=0;
	uint8_t code=big->control.frame.gang[index];
	/* Selector evaluation sees the previous lp_com until its return. */
	big->numeric.loop.command=code; big->control.frame.command=code;
	big->gang_selecting=big->gang_wire=false;
	return start_body(big,sink);
}
static bool body_pending(const struct cterm_big *big)
{
	return big->query.phase!=CTERM_BIG_QUERY_OFF || big->pause.phase!=CTERM_BIG_PAUSE_OFF || big->text_output.active || big->rotation.active || big->footer_active || big->collect.active || big->bar.active || big->flood.active || big->setup.phase!=CTERM_BIG_SETUP_OFF || big->zone.phase!=CTERM_BIG_ZONE_OFF;
}
static int drive(struct cterm_big *big, const void *data, size_t size, size_t *used)
{
	if (used==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*used=0;
	if (big==NULL || (data==NULL && size!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (big->feeding) return IO_ERROR_BUSY;
	big->feeding=true; cterm_big_mouse_flush(big);
	const uint8_t *bytes=data; struct st_graphics_parser_sink sink=callbacks(big);
	size_t output_budget=64, body_budget=64;
	bool advance_needed=size==0; int status=0;
	for (;;) {
		if (big->reset_pending) { status=IO_ERROR_INTERRUPTED; break; }
		if (big->query.phase!=CTERM_BIG_QUERY_OFF) {
			bool complete;
			status=cterm_big_query_poll(big,&output_budget,&complete);
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->query.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->pause.phase!=CTERM_BIG_PAUSE_OFF) {
			size_t accepted; bool complete;
			status=cterm_big_pause_poll(big,*used<size?bytes+*used:NULL,size-*used,&accepted,&body_budget,&complete);
			*used+=accepted;
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->pause.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->text_output.active) {
			size_t accepted; bool complete;
			status=st_big_stream_poll(&big->text_output.stream,&big->numeric,&big->text,&big->control,&big->control_io,
			    *used<size?bytes+*used:NULL,size-*used,&accepted,&output_budget,&complete); *used+=accepted;
			if (status!=0) break;
			if (big->reset_pending) { status=IO_ERROR_INTERRUPTED; break; }
			if (complete) {
				status=cterm_big_draw_text(big,big->text_output.x,big->text_output.y);
				if (status!=0) break;
				const struct st_graphics_command_view view={.chained=big->text_output.chained};
				big->text_output.active=false;
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->rotation.active) {
			bool complete;
			const struct st_big_state_sink palette_sink={.context=big,.palette=palette};
			status=cterm_big_rotation_poll(big,&output_budget,&body_budget,&palette_sink,&complete);
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->rotation.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->flood.active) {
			bool complete;
			status=cterm_big_flood_poll(big,&output_budget,&complete);
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->flood.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->bar.active) {
			size_t accepted; bool complete;
			status=cterm_big_bar_poll(big,*used<size?bytes+*used:NULL,size-*used,&accepted,&complete);
			*used+=accepted;
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->bar.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->collect.active) {
			bool complete;
			status=cterm_big_collect_poll(big,&body_budget,&complete);
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->collect.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			status=IO_ERROR_IN_PROGRESS; break;
		}
		if (big->zone.phase!=CTERM_BIG_ZONE_OFF) {
			size_t accepted; bool complete;
			status=cterm_big_zone_poll(big,*used<size?bytes+*used:NULL,size-*used,&accepted,
			    &output_budget,&body_budget,&complete); *used+=accepted;
			if (status!=0) break;
			if (complete) {
				const struct st_graphics_command_view view={.chained=big->zone.chained};
				status=begin_footer(big,&view);
				if (status!=IO_ERROR_IN_PROGRESS) break;
				status=0; advance_needed=true; continue;
			}
			break;
		}
		if (big->setup.phase!=CTERM_BIG_SETUP_OFF) {
			size_t accepted; bool complete;
			status=cterm_big_setup_poll(big,*used<size?bytes+*used:NULL,size-*used,&accepted,
			    &output_budget,&body_budget,&complete); *used+=accepted;
			if (status!=0) break;
			if (complete) {
				status=begin_repeat(big,&sink); if (status!=0) break;
				advance_needed=true; continue;
			}
			break;
		}
		if (big->footer_active) {
			enum st_big_footer_action action;
			status=st_big_footer_poll(&big->footer,&big->numeric.loop,&big->control,
			    &big->control_io,&output_budget,&action);
			if (status!=0) break;
			if (action==ST_BIG_FOOTER_REPEAT) {
				if (body_budget==0) { status=IO_ERROR_IN_PROGRESS; break; }
				--body_budget; big->footer_active=false;
				status=begin_repeat(big,&sink);
			} else {
				big->footer_active=false;
				status=st_graphics_parser_resume(big->parser);
			}
			if (status!=0) break;
			advance_needed=true; continue;
		}
		size_t available=*used<size?1u:0u;
		const uint8_t *input=available==0?NULL:bytes+*used;
		if (big->gang_selecting) {
			size_t accepted;
			status=gang_selector(big,&sink,input,available,&accepted); *used+=accepted;
			if (status!=0) break;
			if (big->gang_selecting) { if (*used==size) break; continue; }
			advance_needed=true; continue;
		}
		if (available==0 && !advance_needed) break;
		advance_needed=false;
		/* ff is an outer-terminal filter, never a field/pair filter. */
		if (available!=0 && !st_graphics_parser_in_command(big->parser) && input[0]==0xff) { ++*used; continue; }
		unsigned header=st_graphics_parser_header_remaining(big->parser);
		bool expired=false;
		if (big->pair_wait && (available==0 || header==1)) {
			status=pair_expired(big,&expired);
			if (status!=0) break;
			if (expired) {
				if (available!=0) ++*used;
				status=end_pair(big,&sink);
				if (status==IO_ERROR_IN_PROGRESS && body_pending(big)) { status=0; continue; }
				if (status!=0) break;
				continue;
			}
		}
		size_t accepted;
		status=st_graphics_parser_feed(big->parser,&sink,input,available,&accepted); *used+=accepted;
		if (status==IO_ERROR_IN_PROGRESS && body_pending(big)) { status=0; continue; }
		if (status!=0) break;
		if (big->pair_wait && header==2) {
			status=pair_expired(big,&expired);
			if (status==0 && expired) status=end_pair(big,&sink);
			if (status==IO_ERROR_IN_PROGRESS && body_pending(big)) { status=0; continue; }
			if (status!=0) break;
		}
	}
	if (status==0 || status==IO_ERROR_IN_PROGRESS) {
		int forwarded=cterm_big_input_forward(big);
		if (forwarded!=0) status=forwarded;
	}
	big->feeding=false;
	if (big->reset_pending) { cancel(big); if (status==IO_ERROR_INTERRUPTED) status=0; }
	return status;
}
static void reject(struct cterm_big *big, int status)
{
	big->last_error=status; ++big->rejected_commands;
	cterm_log(big->terminal,ST_CTERM_LOG_WARNING,"BIG input rejected (%d); continuing stream",status);
	cancel(big);
	/* Pointer and legacy text callbacks can also latch the same failure.
	 * It belongs to this rejected operation, not the next session poll. */
	(void)cterm_render_take_error(big->terminal);
}
int cterm_big_feed(struct cterm_big *big, const void *data, size_t size, size_t *used)
{
	if (used==NULL) return IO_ERROR_INVALID_ARGUMENT;
	*used=0;
	if (big==NULL || (data==NULL && size!=0)) return IO_ERROR_INVALID_ARGUMENT;
	if (big->feeding) return IO_ERROR_BUSY;
	int deferred=cterm_render_take_error(big->terminal);
	if (deferred!=0) reject(big,deferred);
	for (;;) {
		bool pending=body_pending(big) || big->pair_wait || big->gang_selecting || st_graphics_parser_in_command(big->parser);
		size_t accepted=0;
		int status=drive(big,data==NULL?NULL:(const uint8_t *)data+*used,size-*used,&accepted);
		*used+=accepted;
		deferred=cterm_render_take_error(big->terminal);
		if ((status==0 || status==IO_ERROR_IN_PROGRESS) && deferred!=0) status=deferred;
		if (status==0 || status==IO_ERROR_IN_PROGRESS) return status;
		reject(big,status);
		if (*used==size) return 0;
		/* A pending continuation may fail before reading the next byte;
		 * retry that byte after cancellation. A failing ordinary-byte sink
		 * with no progress must consume its rejected byte to avoid a spin. */
		if (accepted==0 && !pending) ++*used;
	}
}
int cterm_big_poll(struct cterm_big *big)
{
	if (big==NULL) return IO_ERROR_INVALID_ARGUMENT;
	size_t used; int status=cterm_big_feed(big,NULL,0,&used);
	return status==IO_ERROR_IN_PROGRESS?0:status;
}
bool cterm_big_paused(const struct cterm_big *big)
{
	return big != NULL && (big->rotation.active || big->flood.active || big->collect.active || big->zone.cooperative || big->zone.phase==CTERM_BIG_ZONE_FLOW
	    || big->query.phase!=CTERM_BIG_QUERY_OFF
	    || (big->pause.phase!=CTERM_BIG_PAUSE_OFF && big->pause.phase!=CTERM_BIG_PAUSE_NUMBER)
	    || (big->text_output.active && big->text_output.stream.phase==ST_BIG_STREAM_FLOW)
	    || big->footer_active || big->setup.cooperative || big->setup.phase==CTERM_BIG_SETUP_FLOW
	    || (big->setup.phase==CTERM_BIG_SETUP_TEXT && big->setup.stream.phase==ST_BIG_STREAM_FLOW)
	    || (big->gang_selecting && !big->gang_wire)
	    || big->numeric.field.phase == ST_BIG_NUMERIC_SELECT
	    || big->numeric.field.phase == ST_BIG_NUMERIC_WIRE_STORED
	    || big->numeric.field.phase == ST_BIG_NUMERIC_WIRE_RANDOM);
}
int cterm_big_finish(struct cterm_big *big)
{
	if (big == NULL) return 0;
	if (big->feeding) { big->reset_pending = true; return 0; }
	struct st_graphics_parser_sink sink = callbacks(big);
	int status = st_graphics_parser_finish(big->parser,&sink);
	/* Remote EOF discards incomplete commands; no timeout or default stores. */
	if (status!=0 && status!=IO_ERROR_PROTOCOL && status!=IO_ERROR_IN_PROGRESS) reject(big,status);
	else cancel(big);
	status=cterm_render_take_error(big->terminal);
	if (status!=0) reject(big,status);
	return 0;
}
void cterm_big_destroy(struct cterm_big *big)
{
	if (big == NULL) return;
	(void)cterm_big_collect_cancel(big);
	st_pixel_buffer_destroy(big->video_backing);
	st_graphics_parser_destroy(big->parser); free(big);
}
int cterm_big_create(struct cterminal *t)
{
	if ((t->screen.capabilities&ST_CONSOLE_CAP_PIXELS) == 0) return IO_ERROR_NOT_SUPPORTED;
	struct cterm_big *big = calloc(1,sizeof(*big));
	if (big == NULL) return IO_ERROR_OUT_OF_MEMORY;
	big->terminal = t;
	big->console_window[2]=79; big->console_window[3]=24;
	big->random_seconds=random_seconds;
	const struct st_big_numeric_io io = {.context = big, .bell = bell,
	    .peek_key = cterm_big_input_peek, .read_key = cterm_big_input_read};
	int status = st_big_numeric_init(&big->numeric,&io);
	if (status==0) status=st_big_flood_bind(&big->flood_work,big->graph_work.flood,CTERM_BIG_FLOOD_RECORDS,CTERM_BIG_FLOOD_RECORDS);
	if (status==0) status=st_big_producer_bind(&big->producer,big->graph_work.points.records,CTERM_BIG_GRAPH_POINTS,CTERM_BIG_GRAPH_BYTES);
	/* Defined native initial storage replaces uninitialized DOS RAM. The
	 * owned allocation/cache survive I/R, attention, EOF and cancellation. */
	if (status==0) status=st_big_image_bind(&big->image,big->image_bytes,sizeof(big->image_bytes),big->image_pens,sizeof(big->image_pens));
	st_big_state_init(&big->state); st_big_control_init(&big->control);
	if (status==0) status=st_big_fonts_init(&big->fonts,&big->state.bgi.error);
	big->producer.video_segment=0xa000;
	big->control_io=(struct st_big_control_sink){.context=big,.send=output_byte};
	const struct st_graphics_parser_config config = {
		.dialect = &dialect, .commands = commands, .command_count = sizeof(commands)/sizeof(commands[0]),
		.max_arguments = 11,
	};
	if (status == 0) status = st_graphics_parser_create(&big->parser,&config);
	if (status != 0) { cterm_big_destroy(big); return status; }
	t->big_engine = big;
	cterm_start(t);
	status = cterm_render_take_error(t);
	const struct st_big_state_sink sink = {big,install,palette,page};
	unsigned resolution = 2;
	uint32_t width, height, columns, rows, cw, ch;
	if (status == 0) status = st_console_pixel_size(t->screen.console,&width,&height);
	if (status == 0) status = st_console_size(t->screen.console,&columns,&rows);
	if (status == 0) status = st_console_cell_pixel_size(t->screen.console,&cw,&ch);
	if (status == 0) {
		for (unsigned i=1;i<=3;++i) {
			const struct st_big_mode *mode=st_big_mode(i);
			if (width==mode->width && height==mode->height && columns==mode->columns
			    && rows==mode->rows && cw==8 && ch==mode->cell_height) resolution=i;
		}
		status = st_big_state_install(&big->state,resolution,&sink);
		if (status == 0) big->numeric.context.resolution=(uint8_t)resolution;
	}
	if (status != 0) { t->big_engine = NULL; cterm_big_destroy(big); }
	return status;
}

#if ST_CONSOLE_TEST
void cterm_big_inspect(const struct cterm_big *big, struct st_console_test_application *app)
{
	if (big == NULL) return;
	st_console_test_number(app, "cterm.big_resolution", big->numeric.context.resolution);
	st_console_test_number(app, "cterm.big_scaling", big->numeric.context.scaling);
	st_console_test_number(app, "cterm.big_step_x", big->control.pointer.step_x);
	st_console_test_number(app, "cterm.big_step_y", big->control.pointer.step_y);
	st_console_test_number(app, "cterm.big_zone_active", big->zone.phase!=CTERM_BIG_ZONE_OFF);
	st_console_test_number(app, "cterm.big_zone_loopback", big->control.zones.loopback);
	static const char *const coordinates[]={"cterm.big_zone0_x1","cterm.big_zone0_y1",
	    "cterm.big_zone0_x2","cterm.big_zone0_y2"};
	for (size_t i=0;i<4;++i) st_console_test_number(app,coordinates[i],big->control.zones.rectangles[0][i]);
	uint32_t prefix=0, tail=0;
	for (unsigned i=0;i<4;++i) {
		prefix|=(uint32_t)big->control.zones.payload[i]<<(8*i);
		tail|=(uint32_t)big->control.zones.payload[4+i]<<(8*i);
	}
	st_console_test_number(app, "cterm.big_zone0_prefix", prefix);
	st_console_test_number(app, "cterm.big_zone0_tail", tail);
	st_console_test_number(app, "cterm.big_marker", big->state.application.marker);
	st_console_test_number(app, "cterm.big_marker_size", big->state.application.marker_size);
	st_console_test_number(app, "cterm.big_color", big->state.bgi.color);
	/* Keep the related curve words in one immutable inspection field; the
	 * shared capture has a bounded number of application property slots. */
	char curve[192];
	snprintf(curve,sizeof(curve),"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
	    (unsigned)big->state.bgi.aspect[0],(unsigned)big->state.bgi.aspect[1],
	    (unsigned)big->state.bgi.arc[0],(unsigned)big->state.bgi.arc[1],
	    (unsigned)big->state.bgi.arc[2],(unsigned)big->state.bgi.arc[3],
	    (unsigned)big->state.bgi.arc[4],(unsigned)big->state.bgi.arc[5],
	    (unsigned)big->state.bgi.curve_radius[0],(unsigned)big->state.bgi.curve_radius[1],
	    (unsigned)big->state.bgi.curve_span,(unsigned)big->producer.mode,
	    (unsigned)big->producer.count,(unsigned)big->producer.capacity,(unsigned)big->producer.batch);
	st_console_test_text(app,"cterm.big_curve",curve);
	static const char *const colors[]={"cterm.big_marker_color","cterm.big_line_color","cterm.big_fill_color","cterm.big_text_color"};
	for (size_t i=0;i<4;++i) st_console_test_number(app,colors[i],big->state.application.colors[i]);
	st_console_test_number(app, "cterm.big_device_color", big->state.bgi.device_color);
	st_console_test_number(app, "cterm.big_bgi_fill_style", big->state.bgi.fill_style);
	st_console_test_number(app, "cterm.big_bgi_fill_color", big->state.bgi.fill_color);
	st_console_test_number(app, "cterm.big_fill_selection", big->state.application.fill);
	st_console_test_number(app, "cterm.big_pattern_active", big->fill_edit.active);
	st_console_test_number(app, "cterm.big_bar_active", big->bar.active);
	st_console_test_number(app, "cterm.big_flood_active", big->flood.active);
	st_console_test_number(app, "cterm.big_rotation_active", big->rotation.active);
	st_console_test_number(app, "cterm.big_pause_active", big->pause.phase!=CTERM_BIG_PAUSE_OFF);
	st_console_test_number(app, "cterm.big_pause_seconds", big->pause.seconds);
	st_console_test_number(app, "cterm.big_query_active", big->query.phase!=CTERM_BIG_QUERY_OFF);
	st_console_test_number(app, "cterm.big_query_selector", big->query.selector);
	st_console_test_number(app, "cterm.big_music_memory", big->music_memory);
	st_console_test_number(app, "cterm.big_flow_mode", big->control.flow.mode);
	st_console_test_number(app, "cterm.big_random_state", big->numeric.random_state);
	st_console_test_number(app, "cterm.big_random_from", big->numeric.random_from);
	st_console_test_number(app, "cterm.big_random_to", big->numeric.random_to);
	st_console_test_number(app, "cterm.big_spray_x", big->spray.x);
	st_console_test_number(app, "cterm.big_spray_y", big->spray.y);
	st_console_test_number(app, "cterm.big_spray_width", big->spray.width);
	st_console_test_number(app, "cterm.big_spray_height", big->spray.height);
	st_console_test_number(app, "cterm.big_spray_count", big->spray.count);
	st_console_test_number(app, "cterm.big_spray_counter", big->spray.counter);
	st_console_test_number(app, "cterm.big_text_font", big->state.bgi.font);
	st_console_test_number(app, "cterm.big_text_reading", big->text_output.active);
	st_console_test_number(app, "cterm.big_text_buffer_count", big->text.count);
	st_console_test_number(app, "cterm.big_text_count", big->state.bgi.text_count);
	st_console_test_number(app, "cterm.big_font_registry_count", big->fonts.count);
	st_console_test_number(app, "cterm.big_font_loaded", big->fonts.loaded);
	st_console_test_number(app, "cterm.big_font_active", big->fonts.active!=NULL);
	uint32_t registered_fonts=0;
	for (size_t i=0;i<ST_BIG_FONT_SLOTS;++i) if (big->fonts.slots[i].body!=NULL) registered_fonts|=UINT32_C(1)<<i;
	st_console_test_number(app, "cterm.big_font_registered_mask", registered_fonts);
	st_console_test_number(app, "cterm.big_text_direction", big->state.bgi.direction);
	st_console_test_number(app, "cterm.big_text_size", big->state.bgi.text_size);
	st_console_test_number(app, "cterm.big_text_cached_width", big->state.bgi.text_cached[0]);
	st_console_test_number(app, "cterm.big_text_cached_height", big->state.bgi.text_cached[1]);
	st_console_test_number(app, "cterm.big_text_selected_size", big->state.bgi.text_selected_size);
	st_console_test_number(app, "cterm.big_text_scaled_width", big->state.bgi.text_scaled[0]);
	st_console_test_number(app, "cterm.big_text_scaled_height", big->state.bgi.text_scaled[1]);
	st_console_test_number(app, "cterm.big_text_ddo_font", big->state.bgi.text_font);
	st_console_test_number(app, "cterm.big_text_ddo_direction", big->state.bgi.text_direction);
	st_console_test_number(app, "cterm.big_text_stroke_active", big->state.bgi.text_stroke_active);
	st_console_test_number(app, "cterm.big_text_suppression", big->state.bgi.text_suppression);
	st_console_test_number(app, "cterm.big_text_stroke_id", big->state.bgi.text_stroke_id);
	st_console_test_number(app, "cterm.big_text_stroke_direction", big->state.bgi.text_stroke_direction);
	st_console_test_number(app, "cterm.big_text_total_width", big->state.bgi.text_total_width);
	st_console_test_number(app, "cterm.big_text_stroke_x", big->state.bgi.text_stroke_point[0]);
	st_console_test_number(app, "cterm.big_text_stroke_y", big->state.bgi.text_stroke_point[1]);
	st_console_test_number(app, "cterm.big_text_justify_x", big->state.bgi.text_justify_x);
	st_console_test_number(app, "cterm.big_text_justify_y", big->state.bgi.text_justify_y);
	st_console_test_number(app, "cterm.big_text_body", big->state.bgi.text_body!=NULL);
	st_console_test_number(app, "cterm.big_text_header_count", big->state.bgi.text_header[1]+256u*big->state.bgi.text_header[2]);
	st_console_test_number(app, "cterm.big_text_header_first", big->state.bgi.text_header[4]);
	st_console_test_number(app, "cterm.big_text_header_cap", big->state.bgi.text_header[8]);
	st_console_test_number(app, "cterm.big_text_header_desc", big->state.bgi.text_header[10]);
	st_console_test_number(app, "cterm.big_text_device_font", big->state.bgi.device_text_font);
	st_console_test_number(app, "cterm.big_text_device_direction", big->state.bgi.device_text_direction);
	st_console_test_number(app, "cterm.big_text_device_width", big->state.bgi.device_text_dimensions[0]);
	st_console_test_number(app, "cterm.big_text_device_height", big->state.bgi.device_text_dimensions[1]);
	st_console_test_number(app, "cterm.big_text_device_mag_x", big->state.bgi.device_text_magnification[0]);
	st_console_test_number(app, "cterm.big_text_device_mag_y", big->state.bgi.device_text_magnification[1]);
	for (size_t i=0;i<4;++i) {
		char name[64];
		snprintf(name,sizeof(name),"cterm.big_text_saved_ratio%zu",i);
		st_console_test_number(app,name,big->state.bgi.saved_ratio[i]);
		snprintf(name,sizeof(name),"cterm.big_text_effective_ratio%zu",i);
		st_console_test_number(app,name,big->state.bgi.effective_ratio[i]);
	}
	st_console_test_number(app, "cterm.big_flow_on_byte", big->control.flow.byte[0]);
	st_console_test_number(app, "cterm.big_flow_off_byte", big->control.flow.byte[1]);
	st_console_test_number(app, "cterm.big_flow_on_count", big->control.flow.count[0]);
	st_console_test_number(app, "cterm.big_flow_off_count", big->control.flow.count[1]);
	st_console_test_number(app, "cterm.big_rotation_first", big->rotation.colors.first);
	st_console_test_number(app, "cterm.big_rotation_last", big->rotation.colors.last);
	st_console_test_number(app, "cterm.big_rotation_count", big->rotation.colors.count);
	st_console_test_number(app, "cterm.big_rotation_delay", big->rotation.colors.delay);
	st_console_test_number(app, "cterm.big_rotation_counter", big->rotation.counter);
	st_console_test_number(app, "cterm.big_local_palette_size", big->state.local_palette[0]);
	st_console_test_number(app, "cterm.big_palette_padding", big->state.palette_padding);
	for (size_t i=0;i<16;++i) {
		char name[64];
		snprintf(name,sizeof(name),"cterm.big_local_palette%zu",i);
		st_console_test_number(app,name,big->state.local_palette[i+1]);
		snprintf(name,sizeof(name),"cterm.big_saved_palette%zu",i);
		st_console_test_number(app,name,big->state.saved_palette[i]);
	}
	st_console_test_number(app, "cterm.big_last_error", big->last_error);
	st_console_test_number(app, "cterm.big_rejected_commands", big->rejected_commands);
	st_console_test_number(app, "cterm.big_palette_size", big->state.bgi.palette_size);
	st_console_test_number(app, "cterm.big_bgi_background", big->state.bgi.background);
	static const char *const palette_entries[]={"cterm.big_palette0","cterm.big_palette1","cterm.big_palette2","cterm.big_palette3",
	    "cterm.big_palette4","cterm.big_palette5","cterm.big_palette6","cterm.big_palette7","cterm.big_palette8","cterm.big_palette9",
	    "cterm.big_palette10","cterm.big_palette11","cterm.big_palette12","cterm.big_palette13","cterm.big_palette14","cterm.big_palette15"};
	for (size_t i=0;i<16;++i) st_console_test_number(app,palette_entries[i],big->state.bgi.palette[i]);
	st_console_test_number(app, "cterm.big_image_operation", big->image_operation);
	st_console_test_number(app, "cterm.big_image_mode", big->image_mode);
	st_console_test_number(app, "cterm.big_image_size", big->image_size);
	st_console_test_number(app, "cterm.big_image_page_calls", big->image_page_calls);
	uint16_t image_width=0,image_height=0;
	(void)st_big_image_header(&big->image,&image_width,&image_height);
	st_console_test_number(app, "cterm.big_image_width", image_width);
	st_console_test_number(app, "cterm.big_image_height", image_height);
	uint32_t image_prefix=0,image_tail=0;
	for (unsigned i=0;i<4;++i) {
		image_prefix|=(uint32_t)big->image_bytes[4+i]<<(8*i);
		image_tail|=(uint32_t)big->image_bytes[8+i]<<(8*i);
	}
	st_console_test_number(app, "cterm.big_image_payload0", image_prefix);
	st_console_test_number(app, "cterm.big_image_payload1", image_tail);
	uint32_t custom_prefix=0, custom_tail=0;
	for (unsigned i=0;i<4;++i) {
		custom_prefix|=(uint32_t)big->state.application.patterns[0][i]<<(8*i);
		custom_tail|=(uint32_t)big->state.application.patterns[0][i+4]<<(8*i);
	}
	st_console_test_number(app, "cterm.big_custom0_prefix", custom_prefix);
	st_console_test_number(app, "cterm.big_custom0_tail", custom_tail);
	st_console_test_number(app, "cterm.big_fill_byte", big->state.bgi.fill_low);
	st_console_test_number(app, "cterm.big_device_fill_color", big->state.bgi.device_fill_color);
	st_console_test_number(app, "cterm.big_device_fill_style", big->state.bgi.device_fill_style);
	static const char *const fill_rows[]={"cterm.big_fill_row0","cterm.big_fill_row1","cterm.big_fill_row2","cterm.big_fill_row3",
	    "cterm.big_fill_row4","cterm.big_fill_row5","cterm.big_fill_row6","cterm.big_fill_row7"};
	for (size_t row=0;row<8;++row) {
		uint32_t pixels=0;
		for (unsigned x=0;x<8;++x) pixels|=(uint32_t)big->state.bgi.fill_pixels[row*8+x]<<(4*x);
		st_console_test_number(app,fill_rows[row],pixels);
	}
	st_console_test_number(app, "cterm.big_write_mode", big->state.bgi.write_mode);
	st_console_test_number(app, "cterm.big_cached_write_mode", big->state.application.write_mode);
	st_console_test_number(app, "cterm.big_point_x", big->state.bgi.x);
	st_console_test_number(app, "cterm.big_point_y", big->state.bgi.y);
	st_console_test_number(app, "cterm.big_ddo_x", big->state.bgi.ddo_x);
	st_console_test_number(app, "cterm.big_ddo_y", big->state.bgi.ddo_y);
	st_console_test_number(app, "cterm.big_device_x", big->state.bgi.device_x);
	st_console_test_number(app, "cterm.big_device_y", big->state.bgi.device_y);
	st_console_test_number(app, "cterm.big_line_style", big->state.bgi.line_style);
	st_console_test_number(app, "cterm.big_line_user", big->state.bgi.line_user);
	st_console_test_number(app, "cterm.big_line_width", big->state.bgi.line_width);
	st_console_test_number(app, "cterm.big_ddo_line_style", big->state.bgi.ddo_line_style);
	st_console_test_number(app, "cterm.big_ddo_line_user", big->state.bgi.ddo_line_user);
	st_console_test_number(app, "cterm.big_ddo_line_width", big->state.bgi.ddo_line_width);
	st_console_test_number(app, "cterm.big_device_line_width", big->state.bgi.device_line_width);
	st_console_test_number(app, "cterm.big_line_mask", big->state.bgi.line_mask);
	st_console_test_number(app, "cterm.big_bgi_error", big->state.bgi.error);
	st_console_test_number(app, "cterm.big_hollow", big->state.application.hollow);
	st_console_test_number(app, "cterm.big_bgi_width", big->state.bgi.width);
	st_console_test_number(app, "cterm.big_bgi_height", big->state.bgi.height);
	st_console_test_number(app, "cterm.big_memory_page", big->state.memory_page);
	st_console_test_number(app, "cterm.big_active_page", big->state.active_page);
	st_console_test_number(app, "cterm.big_visual_page", big->state.visual_page);
	st_console_test_number(app, "cterm.big_physical_active_page", big->video_active_page);
	st_console_test_number(app, "cterm.big_physical_visual_page", big->video_visual_page);
	st_console_test_number(app, "cterm.big_display_start", big->video_start);
	st_console_test_number(app, "cterm.big_video_backing", big->video.count);
	st_console_test_number(app, "cterm.big_loop_active", big->numeric.loop.active);
	st_console_test_number(app, "cterm.big_loop_current", big->numeric.loop.current);
	st_console_test_number(app, "cterm.big_loop_count", big->numeric.loop.count);
	st_console_test_number(app, "cterm.big_loop_index", big->numeric.loop.index);
	st_console_test_number(app, "cterm.big_no_parameters", big->numeric.loop.no_parameters);
	st_console_test_number(app, "cterm.big_setup_active", big->setup.phase!=CTERM_BIG_SETUP_OFF);
	st_console_test_number(app, "cterm.big_pair_wait", big->pair_wait);
	st_console_test_number(app, "cterm.big_footer_active", big->footer_active);
	st_console_test_number(app, "cterm.big_collect_active", big->collect.active);
	st_console_test_number(app, "cterm.big_pointer_active", big->collect.pointer);
	st_console_test_number(app, "cterm.big_pointer_x", big->collect.x);
	st_console_test_number(app, "cterm.big_pointer_y", big->collect.y);
	st_console_test_number(app, "cterm.big_pointer_armed", big->collect.armed);
	st_console_test_number(app, "cterm.big_reply_pending", big->control.input.pending);
	st_console_test_number(app, "cterm.big_reply_count", big->control.input.count);
	st_console_test_number(app, "cterm.big_loop_type", big->numeric.loop.type);
}

#endif
