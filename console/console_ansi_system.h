/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_ANSI_SYSTEM_H_
#define ST_CONSOLE_ANSI_SYSTEM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_console_ansi_system;

int st_console_ansi_system_open(struct st_console_ansi_system **result,
	bool *input_available, bool utf8);
int st_console_ansi_system_read(struct st_console_ansi_system *system,
	void *buffer, size_t size, size_t *received, unsigned int timeout_ms);
bool st_console_ansi_system_size(struct st_console_ansi_system *system,
	uint32_t *columns, uint32_t *rows);
void st_console_ansi_system_close(struct st_console_ansi_system *system);

#endif
