/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ROUNDED_RECTANGLE_INTERNAL_H_
#define ST_RIP3_ROUNDED_RECTANGLE_INTERNAL_H_

#include "st_rip3_rounded_rectangle.h"

#include <stdbool.h>
#include <stdint.h>

/* Wire geometry has independent fitted corner semiaxes. The public standalone
 * circular-radius API retains its existing contract. A NULL brush/pen disables
 * that operation. Fill uses mode; an enabled filled border uses COPY afterward. */
int st_rip3_rounded_rectangle_render(struct st_rip3_ports *ports,
	const struct st_rip3_rounded_rectangle *rectangle, double radius_y,
	const struct st_rip3_pen *pen, const struct st_rip3_fill *fill,
	uint32_t foreground, uint32_t background, uint32_t drawing,
	enum st_rip3_write_mode mode);

#endif
