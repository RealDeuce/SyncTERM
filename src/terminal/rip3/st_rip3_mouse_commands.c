/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_mouse.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "io_error.h"
#include <limits.h>

static int
remove_fields(const struct st_rip3_command_host *host,
    const struct st_rip3_environment *env, unsigned number,
    const uint8_t *command, size_t length, bool *operation)
{
	if (length!=2+4*env->coordinate_size+4) return IO_ERROR_INVALID_FORMAT;
	uint64_t values[5];size_t at=2;
	for (unsigned i=0;i<5;i++) {
		size_t width=i==4 ? 4 : env->coordinate_size;
		int status=st_rip3_command_field(values+i,command+at,width,env->base_math);
		if (status!=0) return status;
		at+=width;
	}
	/* Optional erase/pattern context and selector edge policy are B120.
	 * Reject these requests before deleting or changing press feedback. */
	if ((values[4]&96)!=0) return IO_ERROR_NOT_SUPPORTED;
	if (values[0]>values[2] || values[1]>values[3]) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_frame frame;struct st_rip3_port_info port;
	int status=st_rip3_environments_frame(host->environments,number,&frame);
	if (status==0) status=st_rip3_ports_info(host->ports,0,&port);
	if (status!=0) return status;
	/* Read PORT0's stored origin without changing selection, clipping the
	 * selector, or relocating existing creation-time global hit rectangles. */
	int64_t ox=(int64_t)port.bounds.x0+port.viewport.x0,oy=(int64_t)port.bounds.y0+port.viewport.y0;
	int64_t x0=ox+(int64_t)(values[0]*frame.device_width/frame.world_width);
	int64_t y0=oy+(int64_t)(values[1]*frame.device_height/frame.world_height);
	int64_t x1=ox+(int64_t)(values[2]*frame.device_width/frame.world_width);
	int64_t y1=oy+(int64_t)(values[3]*frame.device_height/frame.world_height);
	if (x0<0 || y0<0 || x1>INT32_MAX || y1>INT32_MAX || x0>=x1 || y0>=y1) return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_rect area={(int32_t)x0,(int32_t)y0,(int32_t)x1,(int32_t)y1};
	*operation=true;return st_rip3_mouse_remove(host->mouse,&area,(unsigned)values[4]&31u);
}

int
st_rip3_mouse_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	if (host->mouse==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (command[1]=='K') {
		if (length!=2) return IO_ERROR_INVALID_FORMAT;
		*operation=true;st_rip3_mouse_kill(host->mouse);return 0;
	}
	struct st_rip3_environment env;unsigned number;
	int status=st_rip3_command_environment(host,&number,&env);
	if (status!=0) return status;
	if (command[1]=='k') return remove_fields(host,&env,number,command,length,operation);
	size_t fixed=2+2+4*env.coordinate_size+2+5;
	if (length<fixed) return IO_ERROR_INVALID_FORMAT;
	uint64_t values[8];size_t at=2;
	for (unsigned i=0;i<8;i++) {
		size_t width=i==0 ? 2 : i<=4 ? env.coordinate_size : i==7 ? 5 : 1;
		status=st_rip3_command_field(values+i,command+at,width,env.base_math);
		if (status!=0) return status;
		at+=width;
	}
	if (values[5]>1 || values[6]>1) return IO_ERROR_OUT_OF_RANGE;
	if (values[7]!=0) return IO_ERROR_NOT_SUPPORTED;
	/* Clear/zoom needs its own native text-window lifecycle; it must not be
	 * approximated by SOFT (which resets unrelated tables and mouse fields). */
	if (values[6]!=0) return IO_ERROR_NOT_SUPPORTED;
	if (values[1]>values[3] || values[2]>values[4]) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_frame frame;
	status=st_rip3_environments_frame(host->environments,number,&frame);
	if (status!=0) return status;
	struct st_rip3_port_info port;
	status=st_rip3_ports_info(host->ports,0,&port);
	if (status!=0) return status;
	if (!port.viewport_active) return 0;
	int64_t x0=(int64_t)(values[1]*frame.device_width/frame.world_width);
	int64_t y0=(int64_t)(values[2]*frame.device_height/frame.world_height);
	int64_t x1=(int64_t)(values[3]*frame.device_width/frame.world_width);
	int64_t y1=(int64_t)(values[4]*frame.device_height/frame.world_height);
	if (x0<port.clip.x0) x0=port.clip.x0;
	if (y0<port.clip.y0) y0=port.clip.y0;
	if (x1>(int64_t)port.clip.x1+1) x1=(int64_t)port.clip.x1+1;
	if (y1>(int64_t)port.clip.y1+1) y1=(int64_t)port.clip.y1+1;
	if (x0>=x1 || y0>=y1) return 0;
	int64_t ox=(int64_t)port.bounds.x0+port.viewport.x0;
	int64_t oy=(int64_t)port.bounds.y0+port.viewport.y0;
	if (ox+x0<0 || oy+y0<0 || ox+x1>INT32_MAX || oy+y1>INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_rect area={(int32_t)(ox+x0),(int32_t)(oy+y0),(int32_t)(ox+x1),(int32_t)(oy+y1)};
	const uint8_t *program=command+at;size_t program_length=length-at;
	int identity=st_rip3_mouse_identity(&program,&program_length);
	*operation=true;return st_rip3_mouse_define(host->mouse,ST_RIP3_MOUSE_SIMPLE,NULL,&area,values[5]!=0,false,identity,program,program_length);
}
