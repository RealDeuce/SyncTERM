/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BITMAP_ERROR_UI_H_
#define ST_RIP3_BITMAP_ERROR_UI_H_

#include <stdbool.h>

struct st_rip3_ports;
struct st_rip3_palettes;
struct st_rip3_maf_font;
struct st_rip3_bitmap_destination;

/* Borrowed session-owned bindings for a bitmap-error sink. NULL font selects
 * the original canvas-native v3 bitmap font. A custom immutable MAF view is
 * useful for embedding and independent expectations. No STYLE/ENV/cursor,
 * palette, port selection or image-style configuration is changed. */
struct st_rip3_bitmap_error_ui {
	struct st_rip3_ports          *ports;
	const struct st_rip3_palettes *palettes;
	const struct st_rip3_maf_font *font;
};

/* Assign directly to st_rip3_bitmap_error_sink.show, with the above context.
 * Explicit inclusive device boxes retain their full geometry before viewport
 * clipping and place the native marker one device pixel inside. Natural sizing places the local marker at the requested anchor.
 * NULL area places it at the viewport origin. Text is CP437 with one '?' for
 * each unmappable Unicode scalar. The active palette's nearest black and
 * most contrasting entry supply background and ink.
 * Content protection permits drawing. Inactive/mismatched ports, bad bindings,
 * malformed text/font/geometry and allocation failure draw nothing. All pixels
 * are staged before COPY painting. No status escapes to the parser/session.
 * The caller retains bindings and invokes this on its exclusive session thread. */
void st_rip3_bitmap_error_show(void *context, const char *name, unsigned port,
    const struct st_rip3_bitmap_destination *area, bool natural_size);

#endif
