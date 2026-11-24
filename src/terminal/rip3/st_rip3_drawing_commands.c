/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_text_commands.h"
#include "st_rip3_ports.h"
#include "st_rip3_palettes.h"
#include "st_rip3_draw_internal.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int
color_field(const struct st_rip3_environment *environment,
    struct st_rip3_fields *fields, uint32_t *result)
{
	uint64_t value;
	int status;

	bool rgb = environment->rgb_bits != 0;

	status = st_rip3_command_next_field(fields, &value, rgb ? 4 : 2,
	    rgb ? 64 : environment->base_math, true);
	if (status != 0)
		return status;
	uint64_t maximum=rgb ? UINT64_C(0xffffff) : 255;
	if (value > maximum)
		return IO_ERROR_OUT_OF_RANGE;
	*result = (uint32_t)value;
	return 0;
}

static int
fill_style(struct st_rip3_style *style, const struct st_rip3_environment *environment,
    const uint8_t *arguments, size_t count, bool custom)
{
	struct st_rip3_fields fields = {arguments, count};
	uint32_t color;
	uint64_t value;
	int status;

	if (custom) {
		unsigned rows[8];

		for (size_t i = 0; i < 8; i++) {
			status = st_rip3_command_next_field(&fields, &value, 2, environment->base_math, false);
			if (status != 0)
				return status;
			if (value > 255)
				return IO_ERROR_OUT_OF_RANGE;
			rows[i] = (unsigned)value;
		}
		status = color_field(environment, &fields, &color);
		if (status != 0)
			return status;
		status = st_rip3_fill_custom(&style->fill, rows, &style->fill.origin);
	}
	else {
		/* Canonical driver 3.0.7 rows from the adopted #12 reconstruction.
		 * Pattern bits select fill/background ink; they are not inverted. */
		static const uint8_t masks[12][8] = {
		    [0] = {0}, [1] = {255,255,255,255,255,255,255,255},
		    [2] = {255,255,0,0,255,255,0,0},
		    [3] = {1,2,4,8,16,32,64,128},
		    [4] = {0xe0,0xc1,0x83,7,0x0e,0x1c,0x38,0x70},
		    [5] = {0xf0,0x78,0x3c,0x1e,0x0f,0x87,0xc3,0xe1},
		    [6] = {0xa5,0xd2,0x69,0xb4,0x5a,0x2d,0x96,0x4b},
		    [7] = {255,0x88,0x88,0x88,255,0x88,0x88,0x88},
		    [8] = {0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81},
		    [9] = {0xcc,0x33,0xcc,0x33,0xcc,0x33,0xcc,0x33},
		    [10] = {0x80,0,8,0,0x80,0,8,0},
		    [11] = {0x88,0,0x22,0,0x88,0,0x22,0},
		};

		status = st_rip3_command_next_field(&fields, &value, 2, environment->base_math, false);
		if (status != 0)
			return status;
		if (value > 11)
			return IO_ERROR_OUT_OF_RANGE;
		status = color_field(environment, &fields, &color);
		if (status != 0)
			return status;
		/* S/s replace mask and ink. Wire phase is resolved from the selected
		 * drawing context at paint time, not stored in a STYLE entry. */
		memcpy(style->fill.rows, masks[value], sizeof(style->fill.rows));
	}
	if (status == 0)
		style->fill_color = color;
	return status;
}

static int
line_style(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, struct st_rip3_style *style,
    const uint8_t *arguments, size_t count)
{
	uint64_t flags, pattern, kind, thickness;
	int status;

	/* These field widths are fixed, independent of XY coordinate size.
	 * Only the final thickness field can shorten. */
	struct st_rip3_fields fields = {arguments, count};
	status = st_rip3_command_next_field(&fields, &flags, 1, environment->base_math, false);
	if (status == 0) status = st_rip3_command_next_field(&fields, &kind, 1, environment->base_math, false);
	if (status == 0) status = st_rip3_command_next_field(&fields, &pattern, 4, environment->base_math, false);
	if (status == 0) status = st_rip3_command_next_field(&fields, &thickness, 2, environment->base_math, true);
	if (status != 0) return status;
	if (flags > 3 || kind > 4 || (kind == 4 && pattern > UINT16_MAX)) return IO_ERROR_OUT_OF_RANGE;
	if (thickness == 0) thickness = 1;
	if ((flags&2) != 0) {
		struct st_rip3_frame frame;
		status = st_rip3_environments_frame(host->environments,environment_number,&frame);
		if (status != 0) return status;
		thickness = thickness*frame.device_width/frame.world_width;
		if (thickness == 0) thickness = 1;
	}
	/* The protocol stores a byte after conversion, not a saturated width.
	 * The device kernels store effective pixel widths: wrapped byte zero is
	 * the cosmetic one-pixel pen (and has POINT radius zero, like byte one). */
	uint8_t stored = (uint8_t)thickness;
	status = st_rip3_pen_set(&style->pen,flags == 1,(unsigned)kind,
	    (uint16_t)pattern,stored == 0 ? 1 : stored);
	if (status == 0) style->line_type = (unsigned)kind;
	return status;
}

