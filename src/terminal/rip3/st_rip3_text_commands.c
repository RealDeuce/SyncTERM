/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_outline_cache.h"
#include "st_rip3_outline_font.h"
#include "st_rip3_outline_layout.h"
#include "st_rip3_outline_text.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_utf8.h"
#include "st_codepage.h"
#include "io_error.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
st_rip3_command_outline_font(struct st_rip3_outline_font *result, const struct st_rip3_text_context *context,
    const struct st_rip3_outline_style *style, const struct st_rip3_frame *frame, unsigned orientation)
{
	if (context->outline_font!=NULL) {
		struct st_rip3_rff checked;
		struct st_rip3_rff_style found;
		int status=st_rip3_rff_open(&checked,context->outline_font->data,context->outline_font->size);
		if (status==0) status=st_rip3_rff_find_style(&found,&checked,style->name);
		if (status!=0 && status!=IO_ERROR_NOT_FOUND) return status;
		if (status==0) for (size_t face=0; face<checked.styles; face++) {
			struct st_rip3_rff_style candidate;
			status=st_rip3_rff_style_at(&candidate,&checked,face);
			if (status!=0) return status;
			if (candidate.name==found.name)
				return st_rip3_outline_font_bind(result,&checked,face,frame,style->size,style->flags,orientation);
		}
	}
	return st_rip3_outline_font_builtin(result,style->name,frame,style->size,style->flags,orientation);
}

static int
outline_text(const struct st_rip3_command_host *host, const struct st_rip3_style *style,
    const struct st_rip3_frame *frame, unsigned rgb_bits, unsigned port_number,
    const struct st_rip3_port_info *port, const struct st_rip3_text_position *anchor,
    const uint8_t *glyphs, size_t length, bool positioned)
{
	struct st_rip3_outline_layout layout;
	struct st_rip3_outline_font font;
	int status=st_rip3_outline_layout_prepare(&layout,&style->outline,frame);
	if (status==0) status=st_rip3_command_outline_font(&font,host->text,&style->outline,frame,layout.font_orientation);
	if (status==0 && host->text->outline_cache==NULL)
		status=st_rip3_outline_cache_create(&host->text->outline_cache);
	if (status==0) status=st_rip3_outline_cache_prepare(host->text->outline_cache,&font,layout.spacing);
	/* Every accepted outline call stores its cursor even on failed preparation
	 * or later ink production. A failed preparation reads the previous snapshot. */
	struct st_rip3_outline_width width;
	int measured=st_rip3_outline_cache_measure(&width,host->text->outline_cache,glyphs,length);
	if (measured!=0) return measured;
	struct st_rip3_cursor next=port->cursor;
	if (positioned) next=(struct st_rip3_cursor){
	    (int64_t)port->bounds.x0+port->viewport.x0+(int64_t)anchor->x,
	    (int64_t)port->bounds.y0+port->viewport.y0+(int64_t)anchor->y};
	next.x+=width.cursor;
	measured=st_rip3_ports_cursor(host->ports,port_number,&next);
	if (measured!=0) return measured;
	if (status!=0) return status;
	return st_rip3_command_draw_outline(host,style,&layout,&font,rgb_bits,anchor,glyphs,length);
}

int
st_rip3_command_draw_outline(const struct st_rip3_command_host *host, const struct st_rip3_style *style,
    const struct st_rip3_outline_layout *layout, const struct st_rip3_outline_font *font, unsigned rgb_bits,
    const struct st_rip3_text_position *anchor, const uint8_t *glyphs, size_t length)
{
	if (length>SIZE_MAX/sizeof(struct st_rip3_text_position) || length>SIZE_MAX/sizeof(int32_t))
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_text_position *positions=malloc(length*sizeof(*positions));
	int32_t *advances=malloc(length*sizeof(*advances));
	if (positions==NULL || advances==NULL) { free(positions);free(advances);return IO_ERROR_OUT_OF_MEMORY; }
	int32_t bbox=0;int64_t total;
	int status=st_rip3_outline_font_advances(advances,font,style->outline.string_rotation,glyphs,length);
	if (status==0 && layout->bounding_box_spacing) status=st_rip3_outline_font_bbox_width(&bbox,font);
	if (status==0) status=st_rip3_outline_layout_positions(layout,anchor,advances,length,bbox,positions,&total);
	uint8_t foreground=0,background=0;
	if (status==0 && style->write_mode!=ST_RIP3_NOT) {
		status=st_rip3_command_ink(host,rgb_bits,style->drawing_color,&foreground);
		if (status==0 && style->outline.shadow!=0) status=st_rip3_command_ink(host,rgb_bits,style->background_color,&background);
	}
	if (status==0) status=st_rip3_draw_outline_positioned(host->ports,font,layout,glyphs,length,positions,
	    foreground,background,style->write_mode);
	free(positions);free(advances);return status;
}

