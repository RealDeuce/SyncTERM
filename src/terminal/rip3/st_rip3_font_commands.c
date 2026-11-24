/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"
#include "st_rip3_styles.h"
#include "io_error.h"

#include <stdint.h>
#include <string.h>

int
st_rip3_font_argument_prepare(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, struct st_rip3_outline_style *outline)
{
	static const size_t widths[] = {1,1,4,2,2,2,2,2,2,2,6};
	uint64_t values[11];
	size_t offset = 1;
	struct st_rip3_environment env;
	struct st_rip3_style_info info;
	unsigned environment, number;

	if (length <= ST_RIP3_OUTLINE_NAME_OFFSET || command[ST_RIP3_OUTLINE_NAME_OFFSET] == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (host->styles == NULL) return IO_ERROR_NOT_SUPPORTED;
	int status = st_rip3_command_environment(host, &environment, &env);
	if (status == 0) status = st_rip3_styles_current(host->styles, &number);
	if (status == 0) status = st_rip3_styles_info(host->styles, number, &info);
	if (status != 0) return status;
	if (info.protected) return IO_ERROR_PERMISSION_DENIED;
	for (size_t i = 0; i < sizeof(widths)/sizeof(widths[0]); i++) {
		/* Reserved digits can exceed 32 bits. Their accumulator's original
		 * wrap has no observable effect; validate every digit without storing it. */
		status = st_rip3_number_decode(&values[i],command+offset,widths[i],64);
		if (status != 0) return status;
		offset += widths[i];
	}
	if (values[5] > 2700 || values[5]%900 != 0
	    || values[6] > 2700 || values[6]%900 != 0 || values[8] == 0)
		return IO_ERROR_OUT_OF_RANGE;
	uint32_t baseline = (uint32_t)values[7];
	if ((values[2]&512) != 0) {
		struct st_rip3_frame frame;
		status = st_rip3_environments_frame(host->environments,environment,&frame);
		if (status != 0) return status;
		baseline = (uint32_t)(values[7]*frame.device_height/frame.world_height);
	}
	*outline = (struct st_rip3_outline_style){
	    .compatibility_s=(unsigned)values[0],.compatibility_f=(unsigned)values[1],
	    .flags=(uint32_t)values[2],.size=(unsigned)values[3],.shadow=(unsigned)values[4],
	    .string_rotation=(unsigned)values[5],.character_rotation=(unsigned)values[6],
	    .baseline=baseline,.spacing=(unsigned)values[8],
	};
	return 0;
}

int
st_rip3_font_commands_expanded(const struct st_rip3_command_host *host,
    const struct st_rip3_outline_style *outline, const uint8_t *name, size_t length)
{
	/* Use the style selected after the provider runs and retain any other
	 * changes it made. Numeric fields, including baseline, are already decoded. */
	struct st_rip3_style style;
	unsigned number;
	int status = st_rip3_styles_current(host->styles, &number);
	if (status == 0) status = st_rip3_styles_get(host->styles, number, &style);
	if (status != 0) return status;
	style.outline = *outline;
	if (length >= sizeof(style.outline.name)) length = sizeof(style.outline.name)-1;
	if (length != 0) memcpy(style.outline.name, name, length);
	style.outline.name[length] = 0;
	style.font_mode = ST_RIP3_FONT_OUTLINE;
	return st_rip3_styles_set(host->styles, number, &style);
}

int
st_rip3_font_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	if (command[0] == 'y') {
		struct st_rip3_outline_style outline;
		int status = st_rip3_font_argument_prepare(host, command, length, &outline);
		if (status != 0) return status;
		/* This entry has no graphical evaluator. The live session handles
		 * quoted names and variable syntax before it commits the style. */
		for (size_t i = ST_RIP3_OUTLINE_NAME_OFFSET; i < length; i++) {
			uint8_t c = command[i];
			if (c == '$' || c == '[' || c == '\\' || c == '^' || c == '`'
			    || (i+1 < length && ((c == '<' && command[i+1] == '<')
			    || (c == '(' && command[i+1] == '('))))
				return IO_ERROR_NOT_SUPPORTED;
		}
		*operation = true;
		return st_rip3_font_commands_expanded(host, &outline,
		    command + ST_RIP3_OUTLINE_NAME_OFFSET, length - ST_RIP3_OUTLINE_NAME_OFFSET);
	}
	struct st_rip3_environment environment;
	struct st_rip3_style style;
	unsigned env, number;
	const uint8_t *arguments = command+1;
	size_t count = length-1;
	int status;

	if (host->styles == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host,&env,&environment);
	if (status == 0) status = st_rip3_styles_current(host->styles,&number);
	if (status == 0) status = st_rip3_styles_get(host->styles,number,&style);
	if (status != 0) return status;
	uint64_t attributes;
	if (count == 0 || count > 2) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&attributes,arguments,count,environment.base_math);
	if (status != 0) return status;
	if (attributes > 15 || style.font_mode != ST_RIP3_FONT_OUTLINE) return IO_ERROR_OUT_OF_RANGE;
	style.outline.flags |= (uint32_t)attributes;
	*operation = true;
	return st_rip3_styles_set(host->styles,number,&style);
}
