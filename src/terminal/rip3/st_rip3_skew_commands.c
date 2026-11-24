/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_skew.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_polygon_internal.h"
#include "io_error.h"

#include <stdint.h>

enum border_policy {
	BORDER_FROM_STYLE,
	BORDER_FROM_MARKER,
	BORDER_OMITTED,
};

static int
paint(const struct st_rip3_command_host *host, const struct st_rip3_environment *env,
    const struct st_rip3_port_info *port, const struct st_rip3_skew_path *path,
    bool closed, bool filled, enum border_policy border, bool *operation)
{
	struct st_rip3_style style;
	const struct st_rip3_vector *points=path->points;
	unsigned current;
	uint8_t foreground=0,background=0,drawing=0;
	bool borders=false;
	bool marker_pen = border == BORDER_FROM_MARKER;
	int status;
	if (path->count<2) return 0; /* Both Polyline and Polygon require two points. */
	if (host->styles==NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_styles_current(host->styles,&current);
	if (status==0) status=st_rip3_styles_get(host->styles,current,&style);
	if (status!=0) return status;
	*operation=true;
	if (filled) {
		bool uses_foreground=false,uses_background=false;
		borders = marker_pen || (border == BORDER_FROM_STYLE && style.borders);
		bool resolve_stroke = borders && (!marker_pen || style.write_mode != ST_RIP3_NOT);
		for (size_t i=0; i<8; i++) {
			uses_foreground |= style.fill.rows[i]!=0;
			uses_background |= style.fill.rows[i]!=255;
		}
		if (resolve_stroke) status=st_rip3_command_ink(host,env->rgb_bits,style.drawing_color,&drawing);
		if (status==0 && style.write_mode!=ST_RIP3_NOT && uses_foreground)
			status=st_rip3_command_ink(host,env->rgb_bits,style.fill_color,&foreground);
		if (status==0 && ((style.write_mode!=ST_RIP3_NOT && uses_background)
		    || (resolve_stroke && style.pen.draw_background && style.pen.pattern!=0xffff)))
			status=st_rip3_command_ink(host,env->rgb_bits,style.background_color,&background);
	}
	else if (style.write_mode!=ST_RIP3_NOT) {
		status=st_rip3_command_ink(host,env->rgb_bits,style.drawing_color,&foreground);
		if (status==0 && style.pen.draw_background && style.pen.pattern!=0xffff)
			status=st_rip3_command_ink(host,env->rgb_bits,style.background_color,&background);
	}
	if (status!=0) return status;
	if (filled) {
		struct st_rip3_fill fill;
		st_rip3_command_fill(port,&style.fill,&fill);
		if (marker_pen)
			return st_rip3_polygon_marker_fill(host->ports,points,path->count,&fill,foreground,background,
			    &style.pen,drawing,style.write_mode);
		status=st_rip3_polygon_fill(host->ports,points,path->count,&fill,foreground,background,
		    borders ? &style.pen : NULL,drawing,style.write_mode);
	}
	else {
		status=st_rip3_stroke_visits(host->ports,points,path->count,closed,&style.pen,
		    foreground,background,style.write_mode);
	}
	return status;
}

int
st_rip3_affine_command_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	unsigned current;
	int status = st_rip3_command_environment(host, &current, &environment);
	if (status != 0)
		return status;
	bool whole = command[0] == '.' || command[0] == '{';
	bool fill_digit = command[0] == ':' || command[0] == '`';
	size_t pairs = whole ? 3 : 5;
	size_t xy = environment.coordinate_size;
	struct st_rip3_fields fields = {command + 1, length - 1};
	struct st_rip3_frame frame;
	status = st_rip3_command_frame(host, current, &frame);
	if (status != 0)
		return status;
	struct st_rip3_vector control[5];
	for (size_t i = 0; i < pairs; ++i) {
		uint64_t x, y;
		status = st_rip3_command_next_field(&fields, &x, xy, environment.base_math, false);
		if (status == 0)
			status = st_rip3_command_next_field(&fields, &y, xy, environment.base_math,
			    !fill_digit && i + 1 == pairs);
		if (status != 0)
			return status;
		control[i] = (struct st_rip3_vector){
		    (double)(x * frame.device_width / frame.world_width),
		    (double)(y * frame.device_height / frame.world_height),
		};
	}
	uint64_t flags = 0;
	if (fill_digit)
		status = st_rip3_command_next_field(&fields, &flags, 1, environment.base_math, true);
	if (status == 0)
		status = st_rip3_ports_current(host->ports, &current);
	struct st_rip3_port_info port;
	if (status == 0)
		status = st_rip3_ports_info(host->ports, current, &port);
	if (status != 0 || !port.viewport_active)
		return status;

	enum st_rip3_skew_kind kind = whole ? ST_RIP3_SKEW_OVAL
	    : command[0] == ',' ? ST_RIP3_SKEW_ARC
	    : command[0] == ':' ? ST_RIP3_SKEW_PIE : ST_RIP3_SKEW_CHORD;
	struct st_rip3_skew_path path = {0};
	bool closed;
	status = st_rip3_affine_build(&path, &closed, control, kind);
	if (status == 0) {
		/* The contour already addresses the screen or bitmap directly.
		 * Convert to viewport-relative coordinates for the painter. */
		double viewport_x = (double)port.bounds.x0 + port.viewport.x0;
		double viewport_y = (double)port.bounds.y0 + port.viewport.y0;
		for (size_t i = 0; i < path.count; ++i) {
			path.points[i].x -= viewport_x;
			path.points[i].y -= viewport_y;
		}
		status = paint(host, &environment, &port, &path, closed,
		    closed && (command[0] == '{' || flags != 0), BORDER_FROM_STYLE, operation);
	}
	st_rip3_skew_free(&path);
	return status;
}

