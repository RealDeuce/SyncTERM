/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"

#include "io_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct port {
	struct st_rip3_port_info info;
	/* Only offscreen ports own a surface. Screen ports share screen. */
	struct st_rip3_surface *bitmap;
};

struct st_rip3_ports {
	struct st_rip3_surface *screen;
	struct st_rip3_rect screen_bounds;
	unsigned current;
	struct port entries[36];
};

static bool
rect_valid(const struct st_rip3_rect *rect)
{
	return rect != NULL && rect->x0 <= rect->x1 && rect->y0 <= rect->y1;
}

static bool
clip(struct st_rip3_rect *area, const struct st_rip3_rect *bounds)
{
	if (area->x0 < bounds->x0)
		area->x0 = bounds->x0;
	if (area->y0 < bounds->y0)
		area->y0 = bounds->y0;
	if (area->x1 > bounds->x1)
		area->x1 = bounds->x1;
	if (area->y1 > bounds->y1)
		area->y1 = bounds->y1;
	return rect_valid(area);
}

static struct st_rip3_rect
port_bounds(const struct port *port)
{
	return (struct st_rip3_rect){0, 0,
	    port->info.bounds.x1 - port->info.bounds.x0,
	    port->info.bounds.y1 - port->info.bounds.y0};
}

static struct st_rip3_rect
viewport_absolute(const struct port *port)
{
	struct st_rip3_rect view = port->info.viewport;

	view.x0 += port->info.bounds.x0;
	view.x1 += port->info.bounds.x0;
	view.y0 += port->info.bounds.y0;
	view.y1 += port->info.bounds.y0;
	return view;
}

static struct st_rip3_surface *
port_surface(const struct st_rip3_ports *ports, unsigned number)
{
	return ports->entries[number].info.offscreen
	    ? ports->entries[number].bitmap : ports->screen;
}

static void
clear_port(struct port *port)
{
	st_rip3_surface_destroy(port->bitmap);
	*port = (struct port){0};
}

int
st_rip3_ports_create(struct st_rip3_ports **result, enum st_rip3_canvas canvas)
{
	struct st_rip3_ports *ports;
	struct st_rip3_frame frame;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_frame_init(&frame, canvas);
	if (status != 0)
		return status;
	ports = calloc(1, sizeof(*ports));
	if (ports == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	status = st_rip3_surface_create(&ports->screen, frame.device_width,
	    frame.device_height);
	if (status != 0) {
		free(ports);
		return status;
	}
	ports->screen_bounds = (struct st_rip3_rect){0, 0,
	    (int32_t)frame.device_width - 1, (int32_t)frame.device_height - 1};
	(void)st_rip3_ports_reset(ports, true);
	*result = ports;
	return 0;
}

void
st_rip3_ports_destroy(struct st_rip3_ports *ports)
{
	if (ports == NULL)
		return;
	for (unsigned i = 1; i < 36; i++)
		clear_port(&ports->entries[i]);
	st_rip3_surface_destroy(ports->screen);
	free(ports);
}

int
st_rip3_ports_current(const struct st_rip3_ports *ports, unsigned *result)
{
	if (ports == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = ports->current;
	return 0;
}

int
st_rip3_ports_info(const struct st_rip3_ports *ports, unsigned number,
    struct st_rip3_port_info *result)
{
	if (ports == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	*result = ports->entries[number].info;
	return 0;
}

int
st_rip3_ports_define(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *bounds, bool offscreen)
{
	struct port value = {0};
	int status;

	if (ports == NULL || bounds == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (ports->entries[number].info.protected) {
		ports->current = 0;
		return IO_ERROR_PERMISSION_DENIED;
	}
	value.info.bounds = *bounds;
	if (offscreen) {
		value.info.bounds.x0 = 0;
		value.info.bounds.y0 = 0;
		if (bounds->x1 < 0 || bounds->y1 < 0)
			return IO_ERROR_OUT_OF_RANGE;
		status = st_rip3_surface_create(&value.bitmap,
		    (uint32_t)bounds->x1 + 1, (uint32_t)bounds->y1 + 1);
		if (status != 0)
			return status;
	}
	else {
		if (!rect_valid(bounds))
			return IO_ERROR_INVALID_ARGUMENT;
		if (!clip(&value.info.bounds, &ports->screen_bounds))
			return IO_ERROR_OUT_OF_RANGE;
	}
	value.info.defined = true;
	value.info.offscreen = offscreen;
	value.info.viewport_active = true;
	value.info.viewport = port_bounds(&value);
	value.info.image = (struct st_rip3_image_style){.automatic = true, .enabled = true};
	clear_port(&ports->entries[number]);
	ports->entries[number] = value;
	return 0;
}

int
st_rip3_ports_switch(struct st_rip3_ports *ports, unsigned number)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (!ports->entries[number].info.defined) {
		ports->current = 0;
		return IO_ERROR_NOT_FOUND;
	}
	ports->current = number;
	return 0;
}

int
st_rip3_ports_protect(struct st_rip3_ports *ports, unsigned number, bool protect)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (ports->entries[number].info.defined)
		ports->entries[number].info.protected = protect;
	return 0;
}

int
st_rip3_ports_delete(struct st_rip3_ports *ports, unsigned number)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	if (number != 0) {
		if (ports->entries[number].info.protected)
			return IO_ERROR_PERMISSION_DENIED;
		clear_port(&ports->entries[number]);
	}
	else {
		for (unsigned i = 1; i < 36; i++) {
			if (!ports->entries[i].info.protected)
				clear_port(&ports->entries[i]);
		}
	}
	if (!ports->entries[ports->current].info.defined)
		ports->current = 0;
	return 0;
}

int
st_rip3_ports_reset(struct st_rip3_ports *ports, bool hard)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	for (unsigned i = 1; i < 36; i++) {
		if (hard || !ports->entries[i].info.protected)
			clear_port(&ports->entries[i]);
	}
	ports->entries[0].info = (struct st_rip3_port_info){
		.defined = true,
		.viewport_active = true,
		.bounds = ports->screen_bounds,
		.viewport = ports->screen_bounds,
		.image = {.automatic = true, .enabled = true},
	};
	ports->current = 0;
	return 0;
}

