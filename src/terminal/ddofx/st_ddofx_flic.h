/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_FLIC_H_
#define ST_DDOFX_FLIC_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct st_ddofx_flic {
	FILE    *file;
	unsigned width, height, frames;
	uint32_t end;
	uint8_t *image;
};

bool st_ddofx_flic_header(struct st_ddofx_flic *, FILE *);
bool st_ddofx_flic_frame(struct st_ddofx_flic *, uint32_t palette[256]);
#endif
