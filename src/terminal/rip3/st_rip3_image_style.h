/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_IMAGE_STYLE_H_
#define ST_RIP3_IMAGE_STYLE_H_

struct st_rip3_ports;
struct st_rip3_frame;
struct st_rip3_rect;

/* Normalize WORLD corners, project once and add the stored far-edge unit.
 * No viewport clipping, selection, protection or activation gating occurs.
 * Macro callers apply their separate gates before using this shared setter.
 * Invalid/unrepresentable input retains the complete stored record. */
int st_rip3_image_style_define(struct st_rip3_ports *ports, unsigned number,
	const struct st_rip3_frame *frame, const struct st_rip3_rect *world, unsigned flags);

#endif