int
st_rip3_ports_viewport(struct st_rip3_ports *ports,
    const struct st_rip3_rect *viewport)
{
	struct port *port;
	struct st_rip3_rect view;
	struct st_rip3_rect bounds;

	if (ports == NULL || !rect_valid(viewport))
		return IO_ERROR_INVALID_ARGUMENT;
	port = &ports->entries[ports->current];
	if (port->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	if (viewport->x0 == 0 && viewport->y0 == 0
	    && viewport->x1 == 0 && viewport->y1 == 0) {
		port->info.viewport_active = false;
		return 0;
	}
	bounds = port_bounds(port);
	view = *viewport;
	if (!clip(&view, &bounds))
		return IO_ERROR_OUT_OF_RANGE;
	port->info.viewport = view;
	port->info.viewport_active = true;
	return 0;
}

int
st_rip3_ports_activate(struct st_rip3_ports *ports, bool active)
{
	struct port *port;

	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	port = &ports->entries[ports->current];
	if (port->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	port->info.viewport_active = active;
	return 0;
}

static int32_t
clamp_image_coordinate(int32_t value, int32_t maximum)
{
	return value < 0 ? 0 : value > maximum ? maximum : value;
}

int
st_rip3_ports_image_style(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, unsigned flags)
{
	struct st_rip3_image_style style = {.automatic = area == NULL,
	    .enabled = true, .flags = flags};
	struct port *port;

	if (ports == NULL || (area != NULL && !rect_valid(area)))
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36 || flags > 15)
		return IO_ERROR_OUT_OF_RANGE;
	port = &ports->entries[number];
	if (!port->info.defined)
		return IO_ERROR_NOT_FOUND;
	if (port->info.protected)
		return IO_ERROR_PERMISSION_DENIED;
	if (area != NULL) {
		int32_t xmax = port->info.viewport.x1 - port->info.viewport.x0;
		int32_t ymax = port->info.viewport.y1 - port->info.viewport.y0;

		style.enabled = area->x0 != 0 || area->y0 != 0
		    || area->x1 != 0 || area->y1 != 0;
		style.area = (struct st_rip3_rect){
		    clamp_image_coordinate(area->x0, xmax),
		    clamp_image_coordinate(area->y0, ymax),
		    clamp_image_coordinate(area->x1, xmax),
		    clamp_image_coordinate(area->y1, ymax)};
	}
	port->info.image = style;
	return 0;
}

/* Translate through the viewport origin, intersect in wide arithmetic, then
 * narrow. Even an extreme offscreen drawing coordinate cannot wrap on screen. */
static bool
translate(const struct port *port, const struct st_rip3_rect *area,
    struct st_rip3_rect *visible)
{
	struct st_rip3_rect view = viewport_absolute(port);
	int64_t x0, x1, y0, y1;

	if (area == NULL) {
		*visible = view;
		return true;
	}
	x0 = (int64_t)view.x0 + area->x0;
	y0 = (int64_t)view.y0 + area->y0;
	x1 = (int64_t)view.x0 + area->x1;
	y1 = (int64_t)view.y0 + area->y1;
	if (x0 < view.x0)
		x0 = view.x0;
	if (y0 < view.y0)
		y0 = view.y0;
	if (x1 > view.x1)
		x1 = view.x1;
	if (y1 > view.y1)
		y1 = view.y1;
	if (x0 > x1 || y0 > y1)
		return false;
	*visible = (struct st_rip3_rect){(int32_t)x0, (int32_t)y0,
	    (int32_t)x1, (int32_t)y1};
	return true;
}

int
st_rip3_ports_fill(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
    uint8_t color, enum st_rip3_write_mode mode)
{
	struct port *port;
	struct st_rip3_rect visible;

	if (ports == NULL || !rect_valid(area)
	    || mode < ST_RIP3_COPY || mode > ST_RIP3_NOT)
		return IO_ERROR_INVALID_ARGUMENT;
	port = &ports->entries[ports->current];
	if (!port->info.viewport_active || !translate(port, area, &visible))
		return 0;
	return st_rip3_surface_fill(port_surface(ports, ports->current), NULL,
	    &visible, color, mode);
}

int
st_rip3_ports_plot(struct st_rip3_ports *ports, int32_t x, int32_t y,
    uint8_t color, enum st_rip3_write_mode mode)
{
	struct st_rip3_rect point = {x, y, x, y};

	return st_rip3_ports_fill(ports, &point, color, mode);
}

int
st_rip3_ports_erase(struct st_rip3_ports *ports, uint8_t background)
{
	struct port *port;
	struct st_rip3_rect view;

	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	port = &ports->entries[ports->current];
	if (!port->info.viewport_active)
		return 0;
	view = viewport_absolute(port);
	return st_rip3_surface_fill(port_surface(ports, ports->current), NULL,
	    &view, background, ST_RIP3_COPY);
}

int
st_rip3_ports_operate_views(struct st_rip3_ports *ports,
    const unsigned *numbers, size_t count, enum st_rip3_view_operation operation,
    uint8_t background)
{
	bool selected[36] = {0};

	if (ports == NULL || (count != 0 && numbers == NULL)
	    || operation < ST_RIP3_VIEW_ACTIVATE || operation > ST_RIP3_VIEW_ERASE)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < count; i++) {
		if (numbers[i] >= 36)
			return IO_ERROR_OUT_OF_RANGE;
		selected[numbers[i]] = true;
	}
	for (unsigned i = 0; i < 36; i++) {
		struct port *port = &ports->entries[i];

		if (!selected[i] || !port->info.defined)
			continue;
		if (operation == ST_RIP3_VIEW_ERASE) {
			struct st_rip3_rect view;
			int status;

			if (!port->info.viewport_active)
				continue;
			view = viewport_absolute(port);
			status = st_rip3_surface_fill(port_surface(ports, i), NULL,
			    &view, background, ST_RIP3_COPY);
			if (status != 0)
				return status;
		}
		else if (!port->info.protected) {
			if (operation == ST_RIP3_VIEW_MAXIMIZE) {
				if (port->info.viewport_active)
					port->info.viewport = port_bounds(port);
			}
			else
				port->info.viewport_active = operation == ST_RIP3_VIEW_ACTIVATE;
		}
	}
	return 0;
}