static int
font_style(struct st_rip3_style *style, const uint8_t *arguments,
    size_t count, unsigned radix)
{
	uint64_t values[4];
	int status;

	struct st_rip3_fields fields = {arguments, count};
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_next_field(&fields, values+i, 2, radix, i == 3);
		if (status != 0)
			return status;
	}
	return st_rip3_classic_text_style_set(&style->font, (unsigned)values[0],
	    (unsigned)values[1], (unsigned)values[2], (unsigned)values[3]);
}

int
st_rip3_command_ink(const struct st_rip3_command_host *host, unsigned bits, uint32_t color, uint8_t *result)
{
	if (bits==0) {
		if (color > 255)
			return IO_ERROR_OUT_OF_RANGE;
		*result = (uint8_t)color;
		return 0;
	}
	if (host->palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	return st_rip3_palettes_color_bits(host->palettes,bits,color,result);
}

void
st_rip3_command_fill(const struct st_rip3_port_info *port,
    const struct st_rip3_fill *source, struct st_rip3_fill *result)
{
	*result = *source;
	/* Kernel samples use (local-origin) mod 8. Adding the stored viewport's
	 * context origin instead gives the fixed context-(0,0) tile. Unsigned
	 * residues avoid signed overflow, including negative relative origins. */
	result->origin.x = -(int32_t)(((uint32_t)port->bounds.x0+(uint32_t)port->viewport.x0)&7);
	result->origin.y = -(int32_t)(((uint32_t)port->bounds.y0+(uint32_t)port->viewport.y0)&7);
}

static int
point(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const struct st_rip3_style *style,
    const uint8_t *arguments, size_t count, bool pixel, bool *operation)
{
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	uint64_t world_x, world_y;
	unsigned current;
	size_t width = environment->coordinate_size;
	uint8_t ink = 0;
	int status;

	struct st_rip3_fields fields = {arguments, count};
	status = st_rip3_command_next_field(&fields, &world_x, width, environment->base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &world_y, width, environment->base_math, true);
	if (status != 0) return status;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host,environment_number,&frame);
	if (status == 0) status = st_rip3_ports_current(host->ports,&current);
	if (status == 0) status = st_rip3_ports_info(host->ports,current,&port);
	if (status != 0) return status;
	*operation = true;
	if (!port.viewport_active || port.clip.x1 < port.clip.x0 || port.clip.y1 < port.clip.y0) return 0;
	/* Five wire digits can project far beyond int32_t. Retain their exact
	 * integer projection through clipping, without wrapping or saturation. */
	int64_t x = (int64_t)(world_x*frame.device_width/frame.world_width);
	int64_t y = (int64_t)(world_y*frame.device_height/frame.world_height);
	if (pixel) {
		/* X's actual SetPixel call omits the viewport offset. Convert that
		 * context-absolute position to this port API's viewport coordinates;
		 * clipping still belongs to the stored viewport in the same context. */
		x -= (int64_t)port.bounds.x0+port.viewport.x0;
		y -= (int64_t)port.bounds.y0+port.viewport.y0;
	}
	int64_t radius = pixel ? 0 : style->pen.thickness/2;
	int64_t left = x-radius, top = y-radius, right = x+radius, bottom = y+radius;
	if (right < port.clip.x0 || bottom < port.clip.y0 || left > port.clip.x1 || top > port.clip.y1) return 0;
	if (left < port.clip.x0) left = port.clip.x0;
	if (top < port.clip.y0) top = port.clip.y0;
	if (right > port.clip.x1) right = port.clip.x1;
	if (bottom > port.clip.y1) bottom = port.clip.y1;
	if (style->write_mode != ST_RIP3_NOT)
		status = st_rip3_command_ink(host,environment->rgb_bits,style->drawing_color,&ink);
	if (status != 0) return status;
	const struct st_rip3_rect area = {(int32_t)left,(int32_t)top,(int32_t)right,(int32_t)bottom};
	/* POINT is a solid odd square, not a patterned or round-capped stroke.
	 * Wrapped line byte zero is effective width one, hence radius zero. */
	return st_rip3_ports_fill(host->ports,&area,ink,style->write_mode);
}

