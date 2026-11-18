/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_bezier.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_ports.h"
#include "st_rip3_draw_internal.h"
#include "st_rip3_polygon_internal.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int32_t
project(uint64_t value, uint32_t device, uint32_t world)
{
	/* Retain the component's wide WORLD division before an explicit device
	 * word conversion. Original generator wrap/narrowing is a later operation;
	 * no signed C overflow or out-of-range integer cast is used here. */
	uint32_t bits=(uint32_t)(value*device/world);
	return bits<=INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits-INT64_C(4294967296));
}

static int
poly_records(const uint8_t *bytes, size_t length, const struct st_rip3_environment *env,
    const struct st_rip3_frame *frame, bool closed, struct st_rip3_bezier_record *records,
    size_t *count, unsigned *requested)
{
	uint64_t value;
	size_t position=4,curves=0,lines=0;
	unsigned last=0;
	int status;
	if (length<4) return IO_ERROR_INVALID_FORMAT;
	status=st_rip3_command_field(&value,bytes,2,env->base_math);
	if (status!=0) return status;
	*count=(size_t)value;
	if (*count==0) return IO_ERROR_INVALID_FORMAT;
	status=st_rip3_command_field(&value,bytes+2,2,env->base_math);
	if (status!=0) return status;
	*requested=(unsigned)value;
	for (size_t i=0; i<*count; i++) {
		if (position>=length || bytes[position]<'0' || bytes[position]>'6') return IO_ERROR_INVALID_FORMAT;
		last=bytes[position++]-'0';
		bool cubic=last==1 || last==2 || last==5 || last==6;
		if (cubic) curves++; else lines++;
		if (2+7*curves+3*lines>8193) return IO_ERROR_OUT_OF_RANGE;
		if (records!=NULL) records[i].type=last;
		size_t fields=cubic ? 6 : 2;
		for (size_t j=0; j<fields; j++) {
			size_t width=env->coordinate_size;
			if (i+1==*count && j+1==fields && length-position<width) width=length-position;
			if (width==0 || width>length-position) return IO_ERROR_INVALID_FORMAT;
			status=st_rip3_command_field(&value,bytes+position,width,env->base_math);
			if (status!=0) return status;
			if (records!=NULL) {
				if (j%2==0) records[i].points[j/2].x=project(value,frame->device_width,frame->world_width);
				else records[i].points[j/2].y=project(value,frame->device_height,frame->world_height);
			}
			position+=width;
		}
	}
	if (position!=length || (!closed && (last==1 || last==2 || last==5 || last==6))) return IO_ERROR_INVALID_FORMAT;
	if (curves!=0 && *requested==0) return IO_ERROR_OUT_OF_RANGE;
	return 0;
}

static int
paint(const struct st_rip3_command_host *host, const struct st_rip3_environment *env,
    const struct st_rip3_port_info *port, const struct st_rip3_bezier_path *path,
    bool closed, bool filled, bool *operation)
{
	struct st_rip3_style style;
	struct st_rip3_vector *points;
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
	*operation=true;
	if (filled) {
		bool uses_foreground=false,uses_background=false;
		if (host->borders==NULL) return IO_ERROR_NOT_SUPPORTED;
		borders=*host->borders;
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
	points=malloc(path->count*sizeof(*points));
	if (points==NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i=0; i<path->count; i++) points[i]=(struct st_rip3_vector){path->points[i].x,path->points[i].y};
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
	free(points);return status;
}

int
st_rip3_bezier_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment env;
	struct st_rip3_frame frame;
	struct st_rip3_port_info port;
	struct st_rip3_point controls[4];
	struct st_rip3_bezier_path path={0};
	struct st_rip3_bezier_record *records=NULL;
	uint64_t values[9];
	unsigned environment,current,requested=0;
	size_t count=0,body=length-1;
	bool standalone=command[0]=='Z',closed=command[0]=='z' || command[0]=='x';
	int status;
	if (memchr(command+1,' ',body)!=NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_command_environment(host,&environment,&env);
	if (status!=0) return status;
	if (standalone) {
		size_t width=env.coordinate_size;
		if (body<=8*width || body>8*width+2) return IO_ERROR_INVALID_FORMAT;
		for (size_t i=0; i<9; i++) {
			status=st_rip3_command_field(values+i,command+1+i*width,
			    i==8 ? body-8*width : width,env.base_math);
			if (status!=0) return status;
		}
		requested=(unsigned)values[8];
		if (requested<2) return 0;
	}
	else {
		status=poly_records(command+1,body,&env,NULL,closed,NULL,&count,&requested);
		if (status!=0) return status;
	}
	if (host->ports==NULL) return IO_ERROR_NOT_SUPPORTED;
	status=st_rip3_command_frame(host,environment,&frame);
	if (status==0) status=st_rip3_ports_current(host->ports,&current);
	if (status==0) status=st_rip3_ports_info(host->ports,current,&port);
	if (status!=0 || !port.viewport_active) return status;
	if (standalone) {
		for (size_t i=0; i<4; i++) controls[i]=(struct st_rip3_point){
		    project(values[2*i],frame.device_width,frame.world_width),
		    project(values[2*i+1],frame.device_height,frame.world_height)};
		path.count=requested;
		path.points=malloc(path.count*sizeof(*path.points));
		if (path.points==NULL) return IO_ERROR_OUT_OF_MEMORY;
		status=st_rip3_bezier_sample(path.points,controls,requested);
	}
	else {
		records=malloc(count*sizeof(*records));
		if (records==NULL) return IO_ERROR_OUT_OF_MEMORY;
		status=poly_records(command+1,body,&env,&frame,closed,records,&count,&requested);
		if (status==0) status=st_rip3_bezier_expand(&path,records,count,requested,closed);
		free(records);
	}
	if (status==0) status=paint(host,&env,&port,&path,closed,command[0]=='x',operation);
	st_rip3_bezier_free(&path);return status;
}
