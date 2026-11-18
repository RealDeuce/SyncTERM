/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_number.h"
#include "st_rip3_styles.h"
#include "io_error.h"

#include <stdint.h>
#include <string.h>

static int
outline_style(struct st_rip3_style *style, const struct st_rip3_command_host *host,
    unsigned environment, const uint8_t *arguments, size_t count)
{
	static const size_t widths[] = {1,1,4,2,2,2,2,2,2,2,6};
	uint64_t values[11];
	size_t offset = 0;
	int status;

	if (count <= 26) return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < sizeof(widths)/sizeof(widths[0]); i++) {
		/* Reserved digits can exceed 32 bits. Their accumulator's original
		 * wrap has no observable effect; validate every digit without storing it. */
		status = st_rip3_number_decode(&values[i],arguments+offset,widths[i],64);
		if (status != 0) return status;
		offset += widths[i];
	}
	if (values[5] > 2700 || values[5]%900 != 0
	    || values[6] > 2700 || values[6]%900 != 0 || values[8] == 0)
		return IO_ERROR_OUT_OF_RANGE;
	/* The complete-command boundary consumes resolved text. Until expansion
	 * is bound here, reject its notation rather than store a literal variable. */
	for (size_t i = offset; i < count; i++) {
		uint8_t c = arguments[i];
		if (c == '$' || c == '[' || c == '\\' || c == '^' || c == '`'
		    || (i+1 < count && ((c == '<' && arguments[i+1] == '<')
		    || (c == '(' && arguments[i+1] == '('))))
			return IO_ERROR_NOT_SUPPORTED;
	}
	uint32_t baseline = (uint32_t)values[7];
	if ((values[2]&512) != 0) {
		struct st_rip3_frame frame;
		status = st_rip3_environments_frame(host->environments,environment,&frame);
		if (status != 0) return status;
		baseline = (uint32_t)(values[7]*frame.device_height/frame.world_height);
	}
	struct st_rip3_outline_style outline = {
	    .compatibility_s=(unsigned)values[0],.compatibility_f=(unsigned)values[1],
	    .flags=(uint32_t)values[2],.size=(unsigned)values[3],.shadow=(unsigned)values[4],
	    .string_rotation=(unsigned)values[5],.character_rotation=(unsigned)values[6],
	    .baseline=baseline,.spacing=(unsigned)values[8],
	};
	size_t name_size = count-offset;
	if (name_size >= sizeof(outline.name)) name_size = sizeof(outline.name)-1;
	memcpy(outline.name,arguments+offset,name_size);
	style->outline = outline;
	style->font_mode = ST_RIP3_FONT_OUTLINE;
	return 0;
}

int
st_rip3_font_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
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
	if (command[0] == 'y') status = outline_style(&style,host,env,arguments,count);
	else {
		uint64_t attributes;
		if (count == 0 || count > 2) return IO_ERROR_INVALID_FORMAT;
		status = st_rip3_command_field(&attributes,arguments,count,environment.base_math);
		if (status != 0) return status;
		if (attributes > 15 || style.font_mode != ST_RIP3_FONT_OUTLINE) return IO_ERROR_OUT_OF_RANGE;
		style.outline.flags |= (uint32_t)attributes;
	}
	if (status != 0) return status;
	*operation = true;
	return st_rip3_styles_set(host->styles,number,&style);
}