int
st_rip3_skew_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment env;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_skew_path path={0};
	uint64_t values[7];
	unsigned environment,current;
	bool marker=command[0]==';';
	bool whole=command[0]=='&' || command[0]=='-';
	bool filled=command[0]!='&' && command[0]!=']';
	size_t count = whole ? 5 : 7;
	struct st_rip3_fields fields = {command + 1, length - 1};
	int status;
	status=st_rip3_command_environment(host,&environment,&env);
	if (status!=0) return status;
	for (size_t i = 0; i < count; i++) {
		bool coordinate = i < 2 || (marker ? i == 3 || i == 4 : i < 4 && command[0] != '&');
		size_t width = coordinate ? env.coordinate_size : 2;
		status = st_rip3_command_next_field(&fields, values+i, width, env.base_math, i+1 == count);
		if (status!=0) return status;
	}
	if (marker && (values[2] >= 36 || values[5] >= 360 || values[6] > 3))
		return IO_ERROR_OUT_OF_RANGE;
	if (host->ports==NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_command_frame(host,environment,&frame);
	if (status==0) status=st_rip3_ports_current(host->ports,&current);
	if (status==0) status=st_rip3_ports_info(host->ports,current,&port);
	if (status!=0 || !port.viewport_active) return status;
	int64_t x = (int64_t)(values[0] * frame.device_width / frame.world_width);
	int64_t y = (int64_t)(values[1] * frame.device_height / frame.world_height);
	enum border_policy border = BORDER_FROM_STYLE;
	if (marker) {
		status = st_rip3_marker_build(&path, (unsigned)values[2], x, y,
		    values[3] * frame.device_width / frame.world_width,
		    values[4] * frame.device_height / frame.world_height, (unsigned)values[5]);
		filled = (values[6] & 1) != 0;
		border = values[6] == 1 ? BORDER_FROM_MARKER : BORDER_OMITTED;
	}
	else {
		enum st_rip3_skew_kind kind=whole ? ST_RIP3_SKEW_OVAL
		    : command[0]==']' ? ST_RIP3_SKEW_ARC : command[0]=='+' ? ST_RIP3_SKEW_CHORD : ST_RIP3_SKEW_PIE;
		status=st_rip3_skew_build(&path,x,y,
		    values[2]*frame.device_width/frame.world_width,values[3]*frame.device_height/frame.world_height,
		    (unsigned)values[whole ? 4 : 6],whole ? 0 : (unsigned)values[4],whole ? 360 : (unsigned)values[5],kind);
	}
	if (status==0) status=paint(host,&env,&port,&path,command[0]!=']',filled,border,operation);
	st_rip3_skew_free(&path);return status;
}
