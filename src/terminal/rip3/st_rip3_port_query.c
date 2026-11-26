/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_ports.h"

static bool
equal(const char *text, const char *uppercase)
{
	for (size_t i = 0;; i++) {
		unsigned char ch = (unsigned char)text[i];
		if (ch >= 'a' && ch <= 'z')
			ch = (unsigned char)(ch - 'a' + 'A');
		if (ch != (unsigned char)uppercase[i])
			return false;
		if (ch == 0)
			return true;
	}
}

static int64_t
signed_word(int64_t value)
{
	uint64_t low = (uint64_t)value & UINT64_C(65535);
	return low >= 32768 ? (int64_t)low - 65536 : (int64_t)low;
}

static int
text_cursor(const struct st_rip3_host_variables *host, const struct st_rip3_builtin_reference *reference,
    char *result, size_t capacity)
{
	const struct st_rip3_ports *ports;
	struct st_rip3_port_info    info;
	struct st_rip3_frame        frame;
	unsigned                    number;
	bool                        current = reference->count == 0, device = false;
	char                        text[32];
	int                         status, length;
	if (reference->count > 2)
		return IO_ERROR_INVALID_FORMAT;
	if (reference->count != 0) {
		current = equal(reference->parameters[0].text, "CUR");
		if (!current) {
			status = st_rip3_host_number(reference->parameters[0].text, 0, 35, &number);
			if (status != 0)
				return status;
		}
	}
	if (reference->count == 2) {
		device = equal(reference->parameters[1].text, "DEVICE");
		if (!device && !equal(reference->parameters[1].text, "WORLD"))
			return IO_ERROR_INVALID_FORMAT;
	}
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	ports = host->tables->tables[ST_RIP3_TABLE_PORT].live;
	if (current) {
		status = st_rip3_ports_current(ports, &number);
		if (status != 0)
			return status;
	}
	status = st_rip3_ports_info(ports, number, &info);
	if (status != 0)
		return status;
	if (reference->count != 0 && (!info.defined || !info.viewport_active)) {
		memcpy(text, "-1", 3);
		length = 2;
	}
	else {
		int64_t x = info.cursor.x, y = info.cursor.y;
		if (device) {
			status = st_rip3_port_query_frame(host, ports, &frame);
			if (status != 0)
				return status;
			/* The shipped DEVICE branch converts the stored pair again;
			 * WORLD returns it raw, regardless of its producer's units. */
			x = x * frame.device_width / frame.world_width;
			y = y * frame.device_height / frame.world_height;
		}
		length = snprintf(text, sizeof(text), "%" PRId64 ":%" PRId64, signed_word(x), signed_word(y));
		if (length < 0 || (size_t)length >= sizeof(text))
			return IO_ERROR_IO;
	}
	if ((size_t)length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, (size_t)length + 1);
	return 0;
}

int
st_rip3_port_query_frame(const struct st_rip3_host_variables *host, const struct st_rip3_ports *ports,
    struct st_rip3_frame *result)
{
	const struct st_rip3_environments *environments = host->tables->tables[ST_RIP3_TABLE_ENV].live;
	struct st_rip3_port_info           screen;
	struct st_rip3_point               zero = {0}, checked;
	struct st_rip3_frame               frame;
	int                                status;

	if (environments != NULL) {
		unsigned number;
		status = st_rip3_environments_current(environments, &number);
		if (status == 0)
			status = st_rip3_environments_frame(environments, number, &frame);
		if (status != 0)
			return status;
	}
	else if (host->frame != NULL)
		frame = *host->frame;
	else
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_device_to_world(&frame, &zero, &checked);
	if (status == 0)
		status = st_rip3_ports_info(ports, 0, &screen);
	if (status != 0)
		return status;
	if (frame.device_width != (uint32_t)screen.bounds.x1 + 1
	    || frame.device_height != (uint32_t)screen.bounds.y1 + 1)
		return IO_ERROR_CONFLICT;
	*result = frame;
	return 0;
}