static int
text_glyphs(const struct st_rip3_text_context *context, const uint8_t *text, size_t length,
    const uint8_t **glyphs, size_t *count, uint8_t **owned, bool expanded)
{
	enum st_rip3_text_encoding encoding=context->encoding;
	/* Expansion/control notation is not silently printed as literal text.
	 * The enclosing expansion/transport layer remains separate (#6/#7). */
	for (size_t i = 0; !expanded && i < length; i++) {
		uint8_t c = text[i];
		if (c == '$' || c == '[' || c == '\\' || c == '^' || c == '`'
		    || (i+1 < length && ((c == '<' && text[i+1] == '<') || (c == '(' && text[i+1] == '('))))
			return IO_ERROR_NOT_SUPPORTED;
	}
	*glyphs = text; *count = length; *owned = NULL;
	if (encoding == ST_RIP3_TEXT_CP437) return 0;
	if (encoding != ST_RIP3_TEXT_UTF8) return IO_ERROR_NOT_SUPPORTED;
	if (length == 0) return 0;
	uint8_t *bytes = malloc(length);
	if (bytes == NULL) return IO_ERROR_OUT_OF_MEMORY;
	size_t offset = 0, output = 0;
	while (offset < length) {
		uint32_t scalar;
		size_t used;
		int status = st_utf8_decode_one(&scalar,&used,text+offset,length-offset);
		if (status != 0) { free(bytes); return status; }
		uint8_t code = '?';
		(void)st_codepage_from_unicode(&code,ST_CODEPAGE_CP437,scalar);
		bytes[output++] = code; offset += used;
	}
	*glyphs = bytes; *count = output; *owned = bytes;
	return 0;
}

int
st_rip3_command_text_glyphs(const struct st_rip3_text_context *context, const uint8_t *text, size_t length,
    const uint8_t **glyphs, size_t *count, uint8_t **owned)
{
	return text_glyphs(context,text,length,glyphs,count,owned,false);
}

int
st_rip3_command_expanded_glyphs(const struct st_rip3_text_context *context,const uint8_t *text,
    size_t length,const uint8_t **glyphs,size_t *count,uint8_t **owned)
{
	return text_glyphs(context,text,length,glyphs,count,owned,true);
}

