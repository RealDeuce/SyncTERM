/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_bitmap_file.h"
#include "st_rip3_bitmap_stamp.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_wallpaper.h"
#include "st_pixels.h"
#include "string_compat.h"
#include "io_error.h"

#include <stdlib.h>
#include <string.h>

static char *
bitmap_name(const uint8_t *bytes, size_t length)
{
	char *name = strndup((const char *)bytes, length);
	if (name == NULL)
		return NULL;
	char *extension = strrchr(name, '.');
	const char *translated = st_rip3_bitmap_legacy_extension(extension);
	if (translated != NULL)
		memcpy(extension, translated, sizeof(".BMP"));
	return name;
}

static bool
file_failure(int status)
{
	return status != 0 && status != IO_ERROR_INVALID_ARGUMENT
	    && status != IO_ERROR_INVALID_ENCODING;
}

static int
load_icon(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const uint8_t *command,
    size_t length, bool *operation)
{
	struct st_rip3_bitmap *bitmap = NULL;
	struct st_rip3_frame frame;
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_rip3_bitmap_destination area;
	uint64_t x, y, mode, clipboard, scale, reserved;
	size_t width = environment->coordinate_size, suffix = 2+2*width, prefix = suffix+5;
	unsigned port;
	int status;

	if (length <= prefix) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&x,command+2,width,environment->base_math);
	if (status == 0) status = st_rip3_command_field(&y,command+2+width,width,environment->base_math);
	if (status == 0) status = st_rip3_command_field(&mode,command+suffix,2,environment->base_math);
	if (status == 0) status = st_rip3_command_field(&clipboard,command+suffix+2,1,environment->base_math);
	if (status == 0) status = st_rip3_command_field(&scale,command+suffix+3,1,environment->base_math);
	if (status == 0) status = st_rip3_command_field(&reserved,command+suffix+4,1,environment->base_math);
	if (status != 0) return status;
	if (x > INT32_MAX || y > INT32_MAX || mode > 4 || clipboard > 1 || scale > 1) return IO_ERROR_OUT_OF_RANGE;
	if (reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&port);
	if (status == 0) status = st_rip3_ports_info(host->ports,port,&info);
	if (status != 0 || !info.viewport_active) return status;
	if (host->resources == NULL || host->palettes == NULL || host->bitmap_errors == NULL
	    || host->bitmap_errors->show == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host,environment_number,&frame);
	if (status != 0) return status;
	area.x0 = (int64_t)(x*frame.device_width/frame.world_width);
	area.y0 = (int64_t)(y*frame.device_height/frame.world_height);
	char *name = bitmap_name(command+prefix,length-prefix);
	if (name == NULL) return IO_ERROR_OUT_OF_MEMORY;
	*operation = true;
	status = st_rip3_bitmap_load(&bitmap,host->resources,name);
	if (status != 0) {
		/* Ordinary LOAD_ICON failures never enter GET_IMAGE, regardless of
		 * the clipboard flag or assigned target. Even a failed placeholder
		 * leaves selection, pointer and port resources intact. Resource
		 * failure may prevent the UI from drawing, but still requests it. */
		if (status != IO_ERROR_INVALID_ARGUMENT && status != IO_ERROR_INVALID_ENCODING)
			host->bitmap_errors->show(host->bitmap_errors->context,name,port,NULL);
		goto done;
	}
	/* Failure handling above is independent of capture bindings. A valid
	 * image needs the complete capture context before any paint is allowed. */
	if (clipboard != 0 && (host->clipboard == NULL || host->offscreen_pixel_budget == NULL)) {
		status = IO_ERROR_NOT_SUPPORTED; goto done;
	}
	/* XY belongs to the active WORLD; image size is either native pixels or
	 * counts projected from the fixed 640x350 reference, independently of
	 * that WORLD. Positive counts round down like the geometry mapper. The
	 * supported devices are at least as large as the reference in each axis. */
	uint64_t image_width = bitmap->pixels->width, image_height = bitmap->pixels->height;
	if (scale != 0) {
		image_width = image_width*frame.device_width/640;
		image_height = image_height*frame.device_height/350;
	}
	area.x1 = area.x0+(int64_t)image_width-1;
	area.y1 = area.y0+(int64_t)image_height-1;
	status = st_rip3_palettes_current_palette(host->palettes,&palette);
	if (status == 0) {
		status = st_rip3_draw_bitmap(host->ports,bitmap,&area,&palette,
		    ST_RIP3_BITMAP_MATCH,(enum st_rip3_transfer_mode)mode,NULL);
		if (status != 0)
			host->bitmap_errors->show(host->bitmap_errors->context,name,port,NULL);
		else if (clipboard != 0) {
			/* The renderer's requested rectangle is not reduced to visible
			 * pixels. Capture owns its extra allocation unit and its separate
			 * destructive failure semantics; those do not request a placeholder. */
			const struct st_rip3_clipboard_area capture = {area.x0,area.y0,area.x1+1,area.y1+1};
			status = st_rip3_clipboard_capture(host->ports,host->clipboard,&capture,*host->offscreen_pixel_budget);
		}
	}
done:
	st_rip3_bitmap_destroy(bitmap); free(name);
	return status;
}

