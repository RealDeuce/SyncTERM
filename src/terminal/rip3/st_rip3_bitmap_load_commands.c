/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "io_error.h"
#include "st_pixels.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_wallpaper.h"
#include "string_compat.h"

static char *
bitmap_name(const uint8_t *bytes, size_t length)
{
	char *name = strndup((const char *)bytes, length);
	if (name == NULL)
		return NULL;
	char       *extension  = strrchr(name, '.');
	const char *translated = st_rip3_bitmap_legacy_extension(extension);
	if (translated != NULL)
		memcpy(extension, translated, sizeof(".BMP"));
	return name;
}

static bool
file_failure(int status)
{
	return status != 0 && status != IO_ERROR_INVALID_ARGUMENT && status != IO_ERROR_INVALID_ENCODING;
}

static int
load_icon(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_bitmap            *bitmap = NULL;
	struct st_rip3_frame              frame;
	struct st_rip3_port_info          info;
	struct st_rip3_palette            palette;
	struct st_rip3_bitmap_destination area;
	uint64_t                          x, y, mode, clipboard, scale;
	struct st_rip3_fields             fields = {command + 2, length - 2};
	size_t                            width  = environment->coordinate_size;
	unsigned                          radix  = environment->base_math, port;
	int                               status = st_rip3_command_next_field(&fields, &x, width, radix, false);

	if (status == 0)
		status = st_rip3_command_next_field(&fields, &y, width, radix, false);
	/* The inherited two-byte mode spelling contains two separate fields.
	 * Only the second field determines the bitmap raster operation. */
	if (status == 0)
		status = st_rip3_command_next_field(&fields, NULL, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &clipboard, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &scale, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, NULL, 1, radix, false);
	if (status != 0)
		return status;
	if (fields.remaining == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (x > INT32_MAX || y > INT32_MAX || mode > 4 || clipboard > 1 || scale > 1)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports, &port);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, port, &info);
	if (status != 0 || !info.viewport_active)
		return status;
	if (host->resources == NULL || host->palettes == NULL || host->bitmap_errors == NULL
	    || host->bitmap_errors->show == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	area = (struct st_rip3_bitmap_destination) {
	    .x0 = (int64_t)(x * frame.device_width / frame.world_width),
	    .y0 = (int64_t)(y * frame.device_height / frame.world_height),
	};
	char *name = bitmap_name(fields.bytes, fields.remaining);
	if (name == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*operation = true;
	status     = st_rip3_bitmap_load(&bitmap, host->resources, name);
	if (status != 0) {
		/* Ordinary LOAD_ICON failures never enter GET_IMAGE, regardless of
		 * the clipboard flag or assigned target. Even a failed placeholder
		 * leaves selection, pointer and port resources intact. Resource
		 * failure may prevent the UI from drawing, but still requests it. */
		if (status != IO_ERROR_INVALID_ARGUMENT && status != IO_ERROR_INVALID_ENCODING)
			host->bitmap_errors->show(host->bitmap_errors->context, name, port, &area, true);
		goto done;
	}
	/* Failure handling above is independent of capture bindings. A valid
	 * image needs the complete capture context before any paint is allowed. */
	if (clipboard != 0 && (host->clipboard == NULL || host->offscreen_pixel_budget == NULL)) {
		status = IO_ERROR_NOT_SUPPORTED;
		goto done;
	}
	/* XY belongs to the active WORLD; image size is either native pixels or
	 * counts projected from the fixed 640x350 reference, independently of
	 * that WORLD. Positive counts round down like the geometry mapper. The
	 * supported devices are at least as large as the reference in each axis. */
	uint64_t image_width = bitmap->pixels->width, image_height = bitmap->pixels->height;
	if (scale != 0) {
		image_width  = image_width * frame.device_width / 640;
		image_height = image_height * frame.device_height / 350;
	}
	area.x1 = area.x0 + (int64_t)image_width - 1;
	area.y1 = area.y0 + (int64_t)image_height - 1;
	status  = st_rip3_palettes_current_palette(host->palettes, &palette);
	if (status == 0) {
		status = st_rip3_draw_bitmap(host->ports, bitmap, &area, &palette, ST_RIP3_BITMAP_MATCH,
		    (enum st_rip3_transfer_mode)mode, NULL);
		if (status != 0)
			host->bitmap_errors->show(host->bitmap_errors->context, name, port, &area, true);
		else if (clipboard != 0) {
			/* The renderer's requested rectangle is not reduced to visible
			 * pixels. Capture owns its extra allocation unit and its separate
			 * destructive failure semantics; those do not request a placeholder. */
			const struct st_rip3_clipboard_area capture = {area.x0, area.y0, area.x1 + 1, area.y1 + 1};
			status = st_rip3_clipboard_capture(host->ports, host->clipboard, &capture,
			    *host->offscreen_pixel_budget);
		}
	}
done:
	st_rip3_bitmap_destroy(bitmap);
	free(name);
	return status;
}

int
st_rip3_bitmap_load_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command,
    size_t length, bool *operation)
{
	struct st_rip3_environment        environment;
	struct st_rip3_port_info          info;
	struct st_rip3_frame              frame;
	struct st_rip3_bitmap_destination area;
	struct st_rip3_bitmap            *bitmap = NULL;
	struct st_rip3_palette            palette;
	struct st_rip3_palette_info       palette_info;
	uint64_t                          positions[4], duplicate, mode, flags, transparent;
	unsigned                          number, port;
	int                               status;

	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	if (command[1] == 'I')
		return load_icon(host, number, &environment, command, length, operation);
	struct st_rip3_fields fields = {command + 2, length - 2};
	size_t                width  = environment.coordinate_size;
	unsigned              radix  = environment.base_math;
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_next_field(&fields, &positions[i], width, radix, false);
		if (status != 0)
			return status;
		if (positions[i] > INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
	}
	status = st_rip3_command_next_field(&fields, &duplicate, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &flags, 2, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &transparent, 2, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, NULL, 4, radix, false);
	if (status != 0)
		return status;
	if (fields.remaining == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (mode > 4 || flags > 63 || ((flags & 8) != 0 && transparent > 255))
		return IO_ERROR_OUT_OF_RANGE;
	if (positions[2] != 0 || positions[3] != 0) {
		for (size_t axis = 0; axis < 2; axis++) {
			if (positions[axis] > positions[axis + 2]) {
				uint64_t far        = positions[axis];
				positions[axis]     = positions[axis + 2];
				positions[axis + 2] = far;
			}
		}
	}
	if (host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports, &port);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, port, &info);
	if (status != 0)
		return status;
	/* Drawing in a deactivated viewport is parsed but completely ignored. */
	if (!info.viewport_active)
		return 0;
	bool duplicating = (flags & 1) != 0 && (duplicate == 0 || (duplicate <= 35 && duplicate != port));
	if (duplicating && (host->clipboard == NULL || host->offscreen_pixel_budget == NULL))
		return IO_ERROR_NOT_SUPPORTED;
	if (host->resources == NULL || host->palettes == NULL || host->bitmap_errors == NULL
	    || host->bitmap_errors->show == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host, number, &frame);
	if (status != 0)
		return status;
	/* Keep the complete projected rectangle for sampling and error-box sizing.
	 * Wire coordinates fit int32_t and device dimensions are at most 1024. */
	area.x0          = (int64_t)(positions[0] * frame.device_width / frame.world_width);
	area.y0          = (int64_t)(positions[1] * frame.device_height / frame.world_height);
	area.x1          = (int64_t)(positions[2] * frame.device_width / frame.world_width);
	area.y1          = (int64_t)(positions[3] * frame.device_height / frame.world_height);
	bool native_size = area.x1 == 0 && area.y1 == 0;
	/* The error marker uses the wire rectangle before the renderer fills in
	 * either missing far coordinate. Its sink accepts inclusive bounds. */
	const struct st_rip3_bitmap_destination requested = {area.x0, area.y0, area.x1 - 1, area.y1 - 1};
	bool  natural_marker = requested.x1 < requested.x0 || requested.y1 < requested.y0;
	char *name           = bitmap_name(fields.bytes, fields.remaining);
	if (name == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	*operation = true;
	status     = st_rip3_bitmap_load(&bitmap, host->resources, name);
	if (status != 0) {
		/* Invalid names have not passed the UTF-8 namespace contract. */
		if (file_failure(status))
			host->bitmap_errors->show(host->bitmap_errors->context, name, port, &requested,
			    natural_marker);
		goto done;
	}
	bool adopt = (flags & 2) != 0 && bitmap->palette_size != 0;
	if (native_size) {
		area.x1 = area.x0 + bitmap->pixels->width;
		area.y1 = area.y0 + bitmap->pixels->height;
	}
	else {
		/* Only a zero absolute far edge requests that axis's projected
		 * image dimension. Raster and duplicate operations take local coordinates. */
		if (area.x1 + info.bounds.x0 + info.viewport.x0 == 0)
			area.x1 =
			    area.x0 + (int64_t)bitmap->pixels->width * frame.device_width / frame.world_width;
		if (area.y1 + info.bounds.y0 + info.viewport.y0 == 0)
			area.y1 =
			    area.y0 + (int64_t)bitmap->pixels->height * frame.device_height / frame.world_height;
	}
	bool empty = area.x0 == area.x1 || area.y0 == area.y1;
	/* The renderer returns exclusive edges. The stamp and wallpaper helpers
	 * use inclusive bounds, while the duplication helper uses half-open bounds. */
	const struct st_rip3_clipboard_area capture = {area.x0, area.y0, area.x1, area.y1};
	area.x1--;
	area.y1--;
	status = st_rip3_palettes_current(host->palettes, &number);
	if (status == 0)
		status = st_rip3_palettes_current_palette(host->palettes, &palette);
	uint8_t transparent_color = (uint8_t)transparent;
	/* This engine's initialized 256-entry device has the normal 236 direct
	 * index limit. ENV RGB precision is an input format, not device depth.
	 * Translate before any image-palette adoption changes the drawing table. */
	if (status == 0 && (flags & 8) != 0)
		status = st_rip3_bitmap_translate_key(&palette, 236, (uint32_t)transparent, &transparent_color);
	if (status == 0 && adopt) {
		status = st_rip3_palettes_info(host->palettes, number, &palette_info);
		if (status == 0 && palette_info.protected)
			status = IO_ERROR_PERMISSION_DENIED;
		if (status == 0)
			status = st_rip3_palette_rgb(&palette, 0, 8, bitmap->palette, bitmap->palette_size);
	}
	enum st_rip3_bitmap_colors colors = adopt              ? ST_RIP3_BITMAP_ADOPTED
	                                    : (flags & 4) != 0 ? ST_RIP3_BITMAP_DITHER
	                                                       : ST_RIP3_BITMAP_MATCH;
	if (status == 0) {
		const struct st_rip3_palette *published = st_rip3_palettes_published(host->palettes);
		struct st_rip3_palette        staged;
		if (adopt) {
			/* Mixed RGB/index operations see the colors this stamp publishes.
			 * Entries outside the file palette keep their published values. */
			staged = *published;
			memcpy(staged.rgb, palette.rgb, bitmap->palette_size * sizeof(staged.rgb[0]));
			st_rip3_ports_bind_drawing_palette(host->ports, &staged);
		}
		status = st_rip3_draw_bitmap(host->ports, bitmap, &area, &palette, colors,
		    (enum st_rip3_transfer_mode)mode, (flags & 8) != 0 ? &transparent_color : NULL);
		if (adopt)
			st_rip3_ports_bind_drawing_palette(host->ports, published);
	}
	if (status != 0) {
		if (file_failure(status))
			host->bitmap_errors->show(host->bitmap_errors->context, name, port, &requested,
			    natural_marker);
		goto done;
	}
	/* No callbacks run between palette preflight, staging/painting and this
	 * allocation-free commit. One owner retains the current slot/protection,
	 * and the exact RGB span was already validated. Failed staging therefore
	 * changes neither the palette nor the pixels. Unspecified entries remain. */
	if (status == 0 && adopt)
		status = st_rip3_palettes_rgb(host->palettes, 0, 8, bitmap->palette, bitmap->palette_size);
	if (status != 0)
		goto done;
	if (duplicating) {
		status = st_rip3_clipboard_duplicate(host->ports, host->clipboard, (unsigned)duplicate, &capture,
		    *host->offscreen_pixel_budget);
		/* Only explicit deletion rejection exits before wallpaper. Allocation
		 * failure and the clipboard route retain their separate state effects. */
		if (duplicate != 0 && status == IO_ERROR_PERMISSION_DENIED)
			goto done;
	}
	if ((flags & 16) != 0 && !empty) {
		/* Shared clipboard capture may have changed the live current port. */
		int wallpaper_status = st_rip3_wallpaper(host->ports, &area, (flags & 32) != 0, &palette);
		if (status == 0)
			status = wallpaper_status;
	}
done:
	st_rip3_bitmap_destroy(bitmap);
	free(name);
	return status;
}