int
st_rip3_ports_copy(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, unsigned destination,
    const struct st_rip3_rect *destination_area, enum st_rip3_transfer_mode mode)
{
	const struct port *src;
	const struct port *dst;
	struct st_rip3_rect from;
	struct st_rip3_rect to;

	if (ports == NULL || (source_area != NULL && !rect_valid(source_area))
	    || (destination_area != NULL && !rect_valid(destination_area))
	    || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (source >= 36 || destination >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	src = &ports->entries[source];
	dst = &ports->entries[destination];
	if (!src->info.defined || !dst->info.defined
	    || !src->info.viewport_active || !dst->info.viewport_active
	    || !translate(src, source_area, &from)
	    || !translate(dst, destination_area, &to))
		return 0;
	return st_rip3_surface_copy(port_surface(ports, destination), NULL, &to,
	    port_surface(ports, source), NULL, &from, mode);
}

int
st_rip3_ports_read(const struct st_rip3_ports *ports, unsigned number,
    int32_t x, int32_t y, uint8_t *result)
{
	const struct port *port;
	struct st_rip3_rect bounds;

	if (ports == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	port = &ports->entries[number];
	if (!port->info.defined)
		return IO_ERROR_NOT_FOUND;
	bounds = port_bounds(port);
	if (x < 0 || y < 0 || x > bounds.x1 || y > bounds.y1)
		return IO_ERROR_OUT_OF_RANGE;
	return st_rip3_surface_get(port_surface(ports, number),
	    x + port->info.bounds.x0, y + port->info.bounds.y0, result);
}

int
st_rip3_ports_snapshot(const struct st_rip3_ports *ports,
    const struct st_rip3_palette *palette, struct st_pixel_buffer **result)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return st_rip3_surface_snapshot(ports->screen, palette, result);
}

struct port_snapshot {
	struct st_rip3_rect screen_bounds;
	unsigned current;
	struct port entries[36];
};

static void
port_backup_destroy(void *object)
{
	struct port_snapshot *snapshot = object;

	if (snapshot == NULL)
		return;
	for (size_t i = 0; i < 36; i++)
		clear_port(&snapshot->entries[i]);
	free(snapshot);
}

static int
clone_entry(struct port *result, const struct port *source)
{
	struct port value = {.info = source->info};
	int status;

	if (source->bitmap != NULL) {
		status = st_rip3_surface_clone(&value.bitmap, source->bitmap);
		if (status != 0)
			return status;
	}
	*result = value;
	return 0;
}

static int
port_backup_clone(const void *object, void **result)
{
	const struct st_rip3_ports *ports = object;
	struct port_snapshot *snapshot = calloc(1, sizeof(*snapshot));
	int status;

	if (snapshot == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	snapshot->screen_bounds = ports->screen_bounds;
	snapshot->current = ports->current;
	for (size_t i = 0; i < 36; i++) {
		status = clone_entry(&snapshot->entries[i], &ports->entries[i]);
		if (status != 0) {
			port_backup_destroy(snapshot);
			return status;
		}
	}
	*result = snapshot;
	return 0;
}

static int
port_backup_restore(void *object, const void *saved)
{
	struct st_rip3_ports *ports = object;
	const struct port_snapshot *snapshot = saved;
	struct port entries[36] = {0};
	int status;

	if (ports->screen_bounds.x0 != snapshot->screen_bounds.x0
	    || ports->screen_bounds.y0 != snapshot->screen_bounds.y0
	    || ports->screen_bounds.x1 != snapshot->screen_bounds.x1
	    || ports->screen_bounds.y1 != snapshot->screen_bounds.y1)
		return IO_ERROR_CONFLICT;
	/* No live entry or screen pixel changes until every owned bitmap has
	 * been copied. Screen ports have definitions only, never a pixel copy. */
	for (size_t i = 0; i < 36; i++) {
		status = clone_entry(&entries[i], &snapshot->entries[i]);
		if (status != 0) {
			for (size_t j = 0; j < i; j++)
				clear_port(&entries[j]);
			return status;
		}
	}
	for (size_t i = 0; i < 36; i++) {
		clear_port(&ports->entries[i]);
		ports->entries[i] = entries[i];
	}
	ports->current = snapshot->current;
	return 0;
}

int
st_rip3_ports_backup_create(struct st_rip3_backup **result)
{
	static const struct st_rip3_backup_ops ops = {
		port_backup_clone, port_backup_restore, port_backup_destroy,
	};

	return st_rip3_backup_create(result, &ops);
}