int
st_rip3_bitmap_load_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_port_info info;
	struct st_rip3_frame frame;
	struct st_rip3_bitmap_destination area;
	struct st_rip3_bitmap *bitmap = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_palette_info palette_info;
	uint64_t fields[4], duplicate, mode, flags, transparent, reserved;
	unsigned number, port;
	int status;

	status = st_rip3_command_environment(host,&number,&environment);
	if (status != 0) return status;
	if (command[1] == 'I') return load_icon(host,number,&environment,command,length,operation);
	size_t width = environment.coordinate_size, suffix = 2+4*width, prefix = suffix+10;
	if (length <= prefix) return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_field(fields+i,command+2+i*width,width,environment.base_math);
		if (status != 0) return status;
		if (fields[i] > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	status = st_rip3_command_field(&duplicate,command+suffix,1,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&mode,command+suffix+1,1,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&flags,command+suffix+2,2,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&transparent,command+suffix+4,2,environment.base_math);
	if (status == 0) status = st_rip3_command_field(&reserved,command+suffix+6,4,environment.base_math);
	if (status != 0) return status;
	if (mode > 4 || flags > 63 || ((flags&8) != 0 && transparent > 255)) return IO_ERROR_OUT_OF_RANGE;
	bool verbatim = fields[2] == 0 && fields[3] == 0;
	if (!verbatim && (fields[0] > fields[2] || fields[1] > fields[3])) return IO_ERROR_INVALID_FORMAT;
	if (reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	if (host->ports == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_ports_current(host->ports,&port);
	if (status == 0) status = st_rip3_ports_info(host->ports,port,&info);
	if (status != 0) return status;
	/* Drawing in a deactivated viewport is parsed but completely ignored. */
	if (!info.viewport_active) return 0;
	bool duplicating = (flags&1) != 0 && (duplicate == 0 || (duplicate <= 35 && duplicate != port));
	if (duplicating && (host->clipboard == NULL || host->offscreen_pixel_budget == NULL)) return IO_ERROR_NOT_SUPPORTED;
	if (host->resources == NULL || host->palettes == NULL
	    || host->bitmap_errors == NULL || host->bitmap_errors->show == NULL) return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_frame(host,number,&frame);
	if (status != 0) return status;
	/* Keep the complete projected rectangle for sampling and error-box sizing.
	 * Wire coordinates fit int32_t and device dimensions are at most 1024. */
	area.x0 = (int64_t)(fields[0]*frame.device_width/frame.world_width);
	area.y0 = (int64_t)(fields[1]*frame.device_height/frame.world_height);
	area.x1 = (int64_t)(fields[2]*frame.device_width/frame.world_width);
	area.y1 = (int64_t)(fields[3]*frame.device_height/frame.world_height);
	char *name = bitmap_name(command+prefix,length-prefix);
	if (name == NULL) return IO_ERROR_OUT_OF_MEMORY;
	*operation = true;
	status = st_rip3_bitmap_load(&bitmap,host->resources,name);
	if (status != 0) {
		/* Invalid names have not passed the UTF-8 namespace contract. */
		if (file_failure(status))
			host->bitmap_errors->show(host->bitmap_errors->context,name,port,verbatim ? NULL : &area);
		goto done;
	}
	bool adopt = (flags&2) != 0 && bitmap->palette_size != 0;
	if (verbatim) {
		area.x1 = area.x0+bitmap->pixels->width-1;
		area.y1 = area.y0+bitmap->pixels->height-1;
	}
	status = st_rip3_palettes_current(host->palettes,&number);
	if (status == 0) status = st_rip3_palettes_current_palette(host->palettes,&palette);
	uint8_t transparent_color = (uint8_t)transparent;
	/* This engine's initialized 256-entry device has the normal 236 direct
	 * index limit. ENV RGB precision is an input format, not device depth.
	 * Translate before any image-palette adoption changes the drawing table. */
	if (status == 0 && (flags&8) != 0)
		status = st_rip3_bitmap_translate_key(&palette,236,(uint32_t)transparent,&transparent_color);
	if (status == 0 && adopt) {
		status = st_rip3_palettes_info(host->palettes,number,&palette_info);
		if (status == 0 && palette_info.protected) status = IO_ERROR_PERMISSION_DENIED;
		if (status == 0) status = st_rip3_palette_rgb(&palette,0,8,bitmap->palette,bitmap->palette_size);
	}
	enum st_rip3_bitmap_colors colors = adopt ? ST_RIP3_BITMAP_ADOPTED
	    : (flags&4) != 0 ? ST_RIP3_BITMAP_DITHER : ST_RIP3_BITMAP_MATCH;
	if (status == 0) status = st_rip3_draw_bitmap(host->ports,bitmap,&area,&palette,colors,
	    (enum st_rip3_transfer_mode)mode,(flags&8) != 0 ? &transparent_color : NULL);
	if (status != 0) {
		if (file_failure(status)) host->bitmap_errors->show(host->bitmap_errors->context,name,port,verbatim ? NULL : &area);
		goto done;
	}
	/* No callbacks run between palette preflight, staging/painting and this
	 * allocation-free commit. One owner retains the current slot/protection,
	 * and the exact RGB span was already validated. Failed staging therefore
	 * changes neither the palette nor the pixels. Unspecified entries remain. */
	if (status == 0 && adopt)
		status = st_rip3_palettes_rgb(host->palettes,0,8,bitmap->palette,bitmap->palette_size);
	if (status != 0) goto done;
	if (duplicating) {
		const struct st_rip3_clipboard_area capture = {area.x0,area.y0,area.x1+1,area.y1+1};
		status = st_rip3_clipboard_duplicate(host->ports,host->clipboard,(unsigned)duplicate,&capture,*host->offscreen_pixel_budget);
		/* Only explicit deletion rejection exits before wallpaper. Allocation
		 * failure and the clipboard route retain their separate state effects. */
		if (duplicate != 0 && status == IO_ERROR_PERMISSION_DENIED) goto done;
	}
	if ((flags&16) != 0) {
		/* Shared clipboard capture may have changed the live current port. */
		int wallpaper_status = st_rip3_wallpaper(host->ports,&area,(flags&32) != 0,&palette);
		if (status == 0) status = wallpaper_status;
	}
done:
	st_rip3_bitmap_destroy(bitmap); free(name);
	return status;
}
