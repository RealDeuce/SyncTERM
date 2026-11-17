/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_image_style.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stddef.h>
#include <stdint.h>

int
st_rip3_image_style_define(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_frame *frame, const struct st_rip3_rect *world, unsigned flags)
{
	if (ports == NULL || frame == NULL || world == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (flags > 63) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_point first = {world->x0,world->y0}, last = {world->x1,world->y1};
	if (first.x > last.x) { int32_t swap = first.x; first.x = last.x; last.x = swap; }
	if (first.y > last.y) { int32_t swap = first.y; first.y = last.y; last.y = swap; }
	int status = st_rip3_world_to_device(frame,&first,&first);
	if (status == 0) status = st_rip3_world_to_device(frame,&last,&last);
	if (status != 0) return status;
	if (last.x == INT32_MAX || last.y == INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_rect record = {first.x,first.y,last.x+1,last.y+1};
	return st_rip3_ports_image_style(ports,number,&record,flags);
}
