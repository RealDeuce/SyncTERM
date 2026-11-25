/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_ELLIPSE_H_
#define ST_RIP3_ELLIPSE_H_

#include "st_rip3_fill.h"

/* Parametric device-pixel ellipse:
 * x(t) = center_x + xx*cos(t) + xy*sin(t)
 * y(t) = center_y + yx*cos(t) + yy*sin(t)
 * A general basis permits world scaling AFTER rotation, including unequal
 * x/y scales. The constructor uses screen coordinates (positive y downward)
 * and counterclockwise rotation/parameter angles. Finite centers and basis
 * components up to +/-1024*INT32_MAX retain wide wire projections until
 * visible raster coverage; no axis is saturated before clipping. */
struct st_rip3_ellipse {
	double center_x;
	double center_y;
	double xx;
	double xy;
	double yx;
	double yy;
};

enum st_rip3_ellipse_region {
	ST_RIP3_ELLIPSE_PIE,
	ST_RIP3_ELLIPSE_CHORD,
	ST_RIP3_ELLIPSE_ARC, /* Outline only; no closing edges. */
};

int st_rip3_ellipse_axes(struct st_rip3_ellipse *result,
	double center_x, double center_y, double radius_x, double radius_y,
	double rotation_degrees);
/* Fill only: no border and no flood fill. Samples are pixel centers in the
 * continuous ellipse/sector/segment. A full 360-degree sweep is the ellipse;
 * zero sweep or a singular basis has no interior. Sweep is in [0,360] degrees.
 * PIE starts inclusive and ends exclusive; chord boundary ties are assigned
 * consistently to one of the two complementary segments. Pattern phase is
 * explicit in fill. Inputs/errors cannot change pixels before validation.
 * Raw wire start/end and aspect-correct circle policy belong to the caller. */
int st_rip3_draw_ellipse_interior(struct st_rip3_ports *ports,
	const struct st_rip3_ellipse *ellipse, double start_degrees,
	double sweep_degrees, enum st_rip3_ellipse_region region,
	const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
	enum st_rip3_write_mode mode);

/* Curves use a solid stroke, independent of the line pattern. Each pixel is
 * written once, including at joins and closing edges. Generated chords have
 * at most 1/4 device-pixel geometric deviation from the parametric curve.
 * Singular bases draw their degenerate line/point; zero sweep draws nothing. */
int st_rip3_draw_ellipse_outline(struct st_rip3_ports *ports,
	const struct st_rip3_ellipse *ellipse, double start_degrees,
	double sweep_degrees, enum st_rip3_ellipse_region region,
	uint32_t thickness, uint32_t color, enum st_rip3_write_mode mode);
/* Border zero disables it. Otherwise the solid border is drawn in COPY mode
 * after the interior, regardless of mode. All outline scratch allocation and
 * input validation precede any drawing. Only PIE/CHORD accept interiors. */
int st_rip3_draw_filled_ellipse(struct st_rip3_ports *ports,
	const struct st_rip3_ellipse *ellipse, double start_degrees,
	double sweep_degrees, enum st_rip3_ellipse_region region,
	const struct st_rip3_fill *fill, uint32_t foreground, uint32_t background,
	uint32_t border_thickness, uint32_t border_color,
	enum st_rip3_write_mode mode);

#endif
