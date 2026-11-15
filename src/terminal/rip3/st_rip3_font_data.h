/* Borrowed immutable historical RIPtel font assets. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_FONT_DATA_H
#define ST_RIP3_FONT_DATA_H

#include <stddef.h>
#include <stdint.h>

struct st_rip3_font_asset {
	const char *filename;
	const uint8_t *data;
	size_t size;
};

size_t st_rip3_font_asset_count(void);
/* NULL for an invalid index; views live for the lifetime of the application. */
const struct st_rip3_font_asset *st_rip3_font_asset_at(size_t index);

#endif
