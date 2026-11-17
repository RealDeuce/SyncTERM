/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_wallpaper.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdint.h>
#include <stdlib.h>

static uint32_t
phase(int64_t value, uint32_t period)
{
	int64_t remainder = value%period;
	return (uint32_t)(remainder < 0 ? remainder+period : remainder);
}

int
st_rip3_wallpaper(struct st_rip3_ports *ports,
    const struct st_rip3_bitmap_destination *seed, bool stagger)
{
	struct st_rip3_port_info info;
	unsigned current;
	int status;

	if (ports == NULL || seed == NULL || seed->x0 > seed->x1 || seed->y0 > seed->y1) return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_current(ports,&current);
	if (status == 0) status = st_rip3_ports_info(ports,current,&info);
	if (status != 0 || !info.viewport_active) return status;
	int32_t xmax = info.viewport.x1-info.viewport.x0, ymax = info.viewport.y1-info.viewport.y0;
	if (seed->x0 < 0 || seed->y0 < 0 || seed->x0 > xmax || seed->y0 > ymax) return IO_ERROR_OUT_OF_RANGE;
	int32_t left = (int32_t)seed->x0, top = (int32_t)seed->y0;
	int32_t right = seed->x1 > xmax ? xmax : (int32_t)seed->x1;
	int32_t bottom = seed->y1 > ymax ? ymax : (int32_t)seed->y1;
	if (left == 0 && top == 0 && right == xmax && bottom == ymax) return 0;
	if (info.clip.x1 < 0 || info.clip.y1 < 0) return 0;
	/* A seed crossing the physical bitmap still needs original-client pixel
	 * policy reconciliation. Do not shrink its period or invent padding. */
	if (right > info.clip.x1 || bottom > info.clip.y1) return IO_ERROR_NOT_SUPPORTED;
	uint32_t width = (uint32_t)(right-left)+1, height = (uint32_t)(bottom-top)+1;
	if ((uint64_t)width*height > SIZE_MAX) return IO_ERROR_OUT_OF_RANGE;
	uint8_t *pixels = malloc((size_t)width*height);
	if (pixels == NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		status = st_rip3_ports_read_viewport(ports,current,left+(int32_t)x,
		    top+(int32_t)y,pixels+(size_t)y*width+x);
		if (status != 0) { free(pixels); return status; }
	}
	for (int32_t y = 0; y <= info.clip.y1; y++) {
		int64_t dy = (int64_t)y-top, row = dy/height;
		if (dy < 0 && dy%height != 0) row--;
		int64_t shift = stagger && row%2 != 0 ? width/2 : 0;
		uint32_t sy = phase(dy,height);
		for (int32_t x = 0; x <= info.clip.x1; x++) {
			uint32_t sx = phase((int64_t)x-left+shift,width);
			(void)st_rip3_ports_plot(ports,x,y,pixels[(size_t)sy*width+sx],ST_RIP3_COPY);
		}
	}
	free(pixels); return 0;
}
