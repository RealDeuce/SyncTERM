/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_ICONS_H_
#define CTERM_RIP_ICONS_H_

#include <stdint.h>
#include <stddef.h>

struct builtin_icon {
	const char     *filename;
	uint16_t        width;
	uint16_t        height;
	const uint8_t  *data;
};

extern const struct builtin_icon builtin_icons[];
extern const size_t builtin_icon_count;

#endif
