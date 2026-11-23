/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COLUMNS_H_
#define ST_RIP3_COLUMNS_H_

#include "st_rip3_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_command_host;

enum { ST_RIP3_COLUMN_COUNT = 36 };

struct st_rip3_column {
	struct st_rip3_rect area;
	unsigned article;
	bool defined;
};

struct st_rip3_columns {
	struct st_rip3_column slots[ST_RIP3_COLUMN_COUNT];
	unsigned article, current;
	int64_t offset;
	bool active;
};

int st_rip3_columns_define(struct st_rip3_columns *, const struct st_rip3_command_host *,
	const uint8_t *command, size_t length);
int st_rip3_columns_line(struct st_rip3_columns *, const struct st_rip3_command_host *,
	const uint8_t *text, size_t length, bool justify);

#endif
