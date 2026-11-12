/* BIG1.03 terminal I#/pair acquisition and I command (references8/27/54). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_big_internal.h"
#include "st_console_test.h"
#include "cterm.h"
#include "cterm_internal.h"
#include "cterm_render.h"
#include "st_bgi_data.h"
#include "st_big_draw.h"
#include "st_time.h"
#include "st_unicode_text.h"
#include "io_error.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t attention[] = {'I','#'};
static const struct st_graphics_dialect dialect = {
	.attention = attention, .attention_size = 2, .chain_indicator = true,
	.chain = '>', .number_digits = 4,
};
static const struct st_graphics_parameter point[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter line[] = {
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y},
	{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter relative[] = {
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_X},
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER,.axis=ST_GRAPHICS_Y}};
static const struct st_graphics_parameter input_parameters[] = {
	{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER},{.kind=ST_GRAPHICS_NUMBER}};
static const struct st_graphics_parameter scalar[] = {{.kind = ST_GRAPHICS_NUMBER}};
static int style_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static int extended_next(void *, const struct st_graphics_command_view *, struct st_graphics_parameter *);
static const struct st_graphics_command commands[] = {{'I',scalar,1,NULL},{'R',scalar,1,NULL},
	{'<',input_parameters,3,NULL},{'T',scalar,1,style_next},{'P',point,2,NULL},
	{'L',line,4,NULL},{'D',point,2,NULL},{'d',relative,4,NULL},
	{'g',scalar,1,NULL},{'X',scalar,1,extended_next},{'&',NULL,0,NULL}};

static bool marker_kind(const struct st_graphics_command_view *view)
{
	return st_big_number_signed((uint16_t)view->arguments[0].value)<=1;
}
static int style_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	*parameter=(struct st_graphics_parameter){.kind=view->count<(marker_kind(view)?3u:5u)?ST_GRAPHICS_NUMBER:ST_GRAPHICS_END};
	return 0;
}
static int extended_next(void *context, const struct st_graphics_command_view *view,
    struct st_graphics_parameter *parameter)
{
	(void)context;
	if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
	*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_END};
	if ((uint16_t)view->arguments[0].value==4 && view->count<7) {
		if (view->count>=2 && st_big_zone_id((uint16_t)view->arguments[1].value)>=9997) return 0;
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER};
		if (view->count>=2 && view->count<=5)
			parameter->axis=view->count%2==0?ST_GRAPHICS_X:ST_GRAPHICS_Y;
	}
	if ((uint16_t)view->arguments[0].value==6 && view->count<3)
		*parameter=(struct st_graphics_parameter){.kind=ST_GRAPHICS_NUMBER,
		    .axis=view->count==1?ST_GRAPHICS_X:ST_GRAPHICS_Y};
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
	if (view->code=='<' && view->count==1) {
		if (!view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		big->control.input.append_cr=(uint8_t)view->arguments[0].value;
	}
	if (view->code=='T' && view->count==2 && marker_kind(view)) {
		big->state.application.marker=(uint8_t)view->arguments[1].value;
		if (big->state.application.marker>7) big->state.application.marker=7;
	}
	if (view->count==0 && (view->code=='P' || view->code=='L' || view->code=='D' || view->code=='d')) {
		int status=st_big_draw_color(&big->state,big->state.application.colors[view->code=='P'?0:1]);
		if (status!=0) return status;
	}
	if (view->code=='X' && view->count>=1 && view->arguments[0].has_value
	    && (uint16_t)view->arguments[0].value==4) {
		int status=cterm_big_zone_field(big,view);
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
		if (codes[i] >= 64) return IO_ERROR_OUT_OF_RANGE;
		const uint16_t *c = st_bgi_ega_colors[codes[i]];
		rgb[i] = (uint32_t)(c[0]>>8)<<16 | (uint32_t)(c[1]>>8)<<8 | (c[2]>>8);
	}
	return st_console_set_palette(big->terminal->screen.console,
	    (uint16_t)(CTERM_BIG_PALETTE_FIRST+first),rgb,count);
}
static int page(void *context, bool visual, uint16_t value)
{
	(void)context; (void)visual;
	/* I only selects physical page0. G's independent pages come later. */
	return value == 0 ? 0 : IO_ERROR_NOT_SUPPORTED;
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
	t->cursor = CTERM_CURSOR_HIDDEN; cterm_apply_window(t);
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
	if (kind==ST_BIG_OUTPUT_REPLY) {
		if (big->terminal->keystroke_cb!=NULL)
			big->terminal->keystroke_cb((const char *)&byte,1,big->terminal->keystroke_cbdata);
	} else cterm_respond(big->terminal,(const char *)&byte,1);
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
	if (view->code=='g') {
		if (view->count!=1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		big->numeric.context.scaling=(uint16_t)view->arguments[0].value;
		return begin_footer(big,view);
	}
	if (view->code=='X') {
		if (view->count<1 || !view->arguments[0].has_value) return IO_ERROR_PROTOCOL;
		uint16_t selector=(uint16_t)view->arguments[0].value;
		if (selector==4) {
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
	big->pair_wait = big->pair_body = big->reset_pending = false;
	big->footer_active=big->gang_selecting=big->gang_wire=big->starting_body=false;
	big->control.input.pending=0; big->control.frame.command=big->control.frame.did_loop=0;
	big->numeric.loop.active=0; big->numeric.loop.command_read=false;
	memset(&big->input,0,sizeof(big->input));
	memset(&big->setup,0,sizeof(big->setup)); big->text.active=0;
	memset(&big->zone,0,sizeof(big->zone));
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
	return big->footer_active || big->collect.active || big->setup.phase!=CTERM_BIG_SETUP_OFF || big->zone.phase!=CTERM_BIG_ZONE_OFF;
}
int cterm_big_feed(struct cterm_big *big, const void *data, size_t size, size_t *used)
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
	if (status!=0 && status!=IO_ERROR_IN_PROGRESS) cancel(big);
	return status;
}
int cterm_big_poll(struct cterm_big *big)
{
	if (big==NULL) return IO_ERROR_INVALID_ARGUMENT;
	/* Mouse callbacks cannot return an error status through their bool API.
	 * Deliver their deferred rendering failure on the next poll or write. */
	int status=cterm_render_take_error(big->terminal);
	if (status!=0) return status;
	size_t used; status=cterm_big_feed(big,NULL,0,&used);
	if (status==0 || status==IO_ERROR_IN_PROGRESS) {
		int rendered=cterm_render_take_error(big->terminal);
		if (rendered!=0) return rendered;
	}
	return status==IO_ERROR_IN_PROGRESS?0:status;
}
bool cterm_big_paused(const struct cterm_big *big)
{
	return big != NULL && (big->collect.active || big->zone.cooperative || big->zone.phase==CTERM_BIG_ZONE_FLOW
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
	cancel(big);
	/* Remote EOF discards incomplete commands; no timeout or default stores. */
	return status == IO_ERROR_PROTOCOL || status == IO_ERROR_IN_PROGRESS ? 0 : status;
}
void cterm_big_destroy(struct cterm_big *big)
{
	if (big == NULL) return;
	(void)cterm_big_collect_cancel(big);
	st_graphics_parser_destroy(big->parser); free(big);
}
int cterm_big_create(struct cterminal *t)
{
	if ((t->screen.capabilities&ST_CONSOLE_CAP_PIXELS) == 0) return IO_ERROR_NOT_SUPPORTED;
	struct cterm_big *big = calloc(1,sizeof(*big));
	if (big == NULL) return IO_ERROR_OUT_OF_MEMORY;
	big->terminal = t;
	const struct st_big_numeric_io io = {.context = big, .bell = bell,
	    .peek_key = cterm_big_input_peek, .read_key = cterm_big_input_read};
	int status = st_big_numeric_init(&big->numeric,&io);
	st_big_state_init(&big->state); st_big_control_init(&big->control);
	big->control_io=(struct st_big_control_sink){.context=big,.send=output_byte};
	const struct st_graphics_parser_config config = {
		.dialect = &dialect, .commands = commands, .command_count = sizeof(commands)/sizeof(commands[0]),
		.max_arguments = 7,
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
	st_console_test_number(app, "cterm.big_point_x", big->state.bgi.x);
	st_console_test_number(app, "cterm.big_point_y", big->state.bgi.y);
	st_console_test_number(app, "cterm.big_ddo_x", big->state.bgi.ddo_x);
	st_console_test_number(app, "cterm.big_ddo_y", big->state.bgi.ddo_y);
	st_console_test_number(app, "cterm.big_device_x", big->state.bgi.device_x);
	st_console_test_number(app, "cterm.big_device_y", big->state.bgi.device_y);
	st_console_test_number(app, "cterm.big_line_style", big->state.bgi.line_style);
	st_console_test_number(app, "cterm.big_line_user", big->state.bgi.line_user);
	st_console_test_number(app, "cterm.big_line_width", big->state.bgi.line_width);
	st_console_test_number(app, "cterm.big_line_mask", big->state.bgi.line_mask);
	st_console_test_number(app, "cterm.big_bgi_error", big->state.bgi.error);
	st_console_test_number(app, "cterm.big_hollow", big->state.application.hollow);
	st_console_test_number(app, "cterm.big_bgi_width", big->state.bgi.width);
	st_console_test_number(app, "cterm.big_bgi_height", big->state.bgi.height);
	st_console_test_number(app, "cterm.big_memory_page", big->state.memory_page);
	st_console_test_number(app, "cterm.big_active_page", big->state.active_page);
	st_console_test_number(app, "cterm.big_visual_page", big->state.visual_page);
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
