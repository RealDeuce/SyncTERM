/* Shared metric preparation for ordinary layout and logical cursor snapshots. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OUTLINE_FONT_INTERNAL_H_
#define ST_RIP3_OUTLINE_FONT_INTERNAL_H_

#include "st_rip3_outline_font.h"

#include <stdint.h>

/* All 256 raw-code 1/1440-inch advances before DPI/spacing, plus the
 * unrotated global device height. Truncated zero DPI is valid for metrics.
 * No outline path is read. Revalidates the immutable file/style and preserves
 * both outputs on error.
 * This service is below parser/session boundaries, whose errors stay nonfatal. */
int st_rip3_outline_font_metric_map(uint64_t result[256], uint64_t *height, const struct st_rip3_outline_font *font);

#endif
