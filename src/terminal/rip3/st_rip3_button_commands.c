/* Button-style wire parsing. Statuses remain below the void parser boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_environments.h"
#include "io_error.h"

int
st_rip3_button_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;unsigned number;
	int status=st_rip3_command_environment(host,&number,&environment);
	if (status!=0) return status;
	if (host->button_styles==NULL) return IO_ERROR_NOT_SUPPORTED;
	size_t count=length-2;
	if (command[0]=='2') {
		if (count==0 || count>2) return IO_ERROR_INVALID_FORMAT;
		uint64_t target;
		status=st_rip3_command_field(&target,command+2,count,environment.base_math);
		if (status!=0) return status;
		if (target>=36) return IO_ERROR_OUT_OF_RANGE;
		*operation=true;return st_rip3_button_styles_select(host->button_styles,(unsigned)target);
	}
	size_t required=2*environment.coordinate_size+26;
	if (count<required-1 || count>required+6) return IO_ERROR_INVALID_FORMAT;
	uint64_t values[14];size_t at=2;
	for (unsigned i=0;i<14;i++) {
		size_t width=i<2 ? environment.coordinate_size : i==3 ? 4 : 2;
		if (i==13 && count==required-1) width=1;
		status=st_rip3_command_field(values+i,command+at,width,environment.base_math);
		if (status!=0) return status;
		at+=width;
	}
	if (count>required) {
		uint64_t ignored;
		status=st_rip3_command_field(&ignored,command+at,1,environment.base_math);
		if (status!=0) return status;
		++at; /* other_port is ignored, including on the clipboard path. */
		if (count>required+1) {
			status=st_rip3_command_field(&ignored,command+at,count-required-1,environment.base_math);
			if (status!=0) return status;
			if (ignored!=0) return IO_ERROR_NOT_SUPPORTED;
		}
	}
	if (values[2]>4 || values[10]>=36 || values[11]>255) return IO_ERROR_OUT_OF_RANGE;
	for (unsigned i=5;i<=9;i++) if (values[i]>255) return IO_ERROR_OUT_OF_RANGE;
	if (values[12]>255 || values[13]>255) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_frame frame;
	status=st_rip3_environments_frame(host->environments,number,&frame);
	if (status!=0) return status;
	/* Definition converts once and stores WORDs. Bevel uses only X scale;
	 * creation later takes its low byte. All colors ignore RGB mode. */
	struct st_rip3_button_style value={
	    .width=(uint16_t)(values[0]*frame.device_width/frame.world_width),
	    .height=(uint16_t)(values[1]*frame.device_height/frame.world_height),
	    .orientation=(unsigned)values[2],.flags=(uint16_t)values[3],
	    .bevel=(uint16_t)(values[4]*frame.device_width/frame.world_width),
	    .group=(unsigned)values[10],.flags2=(uint8_t)values[11],
	    .colors={(uint8_t)values[5],(uint8_t)values[6],(uint8_t)values[7],(uint8_t)values[8],
	        (uint8_t)values[9],(uint8_t)values[12],(uint8_t)values[13]},
	};
	status=st_rip3_button_styles_current(host->button_styles,&number);
	if (status!=0) return status;
	*operation=true;return st_rip3_button_styles_set(host->button_styles,number,&value);
}
