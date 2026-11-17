/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef RIP3_TEST_IMAGE_AREA_FIXTURE_H_
#define RIP3_TEST_IMAGE_AREA_FIXTURE_H_

#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdint.h>

/* Older independent raster fixtures specify inclusive visible pixel areas.
 * Convert only their SETUP to stored device boundaries, preserving their
 * literal expected rasters. Wire/macro state tests use the raw API directly.
 * The deliberate raw-zero inhibit fixture remains distinct from wire zeros. */
static inline int
rip3_test_image_area(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *pixels, unsigned flags)
{
	if (pixels == NULL || (pixels->x0 == 0 && pixels->y0 == 0 && pixels->x1 == 0 && pixels->y1 == 0))
		return st_rip3_ports_image_style(ports,number,pixels,flags);
	if (pixels->x0 > pixels->x1 || pixels->y0 > pixels->y1) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info info;
	int status = st_rip3_ports_info(ports,number,&info);
	if (status != 0) return status;
	if (!info.defined) return IO_ERROR_NOT_FOUND;
	int32_t xmax = info.viewport.x1-info.viewport.x0, ymax = info.viewport.y1-info.viewport.y0;
	struct st_rip3_rect record = *pixels;
	record.x0 = record.x0 < 0 ? 0 : record.x0 > xmax ? xmax : record.x0;
	record.y0 = record.y0 < 0 ? 0 : record.y0 > ymax ? ymax : record.y0;
	record.x1 = (record.x1 < 0 ? 0 : record.x1 > xmax ? xmax : record.x1)+2;
	record.y1 = (record.y1 < 0 ? 0 : record.y1 > ymax ? ymax : record.y1)+2;
	return st_rip3_ports_image_style(ports,number,&record,flags);
}

#endif