static int
offscreen(const struct st_rip3_host_variables *host, const struct st_rip3_builtin_reference *reference,
    char *result, size_t capacity)
{
	if (reference->count > 1)
		return IO_ERROR_INVALID_FORMAT;
	const char *mode = reference->count ? reference->parameters[0].text : "FREE";
	bool        clip = equal(mode, "CLIP"), total = equal(mode, "TOTAL");
	bool        used = equal(mode, "USED"), remaining = equal(mode, "FREE");
	if (!clip && !total && !used && !remaining)
		return IO_ERROR_INVALID_FORMAT;
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	const struct st_rip3_ports *ports = host->tables->tables[ST_RIP3_TABLE_PORT].live;
	char                        text[24];
	int                         length;
	if (clip)
		length = snprintf(text, sizeof(text), "%d", st_rip3_ports_clipboard_index(ports));
	else {
		if (host->offscreen_pixel_budget == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		uint64_t limit  = *host->offscreen_pixel_budget;
		uint64_t pixels = 0;
		if (!total) {
			int status = st_rip3_ports_bitmap_pixels(ports, 0, &pixels);
			if (status != 0)
				return status;
			if (pixels > limit)
				pixels = limit;
		}
		uint64_t value = used ? pixels : limit - pixels;
		length         = snprintf(text, sizeof(text), "%" PRIu64, value);
	}
	if (length < 0 || (size_t)length >= sizeof(text))
		return IO_ERROR_IO;
	if ((size_t)length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, (size_t)length + 1);
	return 0;
}

int
st_rip3_port_query_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_port_info    info;
	const struct st_rip3_ports *ports;
	struct st_rip3_rect         area;
	struct st_rip3_frame        frame;
	unsigned                    number;
	bool                        view = false, device = false;
	int64_t                     value;
	char                        text[24];
	int                         status, length;

	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_builtin_reference words;
	status = st_rip3_builtin_reference_words(&words, reference);
	if (status != 0)
		return status;
	reference        = &words;
	const char *name = reference->name;
	if (strcmp(name, "TEXTXY") == 0)
		return text_cursor(host, reference, result, capacity);
	if (strcmp(name, "OFFSCREEN") == 0)
		return offscreen(host, reference, result, capacity);
	bool width = strcmp(name, "PORTW") == 0, height = strcmp(name, "PORTH") == 0;
	bool x0 = strcmp(name, "PORTX0") == 0, y0 = strcmp(name, "PORTY0") == 0;
	bool x1 = strcmp(name, "PORTX1") == 0, y1 = strcmp(name, "PORTY1") == 0;
	bool current = reference->count == 0;

	if (!width && !height && !x0 && !y0 && !x1 && !y1)
		return IO_ERROR_NOT_SUPPORTED;
	if (reference->count > 3)
		return IO_ERROR_INVALID_FORMAT;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (reference->count != 0) {
		current = equal(reference->parameters[0].text, "CUR");
		if (!current) {
			status = st_rip3_host_number(reference->parameters[0].text, 0, 35, &number);
			if (status != 0)
				return status;
		}
	}
	if (reference->count >= 2) {
		const char *type = reference->parameters[1].text;
		if (equal(type, "VIEW"))
			view = true;
		else if (!equal(type, "PORT"))
			return IO_ERROR_INVALID_FORMAT;
	}
	if (reference->count == 3) {
		const char *domain = reference->parameters[2].text;
		if (equal(domain, "DEVICE"))
			device = true;
		else if (!equal(domain, "WORLD"))
			return IO_ERROR_INVALID_FORMAT;
	}
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	ports = host->tables->tables[ST_RIP3_TABLE_PORT].live;
	if (current) {
		status = st_rip3_ports_current(ports, &number);
		if (status != 0)
			return status;
	}
	status = st_rip3_ports_info(ports, number, &info);
	if (status != 0)
		return status;
	if (!info.defined || !info.viewport_active)
		value = width || height ? 0 : -1;
	else {
		area = view ? info.viewport : info.bounds;
		/* Device tables store inclusive last pixels. Query lower-right
		 * coordinates and dimensions are exclusive boundary/count values. */
		int64_t coordinate;
		if (width)
			coordinate = (int64_t)area.x1 - area.x0 + 1;
		else if (height)
			coordinate = (int64_t)area.y1 - area.y0 + 1;
		else if (x0)
			coordinate = area.x0;
		else if (y0)
			coordinate = area.y0;
		else if (x1)
			coordinate = (int64_t)area.x1 + 1;
		else
			coordinate = (int64_t)area.y1 + 1;
		value = coordinate;
		if (!device) {
			status = st_rip3_port_query_frame(host, ports, &frame);
			if (status != 0)
				return status;
			bool     horizontal  = width || x0 || x1;
			uint32_t numerator   = horizontal ? frame.world_width : frame.world_height;
			uint32_t denominator = horizontal ? frame.device_width : frame.device_height;
			/* Counts/exclusive ends can exceed int32_t even though every stored
			 * coordinate fits. The validated frame and two int32_t endpoints
			 * bound this product below 2^48. Retain signed floor for origins. */
			int64_t product = coordinate * numerator;
			value           = product / denominator;
			if (product < 0 && product % denominator != 0)
				value--;
		}
	}
	/* Spec correction: provide the documented numeric query instead of
	 * reproducing the shipped all-six inverted-success callback defect. */
	length = snprintf(text, sizeof(text), "%" PRId64, value);
	if (length < 0 || (size_t)length >= sizeof(text))
		return IO_ERROR_IO;
	if ((size_t)length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, (size_t)length + 1);
	return 0;
}
