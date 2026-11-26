/* Button-style wire parsing. Statuses remain below the void parser boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "io_error.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"

static void
apply_protection(struct st_rip3_button_styles *styles, unsigned number, uint64_t flags,
    const struct st_rip3_command_sink *sink)
{
	if ((flags & 1) != 0)
		st_rip3_command_result(sink, st_rip3_button_styles_protect(styles, number, true), true);
	if ((flags & 2) != 0 && number != 0)
		st_rip3_command_result(sink, st_rip3_button_styles_protect(styles, number, false), true);
}

int
st_rip3_button_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation, const struct st_rip3_command_sink *sink)
{
	struct st_rip3_environment environment;
	unsigned                   number;
	int                        status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	if (host->button_styles == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_fields fields = {command + 2, length - 2};
	if (command[0] == '2') {
		uint64_t target, flags;
		status = st_rip3_command_next_field(&fields, &target, 1, environment.base_math, false);
		if (status == 0)
			status = st_rip3_command_next_field(&fields, &flags, 2, environment.base_math, true);
		if (status != 0)
			return status;
		if (target >= 36)
			return IO_ERROR_OUT_OF_RANGE;
		status = st_rip3_button_styles_current(host->button_styles, &number);
		if (status != 0)
			return status;
		/* Source flags apply before selection. A protection error still
		 * permits the switch and its remaining flags. */
		*operation = true;
		apply_protection(host->button_styles, number, flags >> 2, sink);
		status = st_rip3_button_styles_select(host->button_styles, (unsigned)target);
		if (status == 0)
			apply_protection(host->button_styles, (unsigned)target, flags, sink);
		return status;
	}
	uint64_t values[14];
	for (unsigned i = 0; i < 14; i++) {
		size_t width = i < 2 ? environment.coordinate_size : i == 3 ? 4 : 2;
		status = st_rip3_command_next_field(&fields, values + i, width, environment.base_math, false);
		if (status != 0)
			return status;
	}
	/* The ignored other_port is required, even on the clipboard path. */
	status = st_rip3_command_next_field(&fields, NULL, 1, environment.base_math, false);
	if (status == 0 && fields.remaining != 0)
		status = st_rip3_command_next_field(&fields, NULL, 5, environment.base_math, true);
	if (status != 0)
		return status;
	if (values[2] > 4 || values[10] >= 36 || values[11] > 255)
		return IO_ERROR_OUT_OF_RANGE;
	for (unsigned i = 5; i <= 9; i++)
		if (values[i] > 255)
			return IO_ERROR_OUT_OF_RANGE;
	if (values[12] > 255 || values[13] > 255)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_frame frame;
	status = st_rip3_environments_frame(host->environments, number, &frame);
	if (status != 0)
		return status;
	/* Definition converts once and stores WORDs. Bevel uses only X scale;
	 * creation later takes its low byte. All colors ignore RGB mode. */
	struct st_rip3_button_style value = {
	    .width       = (uint16_t)(values[0] * frame.device_width / frame.world_width),
	    .height      = (uint16_t)(values[1] * frame.device_height / frame.world_height),
	    .orientation = (unsigned)values[2],
	    .flags       = (uint16_t)values[3],
	    .bevel       = (uint16_t)(values[4] * frame.device_width / frame.world_width),
	    .group       = (unsigned)values[10],
	    .flags2      = (uint8_t)values[11],
	    .colors      = {(uint8_t)values[5], (uint8_t)values[6], (uint8_t)values[7], (uint8_t)values[8],
			    (uint8_t)values[9], (uint8_t)values[12], (uint8_t)values[13]},
	};
	status = st_rip3_button_styles_current(host->button_styles, &number);
	if (status != 0)
		return status;
	*operation = true;
	return st_rip3_button_styles_set(host->button_styles, number, &value);
}