int
st_rip3_text_argument_prepare(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, struct st_rip3_text_argument *argument, size_t *offset)
{
	if (length==0 || (command[0]!='T' && command[0]!='@')) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_text_argument value={.positioned=command[0]=='@'};
	size_t at=1;
	if (value.positioned) {
		struct st_rip3_environment environment;unsigned number;
		int status=st_rip3_command_environment(host,&number,&environment);
		if (status!=0) return status;
		size_t width=environment.coordinate_size;at+=2*width;
		if (length<=at) return IO_ERROR_INVALID_FORMAT;
		status=st_rip3_command_field(&value.x,command+1,width,environment.base_math);
		if (status==0) status=st_rip3_command_field(&value.y,command+1+width,width,environment.base_math);
		if (status!=0) return status;
		if (value.x>INT32_MAX || value.y>INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	*argument=value;*offset=at;return 0;
}

static int
text_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation, const struct st_rip3_text_argument *prepared)
{
	struct st_rip3_environment environment;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_text_position anchor;
	struct st_rip3_cursor next;
	uint64_t raw_x = 0, raw_y = 0;
	struct st_rip3_classic_font builtin;
	struct st_rip3_classic_text_plan plan;
	struct st_rip3_style style;
	unsigned environment_number, number, port_number;
	char kind=prepared==NULL ? (char)command[0] : prepared->positioned ? '@' : 'T';
	const uint8_t *arguments = prepared==NULL ? command+1 : command, *glyphs;
	size_t count = prepared==NULL ? length-1 : length, glyph_count;
	uint8_t *owned = NULL, foreground = 0, background = 0;
	int status = st_rip3_command_environment(host,&environment_number,&environment);
	if (status != 0) return status;
	bool positioned = kind != 'T';
	if (positioned) {
		uint64_t x,y;
		size_t width=environment.coordinate_size;
		if (prepared!=NULL) {x=prepared->x;y=prepared->y;}
		else {
			if (kind=='m' ? (count<=width || count>2*width) : count<2*width) return IO_ERROR_INVALID_FORMAT;
			status=st_rip3_command_field(&x,arguments,width,environment.base_math);
			if (status==0) status=st_rip3_command_field(&y,arguments+width,kind=='m' ? count-width : width,environment.base_math);
			if (status!=0) return status;
			if (x>INT32_MAX || y>INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
		}
		if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
		status = st_rip3_command_frame(host,environment_number,&frame);
		if (status != 0) return status;
		anchor = (struct st_rip3_text_position){(double)(x*frame.device_width/frame.world_width),
		    (double)(y*frame.device_height/frame.world_height)};
		raw_x = x; raw_y = y;
		if (kind=='@' && prepared==NULL) {
			arguments += 2*width; count -= 2*width;
			if (count == 0) return IO_ERROR_INVALID_FORMAT;
		}
	}
	else {
		if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
		status = st_rip3_command_frame(host,environment_number,&frame);
		if (status != 0) return status;
	}
	status = st_rip3_ports_current(host->ports,&number);
	if (status == 0) status = st_rip3_ports_info(host->ports,number,&port);
	if (status != 0 || !port.viewport_active) return status;
	port_number = number;
	int64_t origin_x = (int64_t)port.bounds.x0+port.viewport.x0;
	int64_t origin_y = (int64_t)port.bounds.y0+port.viewport.y0;
	if (kind == 'm') {
		next = (struct st_rip3_cursor){(int64_t)anchor.x+origin_x,(int64_t)anchor.y+origin_y};
		*operation = true;
		return st_rip3_ports_cursor(host->ports,port_number,&next);
	}
	if (count == 0) return 0; /* Empty TEXT never consumes font/ink state. */
	if (host->text == NULL) return IO_ERROR_NOT_SUPPORTED;
	if (host->styles == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_styles_current(host->styles,&number);
	if (status == 0) status = st_rip3_styles_get(host->styles,number,&style);
	if (status != 0) return status;
	status = text_glyphs(host->text,arguments,count,&glyphs,&glyph_count,&owned,prepared!=NULL);
	if (status != 0) return status;
	*operation = true;
	if (!positioned) anchor = (struct st_rip3_text_position){
	    (double)(port.cursor.x-origin_x),(double)(port.cursor.y-origin_y)};
	if (style.font_mode==ST_RIP3_FONT_OUTLINE) {
		status=outline_text(host,&style,&frame,environment.rgb_bits,port_number,&port,&anchor,glyphs,glyph_count,positioned);
		goto done;
	}
	const struct st_rip3_classic_font *font = host->text->font;
	if (font == NULL || font->number != style.font.font) {
		status = st_rip3_classic_font_builtin_device(&builtin,frame.device_width,frame.device_height,style.font.font);
		if (status != 0) goto done;
		font = &builtin;
	}
	status = st_rip3_classic_text_plan(font,&style.font,glyphs,glyph_count,&anchor,&plan);
	if (status != 0) goto done;
	if (!isfinite(plan.advance)) { status = IO_ERROR_OUT_OF_RANGE; goto done; }
	/* The metric scales the complete string, truncates, then returns its
	 * signed low WORD. Raster advances can still be fractional. */
	int64_t advance = (int64_t)(plan.integer_advance&UINT64_C(65535));
	if (advance >= 32768) advance -= 65536;
	if (positioned && style.font.font==0) {
		struct st_rip3_outline_width cached;
		status=st_rip3_outline_cache_measure(&cached,host->text->outline_cache,glyphs,glyph_count);
		if (status!=0) goto done;
		advance=cached.cursor;
	}
	next = port.cursor;
	if (!positioned) next.x += advance;
	else {
		/* The classic TEXT_XY cursor store uses raw world X/Y, unlike MOVE
		 * and its separate converted drawing anchor. Font zero uses the last
		 * logical outline snapshot without realizing/preparing another font. */
		next = (struct st_rip3_cursor){origin_x+(int64_t)raw_x+advance,origin_y+(int64_t)raw_y};
	}
	const int64_t limit = INT64_C(1024)*INT32_MAX;
	if (next.x < -limit || next.x > limit || next.y < -limit || next.y > limit) {
		status = IO_ERROR_OUT_OF_RANGE; goto done;
	}
	if (style.write_mode != ST_RIP3_NOT) {
		status = st_rip3_command_ink(host,environment.rgb_bits,style.drawing_color,&foreground);
		if (status == 0 && (style.font.flags&32) != 0)
			status = st_rip3_command_ink(host,environment.rgb_bits,style.background_color,&background);
	}
	if (status == 0) status = st_rip3_draw_classic_text(host->ports,font,&style.font,glyphs,glyph_count,
	    &anchor,foreground,background,style.write_mode);
	if (status == 0) status = st_rip3_ports_cursor(host->ports,port_number,&next);
done:
	free(owned); return status;
}

int
st_rip3_text_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	return text_execute(host,command,length,operation,NULL);
}

int
st_rip3_text_commands_expanded(const struct st_rip3_command_host *host,
    const struct st_rip3_text_argument *argument, const uint8_t *text, size_t length, bool *operation)
{
	return text_execute(host,text,length,operation,argument);
}
