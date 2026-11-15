/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_GEOMETRY_H_
#define ST_RIP3_GEOMETRY_H_

#include <stdint.h>

enum st_rip3_canvas {
	ST_RIP3_CANVAS_640_480,
	ST_RIP3_CANVAS_800_600,
	ST_RIP3_CANVAS_1024_768,
};

struct st_rip3_frame {
	uint32_t device_width;
	uint32_t device_height;
	uint32_t world_width;
	uint32_t world_height;
};

struct st_rip3_point {
	int32_t x;
	int32_t y;
};

/* Initialize the selected device frame with the 640x350 compatibility world.
 * World dimensions are counts, not the inclusive last pixel coordinate. */
int st_rip3_frame_init(struct st_rip3_frame *frame, enum st_rip3_canvas canvas);
int st_rip3_frame_world(struct st_rip3_frame *frame,
	uint32_t width, uint32_t height);
/* Exact integer-ratio mapping, rounding down to the containing device/world
 * pixel, including for negative coordinates. Mapping does not clip: the frame
 * boundary (world_width,world_height) maps to (device_width,device_height),
 * outside the last pixel. Ports/viewports apply clipping and translations.
 * Results and frames remain unchanged on failure. */
int st_rip3_world_to_device(const struct st_rip3_frame *frame,
	const struct st_rip3_point *world, struct st_rip3_point *device);
int st_rip3_device_to_world(const struct st_rip3_frame *frame,
	const struct st_rip3_point *device, struct st_rip3_point *world);

#endif
