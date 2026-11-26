/* Shared metric preparation for ordinary layout and logical cursor snapshots. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OUTLINE_FONT_INTERNAL_H_
#define ST_RIP3_OUTLINE_FONT_INTERNAL_H_

#include <stdint.h>

#include "st_rip3_outline_font.h"

/* Convert a nonnegative RFF metric from 1/1440-inch units to pixels.
 * Divide before scaling so a wide intermediate product cannot wrap. */
static inline uint64_t
st_rip3_outline_device_extent(uint64_t logical, uint32_t dpi)
{
	return logical / 1440 * dpi + logical % 1440 * dpi / 1440;
}

/* All 256 raw-code 1/1440-inch advances before DPI/spacing, plus the
 * unrotated global device height. Truncated zero DPI is valid for metrics.
 * No outline path is read. Revalidates the immutable file/style and preserves
 * both outputs on error.
 * This service is below parser/session boundaries, whose errors stay nonfatal. */
int st_rip3_outline_font_metric_map(uint64_t result[256], uint64_t *height,
    const struct st_rip3_outline_font *font);

#endif
