/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_skew.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_polygon_internal.h"
#include "io_error.h"

#include <stdint.h>
#include <string.h>

static int
paint(const struct st_rip3_command_host *host, const struct st_rip3_environment *env,
    const struct st_rip3_port_info *port, const struct st_rip3_skew_path *path,
    bool closed, bool filled, bool *operation)
{
	struct st_rip3_style style;
	const struct st_rip3_vector *points=path->points;
	struct st_rip3_stroke_mask mask={0};
	unsigned current;
	uint8_t foreground=0,background=0,drawing=0;
	bool borders=false;
	int status;
	if (path->count<2) return 0; /* Both Polyline and Polygon require two points. */
	if (host->styles==NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_styles_current(host->styles,&current);
	if (status==0) status=st_rip3_styles_get(host->styles,current,&style);
	if (status!=0) return status;
	/* Preserve the entire contour, including repeated turns. The effective
	 * repeated XOR/NOT visit contract is awaiting #76, rather than merging
	 * those visits into a guessed one-write result. */
	if (!filled && path->end-path->start>360
	    && (style.write_mode==ST_RIP3_XOR || style.write_mode==ST_RIP3_NOT))
		return IO_ERROR_NOT_SUPPORTED;
	*operation=true;
	if (filled) {
		bool uses_foreground=false,uses_background=false;
		if (host->borders==NULL) return IO_ERROR_NOT_SUPPORTED;
		borders=*host->borders;
		if (borders && (style.write_mode!=ST_RIP3_COPY || style.pen.pattern!=0xffff))
			return IO_ERROR_NOT_SUPPORTED; /* Original Polygon pass contract: #76. */
		for (size_t i=0; i<8; i++) {
			uses_foreground |= style.fill.rows[i]!=0;
			uses_background |= style.fill.rows[i]!=255;
		}
		if (borders) status=st_rip3_command_ink(host,env->rgb_bits==8,style.drawing_color,&drawing);
		if (status==0 && style.write_mode!=ST_RIP3_NOT && uses_foreground)
			status=st_rip3_command_ink(host,env->rgb_bits==8,style.fill_color,&foreground);
		if (status==0 && ((style.write_mode!=ST_RIP3_NOT && uses_background)
		    || (borders && style.pen.draw_background && style.pen.pattern!=0xffff)))
			status=st_rip3_command_ink(host,env->rgb_bits==8,style.background_color,&background);
	}
	else if (style.write_mode!=ST_RIP3_NOT) {
		status=st_rip3_command_ink(host,env->rgb_bits==8,style.drawing_color,&foreground);
		if (status==0 && style.pen.draw_background && style.pen.pattern!=0xffff)
			status=st_rip3_command_ink(host,env->rgb_bits==8,style.background_color,&background);
	}
	if (status!=0) return status;
	if (filled) {
		struct st_rip3_fill fill;
		st_rip3_command_fill(port,&style.fill,&fill);
		status=st_rip3_polygon_fill(host->ports,points,path->count,&fill,foreground,background,
		    borders ? &style.pen : NULL,drawing,style.write_mode);
	}
	else {
		status=st_rip3_stroke_path_vectors(host->ports,points,path->count,closed,&style.pen,&mask);
		if (status==0) status=st_rip3_stroke_apply(host->ports,&mask,foreground,background,style.write_mode);
		st_rip3_stroke_free(&mask);
	}
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
	bool whole=command[0]=='&' || command[0]=='-';
	bool filled=command[0]!='&' && command[0]!=']';
	size_t fields=whole ? 5 : 7,position=0,body=length-1,total;
	int status;
	if (memchr(command+1,' ',body)!=NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_command_environment(host,&environment,&env);
	if (status!=0) return status;
	total=(command[0]=='&' ? 2*env.coordinate_size+6 : whole ? 4*env.coordinate_size+2 : 4*env.coordinate_size+6);
	if (body<=total-2 || body>total) return IO_ERROR_INVALID_FORMAT;
	for (size_t i=0; i<fields; i++) {
		size_t width=i<2 || (i<4 && command[0]!='&') ? env.coordinate_size : 2;
		status=st_rip3_command_field(values+i,command+1+position,i+1==fields ? body-position : width,env.base_math);
		if (status!=0) return status;
		position+=width;
	}
	if (host->ports==NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_command_frame(host,environment,&frame);
	if (status==0) status=st_rip3_ports_current(host->ports,&current);
	if (status==0) status=st_rip3_ports_info(host->ports,current,&port);
	if (status!=0 || !port.viewport_active) return status;
	enum st_rip3_skew_kind kind=whole ? ST_RIP3_SKEW_OVAL
	    : command[0]==']' ? ST_RIP3_SKEW_ARC : command[0]=='+' ? ST_RIP3_SKEW_CHORD : ST_RIP3_SKEW_PIE;
	status=st_rip3_skew_build(&path,
	    (int64_t)(values[0]*frame.device_width/frame.world_width),
	    (int64_t)(values[1]*frame.device_height/frame.world_height),
	    values[2]*frame.device_width/frame.world_width,values[3]*frame.device_height/frame.world_height,
	    (unsigned)values[whole ? 4 : 6],whole ? 0 : (unsigned)values[4],whole ? 360 : (unsigned)values[5],kind);
	if (status==0) status=paint(host,&env,&port,&path,command[0]!=']',filled,operation);
	st_rip3_skew_free(&path);return status;
}
