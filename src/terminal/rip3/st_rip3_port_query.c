/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool
equal(const char *text, const char *uppercase)
{
	for (size_t i = 0; ; i++) {
		unsigned char ch = (unsigned char)text[i];
		if (ch >= 'a' && ch <= 'z')
			ch = (unsigned char)(ch - 'a' + 'A');
		if (ch != (unsigned char)uppercase[i])
			return false;
		if (ch == 0)
			return true;
	}
}

int
st_rip3_port_query_frame(const struct st_rip3_host_variables *host,
    const struct st_rip3_ports *ports, struct st_rip3_frame *result)
{
	const struct st_rip3_environments *environments = host->tables->tables[ST_RIP3_TABLE_ENV].live;
	struct st_rip3_port_info screen;
	struct st_rip3_point zero = {0}, checked;
	struct st_rip3_frame frame;
	int status;

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

int
st_rip3_port_query_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_port_info info;
	const struct st_rip3_ports *ports;
	struct st_rip3_point point = {0};
	struct st_rip3_rect area;
	struct st_rip3_frame frame;
	unsigned number;
	bool view = false, device = false;
	int32_t value;
	char text[16];
	int status, length;

	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	const char *name = reference->name;
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
		else if (!equal(type, "PORT")) {
			/* PORTW/H's two-field DOMAIN syntax conflicts with their explicit
			 * three-field TYPE/DOMAIN prose/examples. Do not guess shorthand. */
			if ((width || height) && reference->count == 2
			    && (equal(type, "DEVICE") || equal(type, "WORLD")))
				return IO_ERROR_NOT_SUPPORTED;
			return IO_ERROR_INVALID_FORMAT;
		}
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
		if (width) coordinate = (int64_t)area.x1-area.x0+1;
		else if (height) coordinate = (int64_t)area.y1-area.y0+1;
		else if (x0) coordinate = area.x0;
		else if (y0) coordinate = area.y0;
		else if (x1) coordinate = (int64_t)area.x1+1;
		else coordinate = (int64_t)area.y1+1;
		if (coordinate < INT32_MIN || coordinate > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
		if (width || x0 || x1) point.x = (int32_t)coordinate;
		else point.y = (int32_t)coordinate;
		if (!device) {
			status = st_rip3_port_query_frame(host, ports, &frame);
			if (status == 0)
				status = st_rip3_device_to_world(&frame, &point, &point);
			if (status != 0)
				return status;
		}
		value = width || x0 || x1 ? point.x : point.y;
	}
	length = snprintf(text, sizeof(text), "%" PRId32, value);
	if (length < 0 || (size_t)length >= sizeof(text))
		return IO_ERROR_IO;
	if ((size_t)length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, (size_t)length + 1);
	return 0;
}
