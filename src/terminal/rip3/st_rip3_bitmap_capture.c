/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_bitmap.h"
#include "st_rip3_ports.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static int
capture(struct st_rip3_bitmap **result,
    const struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, const struct st_rip3_palette *palette,
    bool viewport, bool active_required)
{
	struct st_rip3_port_info info;
	struct st_rip3_rect visible;
	struct st_rip3_bitmap *bitmap;
	int status;

	if (result == NULL || ports == NULL || palette == NULL
	    || (area != NULL && (area->x0 > area->x1 || area->y0 > area->y1)))
		return IO_ERROR_INVALID_ARGUMENT;
	for (unsigned i = 0; i < 256; i++)
		if (palette->rgb[i] > 0xffffff) return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_info(ports,number,&info);
	if (status != 0) return status;
	if (!info.defined) return IO_ERROR_NOT_FOUND;
	if (active_required && !info.viewport_active) return IO_ERROR_NOT_SUPPORTED;
	visible = (struct st_rip3_rect){0,0,
	    viewport ? info.viewport.x1-info.viewport.x0 : info.bounds.x1-info.bounds.x0,
	    viewport ? info.viewport.y1-info.viewport.y0 : info.bounds.y1-info.bounds.y0};
	if (area != NULL) {
		if (area->x0 > visible.x0) visible.x0 = area->x0;
		if (area->y0 > visible.y0) visible.y0 = area->y0;
		if (area->x1 < visible.x1) visible.x1 = area->x1;
		if (area->y1 < visible.y1) visible.y1 = area->y1;
	}
	if (visible.x0 > visible.x1 || visible.y0 > visible.y1) return IO_ERROR_NOT_FOUND;
	/* Out-of-physical export pixels remain a separate consumer policy. Do
	 * not silently crop the requested dimensions or allocate an enormous
	 * virtual rectangle that cannot be read from its physical source. */
	if (viewport && (visible.x1 > info.clip.x1 || visible.y1 > info.clip.y1))
		return IO_ERROR_NOT_SUPPORTED;
	bitmap = calloc(1,sizeof(*bitmap));
	if (bitmap == NULL) return IO_ERROR_OUT_OF_MEMORY;
	bitmap->bit_depth = 8; bitmap->palette_size = 256;
	for (unsigned i = 0; i < 256; i++) bitmap->palette[i] = palette->rgb[i];
	status = st_rip3_ports_capture_pixels(ports, number, &visible, viewport, false, &bitmap->pixels);
	if (status != 0) goto failed;
	/* A screen restore can include native RGB ink. Resolve it under this
	 * export's explicit palette; retain ordinary indexed pixels verbatim.
	 * One local repeated-color cache avoids rematching long literal-black runs. */
	bool matched=false;uint32_t previous=0;uint8_t previous_index=0;
	size_t count = (size_t)bitmap->pixels->width * bitmap->pixels->height;
	for (size_t i = 0; i < count; ++i) {
		uint32_t color = bitmap->pixels->primary[i];
		if (!ST_PIXEL_COLOR_IS_RGB(color))
			continue;
		if (!matched || color != previous) {
			status = st_rip3_palette_color(palette, true, color & 0xffffff, &previous_index);
			if (status != 0) goto failed;
			matched = true;
			previous = color;
		}
		bitmap->pixels->primary[i] = previous_index;
	}
	*result = bitmap;
	return 0;
failed:
	st_rip3_bitmap_destroy(bitmap);
	return status;
}

int
st_rip3_bitmap_capture(struct st_rip3_bitmap **result,
    const struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, const struct st_rip3_palette *palette)
{
	return capture(result,ports,number,area,palette,true,true);
}

int
st_rip3_bitmap_capture_port(struct st_rip3_bitmap **result,
    const struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, const struct st_rip3_palette *palette)
{
	return capture(result,ports,number,area,palette,false,false);
}

int
st_rip3_bitmap_capture_stored_viewport(struct st_rip3_bitmap **result,
    const struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, const struct st_rip3_palette *palette)
{
	return capture(result,ports,number,area,palette,true,false);
}
