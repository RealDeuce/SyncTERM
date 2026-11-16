/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_SURFACE_H_
#define ST_RIP3_SURFACE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_pixel_buffer;
struct st_rip3_surface;

/* Palette values are untagged 0xRRGGBB. The surface stores 8-bit indexes,
 * so palette changes affect existing pixels when the next snapshot is made. */
struct st_rip3_palette {
	uint32_t rgb[256];
};

struct st_rip3_rect {
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
};

enum st_rip3_write_mode {
	ST_RIP3_COPY,
	ST_RIP3_XOR,
	ST_RIP3_OR,
	ST_RIP3_AND,
	ST_RIP3_NOT,
};

/* Port-copy transfer mode 4 inverts the SOURCE; level-0 NOT inverts the
 * DESTINATION. Separate names keep that protocol distinction explicit. */
enum st_rip3_transfer_mode {
	ST_RIP3_TRANSFER_COPY,
	ST_RIP3_TRANSFER_XOR,
	ST_RIP3_TRANSFER_OR,
	ST_RIP3_TRANSFER_AND,
	ST_RIP3_TRANSFER_INVERT,
};

/* Palette defaults normalize the documented 6-bit table to 8-bit channels,
 * rounding to nearest. Desktop master colors use the RGBrgb bit permutation.
 * Setters validate the whole span before changing any entry. */
int st_rip3_palette_defaults(struct st_rip3_palette *palette);
int st_rip3_palette_desktop(struct st_rip3_palette *palette, size_t start,
	const unsigned *master_colors, size_t count);
int st_rip3_palette_rgb(struct st_rip3_palette *palette, size_t start,
	unsigned bits, const uint32_t *colors, size_t count);
/* Closest match uses squared RGB distance; equal distances prefer the first
 * index. Color-map input is already an index. Result is unchanged on error. */
int st_rip3_palette_color(const struct st_rip3_palette *palette, bool direct,
	uint32_t color, uint8_t *result);

/* A surface is owned by the terminal session thread. Dimensions may also
 * describe offscreen ports; the application selects the device canvas tier.
 * Coordinates below are device pixels, not world coordinates. */
int st_rip3_surface_create(struct st_rip3_surface **result,
	uint32_t width, uint32_t height);
void st_rip3_surface_destroy(struct st_rip3_surface *surface);
/* Exact owned index-plane clone; no RGB conversion or palette association. */
int st_rip3_surface_clone(struct st_rip3_surface **result,
	const struct st_rip3_surface *surface);
int st_rip3_surface_size(const struct st_rip3_surface *surface,
	uint32_t *width, uint32_t *height);
int st_rip3_surface_get(const struct st_rip3_surface *surface,
	int32_t x, int32_t y, uint8_t *result);
/* Clip rectangles have inclusive endpoints; NULL means the whole surface.
 * Drawing outside the surface/clip is a successful no-op. NOT ignores color.
 * Reversed rectangles and invalid write modes fail before changing pixels. */
int st_rip3_surface_plot(struct st_rip3_surface *surface,
	const struct st_rip3_rect *clip, int32_t x, int32_t y, uint8_t color,
	enum st_rip3_write_mode mode);
int st_rip3_surface_fill(struct st_rip3_surface *surface,
	const struct st_rip3_rect *clip, const struct st_rip3_rect *area,
	uint8_t color, enum st_rip3_write_mode mode);
/* Clip both rectangles first, then scale the remaining source to fill the
 * remaining destination. Nearest source pixel centers preserve palette indexes.
 * Overlapping copies read a snapshot, including when the write mode combines
 * source and destination. Allocation failure leaves the destination unchanged. */
int st_rip3_surface_copy(struct st_rip3_surface *destination,
	const struct st_rip3_rect *destination_clip,
	const struct st_rip3_rect *destination_area,
	const struct st_rip3_surface *source,
	const struct st_rip3_rect *source_clip,
	const struct st_rip3_rect *source_area,
	enum st_rip3_transfer_mode mode);
/* Unscaled transfer: source_area's original upper-left maps to destination_x/y.
 * Clipping either operand discards the corresponding pixels in both operands,
 * preserving that translation. The uncovered source is retained. Overlap reads
 * original source pixels; snapshot failure leaves the destination unchanged. */
int st_rip3_surface_blit(struct st_rip3_surface *destination,
	const struct st_rip3_rect *destination_clip,
	int32_t destination_x, int32_t destination_y,
	const struct st_rip3_surface *source,
	const struct st_rip3_rect *source_clip,
	const struct st_rip3_rect *source_area,
	enum st_rip3_transfer_mode mode);
/* One coherent, owned tagged-RGB capture; no borrowed mutable pixel planes.
 * The palette belongs to the caller (ultimately the active palette slot). */
int st_rip3_surface_snapshot(const struct st_rip3_surface *surface,
	const struct st_rip3_palette *palette, struct st_pixel_buffer **result);

#endif
