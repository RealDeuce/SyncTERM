/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_classic_text.h"
#include "st_rip3_outline_cache.h"
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

static int
text_bytes(const uint8_t *text, size_t length, enum st_rip3_text_encoding encoding,
    const uint8_t **glyphs, size_t *count, uint8_t **owned)
{
	/* Expansion/control notation is not silently printed as literal text.
	 * The enclosing expansion/transport layer remains separate (#6/#7). */
	for (size_t i = 0; i < length; i++) {
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
st_rip3_text_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
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
	const uint8_t *arguments = command+1, *glyphs;
	size_t count = length-1, glyph_count;
	uint8_t *owned = NULL, foreground = 0, background = 0;
	int status = st_rip3_command_environment(host,&environment_number,&environment);
	if (status != 0) return status;
	bool positioned = command[0] != 'T';
	if (positioned) {
		size_t width = environment.coordinate_size;
		uint64_t x, y;
		if (command[0] == 'm' ? (count <= width || count > 2*width) : count < 2*width)
			return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&x,arguments,width,environment.base_math);
		if (status == 0) status = st_rip3_command_field(&y,arguments+width,
		    command[0] == 'm' ? count-width : width,environment.base_math);
		if (status != 0) return status;
		if (x > INT32_MAX || y > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
		if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
		status = st_rip3_command_frame(host,environment_number,&frame);
		if (status != 0) return status;
		anchor = (struct st_rip3_text_position){(double)(x*frame.device_width/frame.world_width),
		    (double)(y*frame.device_height/frame.world_height)};
		raw_x = x; raw_y = y;
		if (command[0] == '@') {
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
	if (command[0] == 'm') {
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
	if (style.font_mode != ST_RIP3_FONT_CLASSIC) return IO_ERROR_NOT_SUPPORTED;
	status = text_bytes(arguments,count,host->text->encoding,&glyphs,&glyph_count,&owned);
	if (status != 0) return status;
	*operation = true;
	if (!positioned) anchor = (struct st_rip3_text_position){
	    (double)(port.cursor.x-origin_x),(double)(port.cursor.y-origin_y)};
	const struct st_rip3_classic_font *font = host->text->font;
	if (font == NULL || font->number != style.font.font) {
		enum st_rip3_canvas canvas = frame.device_width == 640 ? ST_RIP3_CANVAS_640_480
		    : frame.device_width == 800 ? ST_RIP3_CANVAS_800_600 : ST_RIP3_CANVAS_1024_768;
		status = st_rip3_classic_font_builtin(&builtin,canvas,style.font.font);
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
		status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.drawing_color,&foreground);
		if (status == 0 && (style.font.flags&32) != 0)
			status = st_rip3_command_ink(host,environment.rgb_bits == 8,style.background_color,&background);
	}
	if (status == 0) status = st_rip3_draw_classic_text(host->ports,font,&style.font,glyphs,glyph_count,
	    &anchor,foreground,background,style.write_mode);
	if (status == 0) status = st_rip3_ports_cursor(host->ports,port_number,&next);
done:
	free(owned); return status;
}
