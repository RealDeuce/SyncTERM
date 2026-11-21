/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COMMAND_INTERNAL_H_
#define ST_RIP3_COMMAND_INTERNAL_H_

#include "st_rip3_commands.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_environment;
struct st_rip3_frame;
struct st_rip3_port_info;
struct st_rip3_fill;
int st_rip3_mouse_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_button_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);

/* Private wire-field and execution helpers. Every status is consumed by the
 * public void processing boundary; none is a terminal/session return status. */
int st_rip3_command_field(uint64_t *result, const uint8_t *bytes,
	size_t length, unsigned radix);
int st_rip3_command_environment(const struct st_rip3_command_host *host,
	unsigned *number, struct st_rip3_environment *value);
int st_rip3_command_frame(const struct st_rip3_command_host *host,
	unsigned environment, struct st_rip3_frame *result);
int st_rip3_command_ink(const struct st_rip3_command_host *host,
	unsigned bits, uint32_t color, uint8_t *result);
/* Resolve fixed drawing-context phase into the device kernel's viewport-
 * relative phase, without changing the caller's stored mask/phase record. */
void st_rip3_command_fill(const struct st_rip3_port_info *port,
	const struct st_rip3_fill *source, struct st_rip3_fill *result);
int st_rip3_bitmap_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_bitmap_load_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_image_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_get_image_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_text_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_font_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_curve_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_skew_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_bezier_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_rounded_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_path_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_drawing_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);
int st_rip3_port_commands_execute(const struct st_rip3_command_host *host,
	const uint8_t *command, size_t length, bool *operation);

#endif
