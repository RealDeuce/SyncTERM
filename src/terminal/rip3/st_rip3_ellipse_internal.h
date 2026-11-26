/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ELLIPSE_INTERNAL_H_
#define ST_RIP3_ELLIPSE_INTERNAL_H_

#include "st_rip3_draw_internal.h"
#include "st_rip3_ellipse.h"

/* Prepare owned curve coverage without painting, shared by rounded corners.
 * Errors preserve result; release successful coverage with stroke_free. */
int st_rip3_ellipse_collect(struct st_rip3_ports *ports, const struct st_rip3_ellipse *ellipse,
    double start_degrees, double sweep_degrees, enum st_rip3_ellipse_region region, uint32_t thickness,
    struct st_rip3_stroke_mask *result);

#endif
