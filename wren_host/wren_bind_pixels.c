/* Wren bindings for native pixel buffers, masks, and integer blits. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "wren_host_internal.h"

#include "file_map.h"
#include "st_console.h"
#include "st_pixel_image.h"
#include "st_pixels.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WREN_MAX_EXACT_INTEGER 9007199254740991.0
#define PIXEL_BUFFER_MAGIC UINT32_C(0x50425546)
#define PIXEL_MASK_MAGIC UINT32_C(0x504d4153)
#define PIXEL_BLIT_MAGIC UINT32_C(0x50424c54)

struct wren_pixel_buffer {
	uint32_t magic;
	struct st_pixel_buffer *pixels;
};

struct wren_pixel_mask {
	uint32_t magic;
	struct st_pixel_mask *mask;
};

struct wren_pixel_blit {
	uint32_t magic;
	struct st_pixel_blit blit;
};

struct pixel_binding {
	const char *class_name;
	bool is_static;
	const char *signature;
	WrenForeignMethodFn method;
};

static bool
slot_integer(WrenVM *vm, int slot, double minimum, double maximum,
	int64_t *result, const char *message)
{
	double value;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_NUM) {
		st_wren_abort(vm, message);
		return false;
	}
	value = wrenGetSlotDouble(vm, slot);
	if (!isfinite(value) || trunc(value) != value || value < minimum
	    || value > maximum) {
		st_wren_abort(vm, message);
		return false;
	}
	*result = (int64_t)value;
	return true;
}

static bool
slot_uint32(WrenVM *vm, int slot, uint32_t *result, const char *message)
{
	int64_t value;

	if (!slot_integer(vm, slot, 0, UINT32_MAX, &value, message))
		return false;
	*result = (uint32_t)value;
	return true;
}

static bool
slot_int32(WrenVM *vm, int slot, int32_t *result, const char *message)
{
	int64_t value;

	if (!slot_integer(vm, slot, INT32_MIN, INT32_MAX, &value, message))
		return false;
	*result = (int32_t)value;
	return true;
}

static bool
slot_bool(WrenVM *vm, int slot, bool *result, const char *message)
{
	if (wrenGetSlotType(vm, slot) != WREN_TYPE_BOOL) {
		st_wren_abort(vm, message);
		return false;
	}
	*result = wrenGetSlotBool(vm, slot);
	return true;
}

static bool
dimensions(WrenVM *vm, int width_slot, int height_slot, uint32_t *width,
	uint32_t *height, size_t element_size, size_t *count,
	const char *message)
{
	if (!slot_uint32(vm, width_slot, width, message)
	    || !slot_uint32(vm, height_slot, height, message))
		return false;
	if (*width != 0 && (size_t)*height > SIZE_MAX / (size_t)*width) {
		st_wren_abort(vm, message);
		return false;
	}
	*count = (size_t)*width * *height;
	if (*count > SIZE_MAX / element_size) {
		st_wren_abort(vm, message);
		return false;
	}
	return true;
}

static bool
slot_colour(WrenVM *vm, int slot, uint32_t *colour)
{
	uint32_t value;

	if (!slot_uint32(vm, slot, &value,
	    "pixel colour must be a native PixelColor value"))
		return false;
	if (ST_PIXEL_COLOR_IS_RGB(value)) {
		if ((value & UINT32_C(0x7f000000)) != 0) {
			st_wren_abort(vm,
			    "RGB pixel colour contains unsupported flag bits");
			return false;
		}
	}
	else if (value > UINT16_MAX) {
		st_wren_abort(vm, "palette pixel colour must be in 0..65535");
		return false;
	}
	*colour = value;
	return true;
}

static bool
colour_to_rgb(WrenVM *vm, uint32_t colour, uint32_t *rgb)
{
	if (ST_PIXEL_COLOR_IS_RGB(colour)) {
		*rgb = colour & UINT32_C(0x00ffffff);
		return true;
	}
	return st_console_get_palette(st_wren_context_from_vm(vm)->console,
	    (uint16_t)colour, rgb, 1) == 0;
}

static size_t
buffer_count(const struct st_pixel_buffer *pixels)
{
	return (size_t)pixels->width * pixels->height;
}

static size_t
mask_count(const struct st_pixel_mask *mask)
{
	return (size_t)mask->width * mask->height;
}

static struct wren_pixel_buffer *
slot_pixel_buffer(WrenVM *vm, int slot, const char *message)
{
	struct wren_pixel_buffer *buffer;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_FOREIGN) {
		st_wren_abort(vm, message);
		return NULL;
	}
	buffer = wrenGetSlotForeign(vm, slot);
	if (buffer == NULL || buffer->magic != PIXEL_BUFFER_MAGIC
	    || buffer->pixels == NULL) {
		st_wren_abort(vm, message);
		return NULL;
	}
	return buffer;
}

static struct wren_pixel_mask *
slot_pixel_mask(WrenVM *vm, int slot, const char *message)
{
	struct wren_pixel_mask *mask;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_FOREIGN) {
		st_wren_abort(vm, message);
		return NULL;
	}
	mask = wrenGetSlotForeign(vm, slot);
	if (mask == NULL || mask->magic != PIXEL_MASK_MAGIC || mask->mask == NULL) {
		st_wren_abort(vm, message);
		return NULL;
	}
	return mask;
}

static struct wren_pixel_blit *
slot_pixel_blit(WrenVM *vm, int slot, const char *message)
{
	struct wren_pixel_blit *blit;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_FOREIGN) {
		st_wren_abort(vm, message);
		return NULL;
	}
	blit = wrenGetSlotForeign(vm, slot);
	if (blit == NULL || blit->magic != PIXEL_BLIT_MAGIC) {
		st_wren_abort(vm, message);
		return NULL;
	}
	return blit;
}

static struct wren_pixel_buffer *
return_pixel_buffer(WrenVM *vm, struct st_pixel_buffer *pixels)
{
	struct wren_pixel_buffer *buffer;

	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "PixelBuffer", 1);
	buffer = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*buffer));
	buffer->magic = PIXEL_BUFFER_MAGIC;
	buffer->pixels = pixels;
	return buffer;
}

static struct wren_pixel_mask *
return_pixel_mask(WrenVM *vm, struct st_pixel_mask *mask)
{
	struct wren_pixel_mask *wrapper;

	wrenEnsureSlots(vm, 2);
	wrenGetVariable(vm, "syncterm", "PixelMask", 1);
	wrapper = wrenSetSlotNewForeign(vm, 0, 1, sizeof(*wrapper));
	wrapper->magic = PIXEL_MASK_MAGIC;
	wrapper->mask = mask;
	return wrapper;
}

static void
fn_pixel_colour_rgb(WrenVM *vm)
{
	uint32_t rgb;

	if (!slot_uint32(vm, 1, &rgb, "PixelColor.rgb: expected 0xRRGGBB"))
		return;
	if (rgb > UINT32_C(0x00ffffff)) {
		st_wren_abort(vm, "PixelColor.rgb: value must be in 0..0xFFFFFF");
		return;
	}
	wrenSetSlotDouble(vm, 0, (double)(rgb | ST_PIXEL_COLOR_RGB_TAG));
}

static void
fn_pixel_colour_palette(WrenVM *vm)
{
	int64_t index;

	if (slot_integer(vm, 1, 0, UINT16_MAX, &index,
	    "PixelColor.palette: index must be in 0..65535"))
		wrenSetSlotDouble(vm, 0, (double)index);
}

static void
fn_pixel_colour_is_rgb(WrenVM *vm)
{
	uint32_t colour;

	if (slot_colour(vm, 1, &colour))
		wrenSetSlotBool(vm, 0, ST_PIXEL_COLOR_IS_RGB(colour));
}

static void
fn_pixel_colour_rgb_value(WrenVM *vm)
{
	uint32_t colour;

	if (!slot_colour(vm, 1, &colour))
		return;
	if (!ST_PIXEL_COLOR_IS_RGB(colour))
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotDouble(vm, 0,
		    (double)(colour & UINT32_C(0x00ffffff)));
}

static void
fn_pixel_colour_palette_index(WrenVM *vm)
{
	uint32_t colour;

	if (!slot_colour(vm, 1, &colour))
		return;
	if (ST_PIXEL_COLOR_IS_RGB(colour))
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotDouble(vm, 0, (double)colour);
}

static void
fn_pixel_colour_to_rgb(WrenVM *vm)
{
	uint32_t colour;
	uint32_t rgb;

	if (!slot_colour(vm, 1, &colour))
		return;
	if (colour_to_rgb(vm, colour, &rgb))
		wrenSetSlotDouble(vm, 0, (double)rgb);
	else
		wrenSetSlotNull(vm, 0);
}

static void
pixel_buffer_allocate(WrenVM *vm)
{
	struct wren_pixel_buffer *wrapper = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*wrapper));
	uint32_t width;
	uint32_t height;
	uint32_t fill = 0;
	size_t count;

	wrapper->magic = PIXEL_BUFFER_MAGIC;
	wrapper->pixels = NULL;
	if (!dimensions(vm, 1, 2, &width, &height, sizeof(uint32_t), &count,
	    "PixelBuffer.new: invalid dimensions"))
		return;
	if (wrenGetSlotCount(vm) > 3 && !slot_colour(vm, 3, &fill))
		return;
	if (st_pixel_buffer_create(&wrapper->pixels, width, height, false) != 0) {
		st_wren_abort(vm, "PixelBuffer.new: out of memory");
		return;
	}
	for (size_t i = 0; i < count; ++i)
		wrapper->pixels->primary[i] = fill;
}

static void
pixel_buffer_finalize(void *data)
{
	struct wren_pixel_buffer *wrapper = data;

	st_pixel_buffer_destroy(wrapper->pixels);
	wrapper->pixels = NULL;
}

static struct st_pixel_buffer *
pixel_buffer(WrenVM *vm)
{
	struct wren_pixel_buffer *wrapper = slot_pixel_buffer(vm, 0,
	    "invalid PixelBuffer receiver");

	return wrapper == NULL ? NULL : wrapper->pixels;
}

static bool
buffer_index(WrenVM *vm, const struct st_pixel_buffer *pixels, int slot,
	size_t *index)
{
	int64_t value;
	size_t count = buffer_count(pixels);

	if (!slot_integer(vm, slot, 0, WREN_MAX_EXACT_INTEGER, &value,
	    "PixelBuffer: index must be a non-negative integer"))
		return false;
	if ((uint64_t)value >= count) {
		st_wren_abort(vm, "PixelBuffer: index out of bounds");
		return false;
	}
	*index = (size_t)value;
	return true;
}

static bool
buffer_position(WrenVM *vm, const struct st_pixel_buffer *pixels,
	int x_slot, int y_slot, size_t *index)
{
	uint32_t x;
	uint32_t y;

	if (!slot_uint32(vm, x_slot, &x,
	    "PixelBuffer: x must be a non-negative integer")
	    || !slot_uint32(vm, y_slot, &y,
	    "PixelBuffer: y must be a non-negative integer"))
		return false;
	if (x >= pixels->width || y >= pixels->height) {
		st_wren_abort(vm, "PixelBuffer: coordinates out of bounds");
		return false;
	}
	*index = (size_t)y * pixels->width + x;
	return true;
}

static void
fn_pixel_buffer_count(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels != NULL)
		wrenSetSlotDouble(vm, 0, (double)buffer_count(pixels));
}

static void
fn_pixel_buffer_width(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels != NULL)
		wrenSetSlotDouble(vm, 0, pixels->width);
}

static void
fn_pixel_buffer_height(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels != NULL)
		wrenSetSlotDouble(vm, 0, pixels->height);
}

static void
fn_pixel_buffer_subscript(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	size_t index;

	if (pixels != NULL && buffer_index(vm, pixels, 1, &index))
		wrenSetSlotDouble(vm, 0, pixels->primary[index]);
}

static void
fn_pixel_buffer_subscript_set(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	size_t index;
	uint32_t colour;

	if (pixels != NULL && buffer_index(vm, pixels, 1, &index)
	    && slot_colour(vm, 2, &colour))
		pixels->primary[index] = colour;
}

static void
fn_pixel_buffer_pixel_at(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	size_t index;

	if (pixels != NULL && buffer_position(vm, pixels, 1, 2, &index))
		wrenSetSlotDouble(vm, 0, pixels->primary[index]);
}

static void
fn_pixel_buffer_set_pixel(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	size_t index;
	uint32_t colour;

	if (pixels != NULL && buffer_position(vm, pixels, 1, 2, &index)
	    && slot_colour(vm, 3, &colour))
		pixels->primary[index] = colour;
}

static void
fn_pixel_buffer_fill(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	uint32_t colour;

	if (pixels != NULL && slot_colour(vm, 1, &colour))
		for (size_t i = 0; i < buffer_count(pixels); ++i)
			pixels->primary[i] = colour;
}

static void
fn_pixel_buffer_iterate(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	int64_t current;
	size_t count;

	if (pixels == NULL)
		return;
	count = buffer_count(pixels);
	if (count == 0) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (wrenGetSlotType(vm, 1) == WREN_TYPE_NULL) {
		wrenSetSlotDouble(vm, 0, 0);
		return;
	}
	if (!slot_integer(vm, 1, 0, WREN_MAX_EXACT_INTEGER, &current,
	    "PixelBuffer.iterate: invalid iterator"))
		return;
	if ((uint64_t)current + 1 >= count)
		wrenSetSlotBool(vm, 0, false);
	else
		wrenSetSlotDouble(vm, 0, (double)(current + 1));
}

static void
fn_pixel_buffer_iterator_value(WrenVM *vm)
{
	fn_pixel_buffer_subscript(vm);
}

static void
fn_pixel_buffer_clone(WrenVM *vm)
{
	struct st_pixel_buffer *source = pixel_buffer(vm);
	struct st_pixel_buffer *copy = NULL;

	if (source == NULL)
		return;
	if (st_pixel_buffer_clone(&copy, source) != 0)
		wrenSetSlotNull(vm, 0);
	else
		(void)return_pixel_buffer(vm, copy);
}

static void
fn_pixel_buffer_has_alternate(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels != NULL)
		wrenSetSlotBool(vm, 0, pixels->alternate != NULL);
}

static void
fn_pixel_buffer_alternate_at(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	size_t index;

	if (pixels == NULL || !buffer_index(vm, pixels, 1, &index))
		return;
	if (pixels->alternate == NULL)
		wrenSetSlotNull(vm, 0);
	else
		wrenSetSlotDouble(vm, 0, pixels->alternate[index]);
}

static bool
ensure_alternate(WrenVM *vm, struct st_pixel_buffer *pixels)
{
	if (st_pixel_buffer_ensure_alternate(pixels) == 0)
		return true;
	st_wren_abort(vm, "PixelBuffer: out of memory creating alternate plane");
	return false;
}

static void
fn_pixel_buffer_set_alternate(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	size_t index;
	uint32_t colour;

	if (pixels != NULL && buffer_position(vm, pixels, 1, 2, &index)
	    && slot_colour(vm, 3, &colour) && ensure_alternate(vm, pixels))
		pixels->alternate[index] = colour;
}

static void
fn_pixel_buffer_fill_alternate(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	uint32_t colour;

	if (pixels != NULL && slot_colour(vm, 1, &colour)
	    && ensure_alternate(vm, pixels))
		for (size_t i = 0; i < buffer_count(pixels); ++i)
			pixels->alternate[i] = colour;
}

static void
fn_pixel_buffer_clear_alternate(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels != NULL)
		st_pixel_buffer_remove_alternate(pixels);
}

static bool
rgb24_data(WrenVM *vm, int slot, size_t count, const uint8_t **bytes,
	const char *message)
{
	int length;

	if (wrenGetSlotType(vm, slot) != WREN_TYPE_STRING) {
		st_wren_abort(vm, message);
		return false;
	}
	*bytes = (const uint8_t *)wrenGetSlotBytes(vm, slot, &length);
	if (count > INT_MAX / 3 || length != (int)(count * 3)) {
		st_wren_abort(vm, message);
		return false;
	}
	return true;
}

static void
import_rgb24(uint32_t *destination, const uint8_t *bytes, size_t count)
{
	for (size_t i = 0; i < count; ++i)
		destination[i] = ST_PIXEL_COLOR_RGB_VALUE(bytes[i * 3],
		    bytes[i * 3 + 1], bytes[i * 3 + 2]);
}

static bool
export_rgb24(WrenVM *vm, const uint32_t *source, size_t count)
{
	uint8_t *bytes;

	if (count > INT_MAX / 3) {
		wrenSetSlotNull(vm, 0);
		return false;
	}
	if (count == 0) {
		wrenSetSlotBytes(vm, 0, "", 0);
		return true;
	}
	bytes = malloc(count * 3);
	if (bytes == NULL) {
		wrenSetSlotNull(vm, 0);
		return false;
	}
	for (size_t i = 0; i < count; ++i) {
		uint32_t rgb;

		if (!colour_to_rgb(vm, source[i], &rgb)) {
			free(bytes);
			wrenSetSlotNull(vm, 0);
			return false;
		}
		bytes[i * 3] = (uint8_t)(rgb >> 16);
		bytes[i * 3 + 1] = (uint8_t)(rgb >> 8);
		bytes[i * 3 + 2] = (uint8_t)rgb;
	}
	wrenSetSlotBytes(vm, 0, (const char *)bytes, count * 3);
	free(bytes);
	return true;
}

static void
fn_pixel_buffer_from_rgb24(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = NULL;
	const uint8_t *primary;
	const uint8_t *alternate = NULL;
	uint32_t width;
	uint32_t height;
	size_t count;
	bool has_alternate = wrenGetSlotCount(vm) > 4;

	if (!dimensions(vm, 1, 2, &width, &height, sizeof(uint32_t), &count,
	    "PixelBuffer.fromRgb24: invalid dimensions")
	    || !rgb24_data(vm, 3, count, &primary,
	    "PixelBuffer.fromRgb24: byte length must be width * height * 3")
	    || (has_alternate && !rgb24_data(vm, 4, count, &alternate,
	    "PixelBuffer.fromRgb24: alternate byte length must be width * height * 3")))
		return;
	if (st_pixel_buffer_create(&pixels, width, height, has_alternate) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	import_rgb24(pixels->primary, primary, count);
	if (has_alternate)
		import_rgb24(pixels->alternate, alternate, count);
	(void)return_pixel_buffer(vm, pixels);
}

static void
fn_pixel_buffer_rgb24(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels != NULL)
		(void)export_rgb24(vm, pixels->primary, buffer_count(pixels));
}

static void
fn_pixel_buffer_alternate_rgb24(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);

	if (pixels == NULL)
		return;
	if (pixels->alternate == NULL)
		wrenSetSlotNull(vm, 0);
	else
		(void)export_rgb24(vm, pixels->alternate, buffer_count(pixels));
}

static void
fn_pixel_buffer_set_alternate_rgb24(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = pixel_buffer(vm);
	const uint8_t *bytes;
	size_t count;

	if (pixels == NULL)
		return;
	count = buffer_count(pixels);
	if (rgb24_data(vm, 1, count, &bytes,
	    "setAlternateRgb24: byte length must be width * height * 3")
	    && ensure_alternate(vm, pixels))
		import_rgb24(pixels->alternate, bytes, count);
}

static int
decode_source(WrenVM *vm, int slot, bool jxl,
	struct st_pixel_buffer **pixel_result, struct st_pixel_mask **mask_result,
	bool *aborted)
{
	const void *data;
	size_t length;
	file_map_t *map = NULL;
	int status;

	*aborted = false;

	if (wrenGetSlotType(vm, slot) == WREN_TYPE_STRING) {
		int string_length;

		data = wrenGetSlotBytes(vm, slot, &string_length);
		length = (size_t)string_length;
	}
	else {
		const char *path = st_wren_file_read_path(vm, slot);

		if (path == NULL) {
			st_wren_abort(vm,
			    "image source must be binary String data or a readable File");
			*aborted = true;
			return IO_ERROR_INVALID_ARGUMENT;
		}
		status = file_map_open(&map, path);
		if (status != 0)
			return status;
		data = file_map_data(map);
		length = file_map_size(map);
	}
	if (mask_result != NULL)
		status = st_pixel_image_decode_pbm(mask_result, data, length);
	else if (jxl)
		status = st_pixel_image_decode_jxl(pixel_result, data, length);
	else
		status = st_pixel_image_decode_ppm(pixel_result, data, length);
	(void)file_map_close(map);
	return status;
}

static void
fn_pixel_buffer_from_ppm(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = NULL;
	bool aborted;

	if (decode_source(vm, 1, false, &pixels, NULL, &aborted) == 0)
		(void)return_pixel_buffer(vm, pixels);
	else if (!aborted)
		wrenSetSlotNull(vm, 0);
}

static void
fn_pixel_buffer_from_jxl(WrenVM *vm)
{
	struct st_pixel_buffer *pixels = NULL;
	bool aborted;

	if (!st_pixel_image_jxl_supported()) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (decode_source(vm, 1, true, &pixels, NULL, &aborted) == 0)
		(void)return_pixel_buffer(vm, pixels);
	else if (!aborted)
		wrenSetSlotNull(vm, 0);
}

static void
fn_pixel_buffer_jxl_supported(WrenVM *vm)
{
	wrenSetSlotBool(vm, 0, st_pixel_image_jxl_supported());
}

static void
pixel_mask_allocate(WrenVM *vm)
{
	struct wren_pixel_mask *wrapper = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*wrapper));
	uint32_t width;
	uint32_t height;
	size_t count;
	bool fill = false;

	wrapper->magic = PIXEL_MASK_MAGIC;
	wrapper->mask = NULL;
	if (!dimensions(vm, 1, 2, &width, &height, 1, &count,
	    "PixelMask.new: invalid dimensions"))
		return;
	if (wrenGetSlotCount(vm) > 3 && !slot_bool(vm, 3, &fill,
	    "PixelMask.new: fill must be Bool"))
		return;
	if (st_pixel_mask_create(&wrapper->mask, width, height) != 0) {
		st_wren_abort(vm, "PixelMask.new: out of memory");
		return;
	}
	if (fill && count != 0) {
		memset(wrapper->mask->bits, UINT8_MAX, (count + 7) / 8);
		if ((count % 8) != 0)
			wrapper->mask->bits[count / 8] &=
			    (uint8_t)(UINT8_MAX << (8 - count % 8));
	}
}

static void
pixel_mask_finalize(void *data)
{
	struct wren_pixel_mask *wrapper = data;

	st_pixel_mask_destroy(wrapper->mask);
	wrapper->mask = NULL;
}

static struct st_pixel_mask *
pixel_mask(WrenVM *vm)
{
	struct wren_pixel_mask *wrapper = slot_pixel_mask(vm, 0,
	    "invalid PixelMask receiver");

	return wrapper == NULL ? NULL : wrapper->mask;
}

static bool
mask_index(WrenVM *vm, const struct st_pixel_mask *mask, int slot,
	size_t *index)
{
	int64_t value;
	size_t count = mask_count(mask);

	if (!slot_integer(vm, slot, 0, WREN_MAX_EXACT_INTEGER, &value,
	    "PixelMask: index must be a non-negative integer"))
		return false;
	if ((uint64_t)value >= count) {
		st_wren_abort(vm, "PixelMask: index out of bounds");
		return false;
	}
	*index = (size_t)value;
	return true;
}

static bool
mask_position(WrenVM *vm, const struct st_pixel_mask *mask, int x_slot,
	int y_slot, uint32_t *x, uint32_t *y)
{
	if (!slot_uint32(vm, x_slot, x,
	    "PixelMask: x must be a non-negative integer")
	    || !slot_uint32(vm, y_slot, y,
	    "PixelMask: y must be a non-negative integer"))
		return false;
	if (*x >= mask->width || *y >= mask->height) {
		st_wren_abort(vm, "PixelMask: coordinates out of bounds");
		return false;
	}
	return true;
}

static bool
mask_bit(const struct st_pixel_mask *mask, size_t index)
{
	return (mask->bits[index / 8]
	    & (uint8_t)(UINT8_C(0x80) >> (index % 8))) != 0;
}

static void
set_mask_bit(struct st_pixel_mask *mask, size_t index, bool value)
{
	uint8_t bit = (uint8_t)(UINT8_C(0x80) >> (index % 8));

	if (value)
		mask->bits[index / 8] |= bit;
	else
		mask->bits[index / 8] &= (uint8_t)~bit;
}

static void
fn_pixel_mask_count(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);

	if (mask != NULL)
		wrenSetSlotDouble(vm, 0, (double)mask_count(mask));
}

static void
fn_pixel_mask_width(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);

	if (mask != NULL)
		wrenSetSlotDouble(vm, 0, mask->width);
}

static void
fn_pixel_mask_height(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);

	if (mask != NULL)
		wrenSetSlotDouble(vm, 0, mask->height);
}

static void
fn_pixel_mask_subscript(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	size_t index;

	if (mask != NULL && mask_index(vm, mask, 1, &index))
		wrenSetSlotBool(vm, 0, mask_bit(mask, index));
}

static void
fn_pixel_mask_subscript_set(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	size_t index;
	bool value;

	if (mask != NULL && mask_index(vm, mask, 1, &index)
	    && slot_bool(vm, 2, &value, "PixelMask value must be Bool"))
		set_mask_bit(mask, index, value);
}

static void
fn_pixel_mask_bit_at(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	uint32_t x;
	uint32_t y;
	bool value;

	if (mask != NULL && mask_position(vm, mask, 1, 2, &x, &y)
	    && st_pixel_mask_get(mask, x, y, &value) == 0)
		wrenSetSlotBool(vm, 0, value);
}

static void
fn_pixel_mask_set_bit(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	uint32_t x;
	uint32_t y;
	bool value;

	if (mask != NULL && mask_position(vm, mask, 1, 2, &x, &y)
	    && slot_bool(vm, 3, &value, "PixelMask value must be Bool"))
		(void)st_pixel_mask_set(mask, x, y, value);
}

static void
fn_pixel_mask_fill(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	bool value;
	size_t count;
	size_t bytes;

	if (mask == NULL || !slot_bool(vm, 1, &value,
	    "PixelMask.fill: value must be Bool"))
		return;
	count = mask_count(mask);
	bytes = (count + 7) / 8;
	if (bytes != 0)
		memset(mask->bits, value ? UINT8_MAX : 0, bytes);
	if (value && (count % 8) != 0)
		mask->bits[count / 8] &=
		    (uint8_t)(UINT8_MAX << (8 - count % 8));
}

static void
fn_pixel_mask_iterate(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	int64_t current;
	size_t count;

	if (mask == NULL)
		return;
	count = mask_count(mask);
	if (count == 0) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (wrenGetSlotType(vm, 1) == WREN_TYPE_NULL) {
		wrenSetSlotDouble(vm, 0, 0);
		return;
	}
	if (!slot_integer(vm, 1, 0, WREN_MAX_EXACT_INTEGER, &current,
	    "PixelMask.iterate: invalid iterator"))
		return;
	if ((uint64_t)current + 1 >= count)
		wrenSetSlotBool(vm, 0, false);
	else
		wrenSetSlotDouble(vm, 0, (double)(current + 1));
}

static void
fn_pixel_mask_iterator_value(WrenVM *vm)
{
	fn_pixel_mask_subscript(vm);
}

static void
fn_pixel_mask_from_bits(WrenVM *vm)
{
	struct st_pixel_mask *mask = NULL;
	const char *bits;
	uint32_t width;
	uint32_t height;
	size_t count;
	size_t byte_count;
	int length;

	if (!dimensions(vm, 1, 2, &width, &height, 1, &count,
	    "PixelMask.fromBits: invalid dimensions"))
		return;
	if (wrenGetSlotType(vm, 3) != WREN_TYPE_STRING) {
		st_wren_abort(vm, "PixelMask.fromBits: bits must be a String");
		return;
	}
	bits = wrenGetSlotBytes(vm, 3, &length);
	byte_count = (count + 7) / 8;
	if (byte_count > INT_MAX || length != (int)byte_count) {
		st_wren_abort(vm,
		    "PixelMask.fromBits: byte length must be ceil(width * height / 8)");
		return;
	}
	if (st_pixel_mask_create(&mask, width, height) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	if (byte_count != 0)
		memcpy(mask->bits, bits, byte_count);
	if ((count % 8) != 0)
		mask->bits[count / 8] &=
		    (uint8_t)(UINT8_MAX << (8 - count % 8));
	(void)return_pixel_mask(vm, mask);
}

static void
fn_pixel_mask_bits(WrenVM *vm)
{
	struct st_pixel_mask *mask = pixel_mask(vm);
	size_t bytes;

	if (mask == NULL)
		return;
	bytes = (mask_count(mask) + 7) / 8;
	wrenSetSlotBytes(vm, 0, bytes == 0 ? "" : (const char *)mask->bits,
	    bytes);
}

static void
fn_pixel_mask_from_pbm(WrenVM *vm)
{
	struct st_pixel_mask *mask = NULL;
	bool aborted;

	if (decode_source(vm, 1, false, NULL, &mask, &aborted) == 0)
		(void)return_pixel_mask(vm, mask);
	else if (!aborted)
		wrenSetSlotNull(vm, 0);
}

static void
pixel_blit_allocate(WrenVM *vm)
{
	struct wren_pixel_blit *wrapper = wrenSetSlotNewForeign(vm, 0, 0,
	    sizeof(*wrapper));

	wrapper->magic = PIXEL_BLIT_MAGIC;
	wrapper->blit = (struct st_pixel_blit){
		.scale_x = 1,
		.scale_y = 1,
	};
}

static void
pixel_blit_finalize(void *data)
{
	(void)data;
}

static struct st_pixel_blit *
pixel_blit(WrenVM *vm)
{
	struct wren_pixel_blit *wrapper = slot_pixel_blit(vm, 0,
	    "invalid PixelBlit receiver");

	return wrapper == NULL ? NULL : &wrapper->blit;
}

#define BLIT_UINT_FIELD(name, field, allow_zero) \
	static void fn_pixel_blit_##name(WrenVM *vm) \
	{ \
		struct st_pixel_blit *blit = pixel_blit(vm); \
		if (blit != NULL) \
			wrenSetSlotDouble(vm, 0, blit->field); \
	} \
	static void fn_pixel_blit_##name##_set(WrenVM *vm) \
	{ \
		struct st_pixel_blit *blit = pixel_blit(vm); \
		uint32_t value; \
		if (blit == NULL || !slot_uint32(vm, 1, &value, \
		    "PixelBlit." #name ": expected a non-negative integer")) \
			return; \
		if (!(allow_zero) && value == 0) { \
			st_wren_abort(vm, \
			    "PixelBlit." #name ": value must be positive"); \
			return; \
		} \
		blit->field = value; \
	}

#define BLIT_INT_FIELD(name, field) \
	static void fn_pixel_blit_##name(WrenVM *vm) \
	{ \
		struct st_pixel_blit *blit = pixel_blit(vm); \
		if (blit != NULL) \
			wrenSetSlotDouble(vm, 0, blit->field); \
	} \
	static void fn_pixel_blit_##name##_set(WrenVM *vm) \
	{ \
		struct st_pixel_blit *blit = pixel_blit(vm); \
		int32_t value; \
		if (blit != NULL && slot_int32(vm, 1, &value, \
		    "PixelBlit." #name ": expected a signed 32-bit integer")) \
			blit->field = value; \
	}

BLIT_UINT_FIELD(source_x, source_x, true)
BLIT_UINT_FIELD(source_y, source_y, true)
BLIT_UINT_FIELD(source_width, width, true)
BLIT_UINT_FIELD(source_height, height, true)
BLIT_INT_FIELD(destination_x, destination_x)
BLIT_INT_FIELD(destination_y, destination_y)
BLIT_UINT_FIELD(scale_x, scale_x, false)
BLIT_UINT_FIELD(scale_y, scale_y, false)
BLIT_UINT_FIELD(mask_x, mask_x, true)
BLIT_UINT_FIELD(mask_y, mask_y, true)

#undef BLIT_UINT_FIELD
#undef BLIT_INT_FIELD

static void
fn_pixel_blit_flip_x(WrenVM *vm)
{
	struct st_pixel_blit *blit = pixel_blit(vm);

	if (blit != NULL)
		wrenSetSlotBool(vm, 0,
		    (blit->flags & ST_PIXEL_BLIT_FLIP_X) != 0);
}

static void
fn_pixel_blit_flip_x_set(WrenVM *vm)
{
	struct st_pixel_blit *blit = pixel_blit(vm);
	bool value;

	if (blit == NULL || !slot_bool(vm, 1, &value,
	    "PixelBlit.flipX: expected Bool"))
		return;
	if (value)
		blit->flags |= ST_PIXEL_BLIT_FLIP_X;
	else
		blit->flags &= ~ST_PIXEL_BLIT_FLIP_X;
}

static void
fn_pixel_blit_flip_y(WrenVM *vm)
{
	struct st_pixel_blit *blit = pixel_blit(vm);

	if (blit != NULL)
		wrenSetSlotBool(vm, 0,
		    (blit->flags & ST_PIXEL_BLIT_FLIP_Y) != 0);
}

static void
fn_pixel_blit_flip_y_set(WrenVM *vm)
{
	struct st_pixel_blit *blit = pixel_blit(vm);
	bool value;

	if (blit == NULL || !slot_bool(vm, 1, &value,
	    "PixelBlit.flipY: expected Bool"))
		return;
	if (value)
		blit->flags |= ST_PIXEL_BLIT_FLIP_Y;
	else
		blit->flags &= ~ST_PIXEL_BLIT_FLIP_Y;
}

static bool
screen_pixels_supported(WrenVM *vm)
{
	uint32_t capabilities;

	return st_console_capabilities(st_wren_context_from_vm(vm)->console,
	    &capabilities) == 0
	    && (capabilities & ST_CONSOLE_CAP_PIXELS) != 0;
}

static void
set_size_result(WrenVM *vm, uint32_t width, uint32_t height)
{
	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotDouble(vm, 1, width);
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, height);
	wrenInsertInList(vm, 0, -1, 1);
}

static void
fn_screen_pixel_size(WrenVM *vm)
{
	uint32_t width;
	uint32_t height;

	if (!screen_pixels_supported(vm)
	    || st_console_pixel_size(st_wren_context_from_vm(vm)->console,
	    &width, &height) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	set_size_result(vm, width, height);
}

static void
fn_screen_cell_pixel_size(WrenVM *vm)
{
	uint32_t width;
	uint32_t height;

	if (!screen_pixels_supported(vm)
	    || st_console_cell_pixel_size(st_wren_context_from_vm(vm)->console,
	    &width, &height) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	set_size_result(vm, width, height);
}

static void
fn_screen_read_pixels(WrenVM *vm)
{
	struct st_console_rect source;
	struct st_pixel_buffer *pixels = NULL;
	size_t count;
	bool force = false;

	if (!slot_uint32(vm, 1, &source.x,
	    "Screen.readPixels: x must be a non-negative integer")
	    || !slot_uint32(vm, 2, &source.y,
	    "Screen.readPixels: y must be a non-negative integer")
	    || !dimensions(vm, 3, 4, &source.width, &source.height, 1, &count,
	    "Screen.readPixels: invalid dimensions"))
		return;
	(void)count;
	if (source.width == 0 || source.height == 0) {
		st_wren_abort(vm,
		    "Screen.readPixels: width and height must be positive");
		return;
	}
	if (wrenGetSlotCount(vm) > 5 && !slot_bool(vm, 5, &force,
	    "Screen.readPixels: force must be Bool"))
		return;
	if (!screen_pixels_supported(vm)
	    || st_console_get_pixels(&pixels,
	    st_wren_context_from_vm(vm)->console, &source, force) != 0) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	(void)return_pixel_buffer(vm, pixels);
}

static void
fn_screen_set_pixel(WrenVM *vm)
{
	uint32_t x;
	uint32_t y;
	uint32_t colour;

	if (!slot_uint32(vm, 1, &x,
	    "Screen.setPixel: x must be a non-negative integer")
	    || !slot_uint32(vm, 2, &y,
	    "Screen.setPixel: y must be a non-negative integer")
	    || !slot_colour(vm, 3, &colour))
		return;
	wrenSetSlotBool(vm, 0, screen_pixels_supported(vm)
	    && st_console_set_pixel(st_wren_context_from_vm(vm)->console,
	    x, y, colour) == 0);
}

static void
fn_screen_blit_pixels(WrenVM *vm)
{
	struct wren_pixel_buffer *source;
	struct wren_pixel_mask *mask = NULL;
	struct wren_pixel_blit *descriptor;
	struct st_pixel_blit blit;
	int descriptor_slot;

	source = slot_pixel_buffer(vm, 1,
	    "Screen.blitPixels: source must be a PixelBuffer");
	if (source == NULL)
		return;
	if (wrenGetSlotCount(vm) == 4) {
		mask = slot_pixel_mask(vm, 2,
		    "Screen.blitPixels: mask must be a PixelMask");
		if (mask == NULL)
			return;
		descriptor_slot = 3;
	}
	else
		descriptor_slot = 2;
	descriptor = slot_pixel_blit(vm, descriptor_slot,
	    "Screen.blitPixels: descriptor must be a PixelBlit");
	if (descriptor == NULL)
		return;
	blit = descriptor->blit;
	if (blit.source_x > source->pixels->width
	    || blit.source_y > source->pixels->height) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	if (blit.width == 0)
		blit.width = source->pixels->width - blit.source_x;
	if (blit.height == 0)
		blit.height = source->pixels->height - blit.source_y;
	if (blit.width == 0 || blit.height == 0
	    || blit.width > source->pixels->width - blit.source_x
	    || blit.height > source->pixels->height - blit.source_y
	    || (mask != NULL && (blit.mask_x >= mask->mask->width
	    || blit.mask_y >= mask->mask->height
	    || blit.width > mask->mask->width - blit.mask_x
	    || blit.height > mask->mask->height - blit.mask_y))) {
		wrenSetSlotBool(vm, 0, false);
		return;
	}
	wrenSetSlotBool(vm, 0, screen_pixels_supported(vm)
	    && st_console_blit_pixels(st_wren_context_from_vm(vm)->console,
	    source->pixels, mask == NULL ? NULL : mask->mask, &blit) == 0);
}

#define BIND(class_name_, static_, signature_, method_) \
	{class_name_, static_, signature_, method_}

static const struct pixel_binding bindings[] = {
	BIND("PixelColor", true, "rgb(_)", fn_pixel_colour_rgb),
	BIND("PixelColor", true, "palette(_)", fn_pixel_colour_palette),
	BIND("PixelColor", true, "isRgb(_)", fn_pixel_colour_is_rgb),
	BIND("PixelColor", true, "rgbValue(_)", fn_pixel_colour_rgb_value),
	BIND("PixelColor", true, "paletteIndex(_)",
	    fn_pixel_colour_palette_index),
	BIND("PixelColor", true, "toRgb(_)", fn_pixel_colour_to_rgb),

	BIND("PixelBuffer", true, "fromRgb24(_,_,_)",
	    fn_pixel_buffer_from_rgb24),
	BIND("PixelBuffer", true, "fromRgb24(_,_,_,_)",
	    fn_pixel_buffer_from_rgb24),
	BIND("PixelBuffer", true, "fromPpm(_)", fn_pixel_buffer_from_ppm),
	BIND("PixelBuffer", true, "fromJxl(_)", fn_pixel_buffer_from_jxl),
	BIND("PixelBuffer", true, "jxlSupported",
	    fn_pixel_buffer_jxl_supported),
	BIND("PixelBuffer", false, "count", fn_pixel_buffer_count),
	BIND("PixelBuffer", false, "[_]", fn_pixel_buffer_subscript),
	BIND("PixelBuffer", false, "[_]=(_)", fn_pixel_buffer_subscript_set),
	BIND("PixelBuffer", false, "iterate(_)", fn_pixel_buffer_iterate),
	BIND("PixelBuffer", false, "iteratorValue(_)",
	    fn_pixel_buffer_iterator_value),
	BIND("PixelBuffer", false, "width", fn_pixel_buffer_width),
	BIND("PixelBuffer", false, "height", fn_pixel_buffer_height),
	BIND("PixelBuffer", false, "pixelAt(_,_)", fn_pixel_buffer_pixel_at),
	BIND("PixelBuffer", false, "setPixel(_,_,_)",
	    fn_pixel_buffer_set_pixel),
	BIND("PixelBuffer", false, "fill(_)", fn_pixel_buffer_fill),
	BIND("PixelBuffer", false, "clone()", fn_pixel_buffer_clone),
	BIND("PixelBuffer", false, "rgb24", fn_pixel_buffer_rgb24),
	BIND("PixelBuffer", false, "hasAlternate",
	    fn_pixel_buffer_has_alternate),
	BIND("PixelBuffer", false, "alternateAt(_)",
	    fn_pixel_buffer_alternate_at),
	BIND("PixelBuffer", false, "setAlternate(_,_,_)",
	    fn_pixel_buffer_set_alternate),
	BIND("PixelBuffer", false, "fillAlternate(_)",
	    fn_pixel_buffer_fill_alternate),
	BIND("PixelBuffer", false, "alternateRgb24",
	    fn_pixel_buffer_alternate_rgb24),
	BIND("PixelBuffer", false, "setAlternateRgb24(_)",
	    fn_pixel_buffer_set_alternate_rgb24),
	BIND("PixelBuffer", false, "clearAlternate()",
	    fn_pixel_buffer_clear_alternate),

	BIND("PixelMask", true, "fromPbm(_)", fn_pixel_mask_from_pbm),
	BIND("PixelMask", true, "fromBits(_,_,_)", fn_pixel_mask_from_bits),
	BIND("PixelMask", false, "bits", fn_pixel_mask_bits),
	BIND("PixelMask", false, "count", fn_pixel_mask_count),
	BIND("PixelMask", false, "[_]", fn_pixel_mask_subscript),
	BIND("PixelMask", false, "[_]=(_)", fn_pixel_mask_subscript_set),
	BIND("PixelMask", false, "iterate(_)", fn_pixel_mask_iterate),
	BIND("PixelMask", false, "iteratorValue(_)",
	    fn_pixel_mask_iterator_value),
	BIND("PixelMask", false, "width", fn_pixel_mask_width),
	BIND("PixelMask", false, "height", fn_pixel_mask_height),
	BIND("PixelMask", false, "bitAt(_,_)", fn_pixel_mask_bit_at),
	BIND("PixelMask", false, "setBit(_,_,_)", fn_pixel_mask_set_bit),
	BIND("PixelMask", false, "fill(_)", fn_pixel_mask_fill),

#define BLIT_BIND(wren_name, c_name) \
	BIND("PixelBlit", false, #wren_name, fn_pixel_blit_##c_name), \
	BIND("PixelBlit", false, #wren_name "=(_)", fn_pixel_blit_##c_name##_set)
	BLIT_BIND(sourceX, source_x),
	BLIT_BIND(sourceY, source_y),
	BLIT_BIND(sourceWidth, source_width),
	BLIT_BIND(sourceHeight, source_height),
	BLIT_BIND(destinationX, destination_x),
	BLIT_BIND(destinationY, destination_y),
	BLIT_BIND(scaleX, scale_x),
	BLIT_BIND(scaleY, scale_y),
	BLIT_BIND(maskX, mask_x),
	BLIT_BIND(maskY, mask_y),
	BLIT_BIND(flipX, flip_x),
	BLIT_BIND(flipY, flip_y),
#undef BLIT_BIND

	BIND("Screen", true, "pixelSize", fn_screen_pixel_size),
	BIND("Screen", true, "cellPixelSize", fn_screen_cell_pixel_size),
	BIND("Screen", true, "readPixels(_,_,_,_)", fn_screen_read_pixels),
	BIND("Screen", true, "readPixels(_,_,_,_,_)", fn_screen_read_pixels),
	BIND("Screen", true, "setPixel(_,_,_)", fn_screen_set_pixel),
	BIND("Screen", true, "blitPixels(_,_)", fn_screen_blit_pixels),
	BIND("Screen", true, "blitPixels(_,_,_)", fn_screen_blit_pixels),
};

#undef BIND

WrenForeignMethodFn
st_wren_pixel_bind_method(const char *class_name, bool is_static,
	const char *signature)
{
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
		if (bindings[i].is_static == is_static
		    && strcmp(bindings[i].class_name, class_name) == 0
		    && strcmp(bindings[i].signature, signature) == 0)
			return bindings[i].method;
	return NULL;
}

WrenForeignClassMethods
st_wren_pixel_bind_class(const char *class_name)
{
	WrenForeignClassMethods methods = {NULL, NULL};

	if (strcmp(class_name, "PixelBuffer") == 0) {
		methods.allocate = pixel_buffer_allocate;
		methods.finalize = pixel_buffer_finalize;
	}
	else if (strcmp(class_name, "PixelMask") == 0) {
		methods.allocate = pixel_mask_allocate;
		methods.finalize = pixel_mask_finalize;
	}
	else if (strcmp(class_name, "PixelBlit") == 0) {
		methods.allocate = pixel_blit_allocate;
		methods.finalize = pixel_blit_finalize;
	}
	return methods;
}