static int
filled_rectangle(const struct st_rip3_command_host *host,
    const struct st_rip3_environment *environment, const struct st_rip3_style *style,
    const struct st_rip3_port_info *port, const struct st_rip3_vector *points, size_t vertices)
{
	struct st_rip3_pen pen = style->pen;
	struct st_rip3_stroke_mask mask = {0};
	struct st_rip3_fill fill;
	bool foreground_used = false, background_used = false;
	bool interior = vertices == 4;
	uint8_t foreground = 0, background = 0, drawing = 0;
	int status = 0;
	bool borders = style->borders;
	if (!borders && !interior) return 0;
	pen.pattern = 0xffff; pen.draw_background = false;
	if (interior) for (size_t i = 0; i < 8; i++) {
		foreground_used |= style->fill.rows[i] != 0;
		background_used |= style->fill.rows[i] != 255;
	}
	if (style->write_mode != ST_RIP3_NOT) {
		if (foreground_used) status = st_rip3_command_ink(host,environment->rgb_bits,style->fill_color,&foreground);
		if (status == 0 && background_used) status = st_rip3_command_ink(host,environment->rgb_bits,style->background_color,&background);
	}
	/* The border is COPY even when the interior is destination-NOT. */
	if (status == 0 && borders) status = st_rip3_command_ink(host,environment->rgb_bits,style->drawing_color,&drawing);
	if (status != 0) return status;
	if (borders) {
		status = st_rip3_stroke_path_vectors(host->ports,points,vertices,interior,&pen,&mask);
		if (status != 0) return status;
	}
	if (interior) {
		const struct st_rip3_rect area = {
		    points[0].x > INT32_MAX ? INT32_MAX : (int32_t)points[0].x,
		    points[0].y > INT32_MAX ? INT32_MAX : (int32_t)points[0].y,
		    points[2].x > INT32_MAX ? INT32_MAX : (int32_t)points[2].x,
		    points[2].y > INT32_MAX ? INT32_MAX : (int32_t)points[2].y};
		st_rip3_command_fill(port,&style->fill,&fill);
		status = st_rip3_draw_filled_rectangle(host->ports,&area,&fill,foreground,background,NULL,0,style->write_mode);
	}
	if (status == 0 && borders) status = st_rip3_stroke_apply(host->ports,&mask,drawing,0,ST_RIP3_COPY);
	st_rip3_stroke_free(&mask);
	return status;
}

static int
shape(const struct st_rip3_command_host *host,
    unsigned environment_number, const struct st_rip3_environment *environment,
    const struct st_rip3_style *style, const uint8_t *arguments,
    size_t count, uint8_t command, bool *operation)
{
	uint64_t values[4];
	struct st_rip3_vector points[4];
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	unsigned current;
	size_t width = environment->coordinate_size, vertices = 2;
	uint8_t foreground = 0, background = 0;
	int status;

	/* A fixed numeric command stops collecting at its complete field width.
	 * A shorter last field still has to reach the delimiter without a space. */
	struct st_rip3_fields fields = {arguments, count};
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_next_field(&fields, values+i, width,
		    environment->base_math, i == 3);
		if (status != 0)
			return status;
	}
	if (host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	/* Valid five-digit wire values project beyond int32_t at small worlds.
	 * Divide in wide integer arithmetic, then preserve the exact integral
	 * projection as doubles until raster clipping. Axis saturation would
	 * change the slope of a line whose far endpoint lies outside the canvas. */
	for (size_t i = 0; i < 2; i++) {
		points[i].x = (double)(values[i*2] * frame.device_width / frame.world_width);
		points[i].y = (double)(values[i*2+1] * frame.device_height / frame.world_height);
	}
	if (command == 'R' || command == 'K') {
		double x0 = points[0].x < points[1].x ? points[0].x : points[1].x;
		double x1 = points[0].x > points[1].x ? points[0].x : points[1].x;
		double y0 = points[0].y < points[1].y ? points[0].y : points[1].y;
		double y1 = points[0].y > points[1].y ? points[0].y : points[1].y;

		points[0] = (struct st_rip3_vector){x0,y0};
		points[1] = (struct st_rip3_vector){x1,y1};
		if (x0 != x1 && y0 != y1) {
			points[1] = (struct st_rip3_vector){x1,y0};
			points[2] = (struct st_rip3_vector){x1,y1};
			points[3] = (struct st_rip3_vector){x0,y1};
			vertices = 4;
		}
	}
	status = st_rip3_ports_current(host->ports, &current);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, current, &port);
	if (status != 0 || !port.viewport_active)
		return status;
	*operation = true;
	if (command == 'K')
		return filled_rectangle(host,environment,style,&port,points,vertices);
	if (command == 'B') {
		struct st_rip3_fill fill;
		bool uses_foreground = false, uses_background = false;
		st_rip3_command_fill(&port,&style->fill,&fill);
		for (size_t i = 0; i < 8; i++) {
			uses_foreground |= style->fill.rows[i] != 0;
			uses_background |= style->fill.rows[i] != 255;
		}
		if (style->write_mode != ST_RIP3_NOT) {
			if (uses_foreground) status = st_rip3_command_ink(host,environment->rgb_bits,style->fill_color,&foreground);
			if (status == 0 && uses_background) status = st_rip3_command_ink(host,environment->rgb_bits,style->background_color,&background);
			if (status != 0) return status;
		}
		/* Axis-aligned clipping can safely saturate far fill bounds. */
		const struct st_rip3_rect area = {
		    points[0].x > INT32_MAX ? INT32_MAX : (int32_t)points[0].x,
		    points[0].y > INT32_MAX ? INT32_MAX : (int32_t)points[0].y,
		    points[1].x > INT32_MAX ? INT32_MAX : (int32_t)points[1].x,
		    points[1].y > INT32_MAX ? INT32_MAX : (int32_t)points[1].y};
		return st_rip3_draw_bar(host->ports,&area,&fill,foreground,background,style->write_mode);
	}
	/* Destination NOT ignores both colour operands. Background lookup is
	 * only needed for a pen whose pattern-off pixels are painted. */
	if (style->write_mode != ST_RIP3_NOT) {
		status = st_rip3_command_ink(host, environment->rgb_bits, style->drawing_color, &foreground);
		if (status == 0 && style->pen.draw_background)
			status = st_rip3_command_ink(host, environment->rgb_bits, style->background_color, &background);
		if (status != 0)
			return status;
	}
	return st_rip3_stroke_visits(host->ports,points,vertices,vertices==4,
	    &style->pen,foreground,background,style->write_mode);
}

int
st_rip3_drawing_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	unsigned environment_number, number;
	uint64_t value;
	const uint8_t *arguments = command+1;
	size_t count = length-1;
	bool shape_command = command[0] == 'L' || command[0] == 'R' || command[0] == 'B' || command[0] == 'K';
	bool pixel_or_point = command[0] == 'X' || command[0] == 'j';
	int status;

	if (host->styles == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &environment_number, &environment);
	if (status == 0)
		status = st_rip3_styles_current(host->styles, &number);
	if (status == 0)
		status = st_rip3_styles_get(host->styles, number, &style);
	if (status != 0)
		return status;
	if (command[0] == 'E') {
		struct st_rip3_frame frame;
		struct st_rip3_port_info port;
		uint8_t background;

		if (host->ports == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		status = st_rip3_command_frame(host, environment_number, &frame);
		if (status == 0)
			status = st_rip3_ports_current(host->ports, &number);
		if (status == 0)
			status = st_rip3_ports_info(host->ports, number, &port);
		if (status != 0 || !port.viewport_active)
			return status;
		*operation = true;
		status = st_rip3_command_ink(host, environment.rgb_bits, style.background_color, &background);
		if (status == 0)
			status = st_rip3_ports_erase(host->ports, background);
		return status;
	}
	if (shape_command)
		return shape(host, environment_number, &environment, &style, arguments,
		    count, command[0], operation);
	if (pixel_or_point)
		return point(host,environment_number,&environment,&style,arguments,count,command[0] == 'X',operation);
	if (command[0] == '=') {
		status = line_style(host,environment_number,&environment,&style,arguments,count);
		if (status != 0) return status;
		*operation = true;
		return st_rip3_styles_set(host->styles,number,&style);
	}
	if (command[0] == 'S' || command[0] == 's') {
		status = fill_style(&style, &environment, arguments, count, command[0] == 's');
		if (status != 0)
			return status;
		*operation = true;
		return st_rip3_styles_set(host->styles, number, &style);
	}
	if (command[0] == 'Y') {
		status = font_style(&style, arguments, count, environment.base_math);
		if (status != 0)
			return status;
		style.font_mode = ST_RIP3_FONT_CLASSIC;
		*operation = true;
		return st_rip3_styles_set(host->styles,number,&style);
	}
	struct st_rip3_fields fields = {arguments, count};
	if (command[0] == 'N' || command[0] == 'W') {
		status = st_rip3_command_next_field(&fields, &value, 2, environment.base_math, true);
		if (status != 0)
			return status;
		if (command[0] == 'N')
			style.borders = value != 0;
		else {
			if (value > 4)
				return IO_ERROR_OUT_OF_RANGE;
			style.write_mode = (enum st_rip3_write_mode)value;
		}
	}
	else {
		uint32_t color;

		status = color_field(&environment, &fields, &color);
		if (status != 0)
			return status;
		if (command[0] == 'c') style.drawing_color = color;
		else style.background_color = color;
	}
	*operation = true;
	return st_rip3_styles_set(host->styles, number, &style);
}
