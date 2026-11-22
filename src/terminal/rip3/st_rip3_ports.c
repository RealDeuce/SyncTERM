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

static const struct st_rip3_image_style image_defaults = {
	.automatic = true, .enabled = true, .area = {-1,-1,-1,-1},
};

struct st_rip3_ports {
	struct st_rip3_surface *screen;
	uint8_t *damage;
	size_t damage_length;
	struct st_rip3_rect screen_bounds;
	unsigned current;
	uint64_t sequence,generations[36];
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
	struct st_rip3_frame frame;
	int status=st_rip3_frame_init(&frame,canvas);
	return status!=0 ? status : st_rip3_ports_create_device(result,frame.device_width,frame.device_height);
}

int
st_rip3_ports_create_device(struct st_rip3_ports **result, uint32_t width, uint32_t height)
{
	struct st_rip3_ports *ports;
	struct st_rip3_frame frame;
	int status;

	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_frame_init_device(&frame, width, height);
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
	result->clip = (struct st_rip3_rect){0,0,-1,-1};
	if (result->defined) {
		struct st_rip3_rect view = viewport_absolute(&ports->entries[number]);
		struct st_rip3_rect visible = view;
		const struct st_rip3_rect *physical = result->offscreen ? &result->bounds : &ports->screen_bounds;
		if (clip(&visible,physical))
			result->clip = (struct st_rip3_rect){visible.x0-view.x0,visible.y0-view.y0,
			    visible.x1-view.x0,visible.y1-view.y0};
	}
	return 0;
}

uint64_t
st_rip3_ports_generation(const struct st_rip3_ports *ports,unsigned number)
{
	return ports==NULL || number>=36 || !ports->entries[number].info.defined ? 0 : ports->generations[number];
}

static bool
generations_available(const struct st_rip3_ports *ports,unsigned count)
{
	return ports->sequence<=UINT64_MAX-count;
}

int
st_rip3_ports_define(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *bounds, bool offscreen)
{
	struct port value = {0};
	int status;

	if (ports == NULL || bounds == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36 || !generations_available(ports,1))
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
	value.info.image = image_defaults;
	clear_port(&ports->entries[number]);
	ports->entries[number] = value;
	ports->generations[number]=++ports->sequence;
	return 0;
}

int
st_rip3_ports_install_bitmap(struct st_rip3_ports *ports, unsigned number,
    struct st_rip3_surface **bitmap)
{
	uint32_t width, height;
	struct port value = {0};
	int status;

	if (ports == NULL || bitmap == NULL || *bitmap == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number == 0 || number >= 36 || !generations_available(ports,1)) return IO_ERROR_OUT_OF_RANGE;
	if (ports->entries[number].info.protected) return IO_ERROR_PERMISSION_DENIED;
	status = st_rip3_surface_size(*bitmap,&width,&height);
	if (status != 0) return status;
	if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	value.info.defined = true; value.info.offscreen = true; value.info.viewport_active = true;
	value.info.bounds = (struct st_rip3_rect){0,0,(int32_t)width-1,(int32_t)height-1};
	value.info.viewport = value.info.bounds;
	value.info.image = image_defaults;
	value.bitmap = *bitmap;
	clear_port(&ports->entries[number]); ports->entries[number] = value;
	ports->generations[number]=++ports->sequence;
	*bitmap = NULL;
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
st_rip3_ports_select(struct st_rip3_ports *ports, unsigned number)
{
	int status;

	if (ports == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36) return IO_ERROR_OUT_OF_RANGE;
	if (!ports->entries[number].info.defined) {
		status = st_rip3_ports_define(ports,number,&ports->screen_bounds,false);
		if (status != 0) {
			ports->current = 0;
			return status;
		}
	}
	return st_rip3_ports_switch(ports,number);
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
	if (number >= 36 || !generations_available(ports,36))
		return IO_ERROR_OUT_OF_RANGE;
	if (number != 0) {
		if (ports->entries[number].info.protected)
			return IO_ERROR_PERMISSION_DENIED;
		clear_port(&ports->entries[number]);
		ports->generations[number]=++ports->sequence;
	}
	else {
		for (unsigned i = 1; i < 36; i++) {
			if (!ports->entries[i].info.protected) {
				clear_port(&ports->entries[i]);ports->generations[i]=++ports->sequence;
			}
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
	ports->entries[ports->current].info.image = image_defaults;
	if (!generations_available(ports,36)) return IO_ERROR_OUT_OF_RANGE;
	for (unsigned i = 1; i < 36; i++) {
		if (hard || !ports->entries[i].info.protected) {
			clear_port(&ports->entries[i]);ports->generations[i]=++ports->sequence;
		}
	}
	ports->generations[0]=++ports->sequence;
	ports->entries[0].info = (struct st_rip3_port_info){
		.defined = true,
		.viewport_active = true,
		.bounds = ports->screen_bounds,
		.viewport = ports->screen_bounds,
		.image = image_defaults,
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
	bounds = port_bounds(port);
	view = *viewport;
	if (!clip(&view, &bounds))
		return IO_ERROR_OUT_OF_RANGE;
	port->info.viewport = view;
	port->info.viewport_active = true;
	return 0;
}

int
st_rip3_ports_viewport_device(struct st_rip3_ports *ports,
    const struct st_rip3_rect *viewport)
{
	struct port *port;

	if (ports == NULL || !rect_valid(viewport) || viewport->x0 < 0 || viewport->y0 < 0)
		return IO_ERROR_INVALID_ARGUMENT;
	port = &ports->entries[ports->current];
	if (!port->info.defined) return IO_ERROR_NOT_FOUND;
	if (port->info.protected) return IO_ERROR_PERMISSION_DENIED;
	/* Absolute inputs and nonnegative port origins fit int32_t. Subtraction
	 * may produce negative relative coordinates, but addition in the drawing
	 * path reconstructs the original representable absolute rectangle. */
	port->info.viewport = (struct st_rip3_rect){
	    viewport->x0-port->info.bounds.x0, viewport->y0-port->info.bounds.y0,
	    viewport->x1-port->info.bounds.x0, viewport->y1-port->info.bounds.y0};
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

int
st_rip3_ports_cursor(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_cursor *position)
{
	const int64_t limit = INT64_C(1024)*INT32_MAX;
	if (ports == NULL || position == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36) return IO_ERROR_OUT_OF_RANGE;
	if (!ports->entries[number].info.defined) return IO_ERROR_NOT_FOUND;
	if (position->x < -limit || position->x > limit || position->y < -limit || position->y > limit)
		return IO_ERROR_OUT_OF_RANGE;
	ports->entries[number].info.cursor = *position;
	return 0;
}

int
st_rip3_ports_image_style(struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, unsigned flags)
{
	if (ports == NULL || (area != NULL && !rect_valid(area))) return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36 || flags > 63) return IO_ERROR_OUT_OF_RANGE;
	if (!ports->entries[number].info.defined) return IO_ERROR_NOT_FOUND;
	struct st_rip3_image_style style = image_defaults;
	style.flags = flags;
	if (area != NULL) {
		style.area = *area;
		style.automatic = area->x0 == -1 && area->y0 == -1 && area->x1 == -1 && area->y1 == -1;
		style.enabled = area->x0 != 0 || area->y0 != 0 || area->x1 != 0 || area->y1 != 0;
	}
	ports->entries[number].info.image = style; return 0;
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
	return st_rip3_surface_copy_projected(port_surface(ports,destination),&to,
	    port_surface(ports,source),&from,mode);
}

int
st_rip3_ports_blit(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, unsigned destination,
    int32_t destination_x, int32_t destination_y,
    enum st_rip3_transfer_mode mode)
{
	const struct port *src;
	const struct port *dst;
	struct st_rip3_rect from, source_view, destination_view;
	int64_t x, y;

	if (ports == NULL || (source_area != NULL && !rect_valid(source_area))
	    || mode < ST_RIP3_TRANSFER_COPY || mode > ST_RIP3_TRANSFER_INVERT)
		return IO_ERROR_INVALID_ARGUMENT;
	if (source >= 36 || destination >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	src = &ports->entries[source];
	dst = &ports->entries[destination];
	if (!src->info.defined || !dst->info.defined
	    || !src->info.viewport_active || !dst->info.viewport_active
	    || !translate(src, source_area, &from))
		return 0;
	source_view = viewport_absolute(src);
	destination_view = viewport_absolute(dst);
	/* Source clipping translates the matching destination, preserving the
	 * requested origin rather than shifting surviving pixels to the corner. */
	x = (int64_t)destination_view.x0 + destination_x
	    + ((int64_t)from.x0 - source_view.x0)
	    - (source_area == NULL ? 0 : source_area->x0);
	y = (int64_t)destination_view.y0 + destination_y
	    + ((int64_t)from.y0 - source_view.y0)
	    - (source_area == NULL ? 0 : source_area->y0);
	/* A visible source span is at most INT32_MAX pixels. Starting beyond
	 * either int32_t limit therefore cannot intersect a nonnegative surface. */
	if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
		return 0;
	return st_rip3_surface_blit(port_surface(ports, destination),
	    &destination_view, (int32_t)x, (int32_t)y,
	    port_surface(ports, source), NULL, &from, mode);
}

static int
clipboard_padding(struct st_rip3_surface *surface,const struct st_rip3_rect *visible,
    int64_t x0,int64_t y0,int64_t x1,int64_t y1)
{
	if (x0<visible->x0) x0=visible->x0;
	if (y0<visible->y0) y0=visible->y0;
	if (x1>visible->x1) x1=visible->x1;
	if (y1>visible->y1) y1=visible->y1;
	if (x0>x1 || y0>y1) return 0;
	const struct st_rip3_rect area={(int32_t)x0,(int32_t)y0,(int32_t)x1,(int32_t)y1};
	return st_rip3_surface_fill(surface,NULL,&area,0,ST_RIP3_COPY);
}

static int
clipboard_blit(struct st_rip3_ports *ports,unsigned source,int32_t x,int32_t y,const struct st_rip3_rect *face)
{
	struct st_rip3_port_info info;
	int status=st_rip3_ports_info(ports,source,&info);
	if (status!=0) return status;
	if (!info.defined || !rect_valid(&info.viewport) || !rect_valid(&info.clip)) return IO_ERROR_NOT_FOUND;
	if (face!=NULL && ((int64_t)info.clip.x0 >= (int64_t)face->x1-face->x0
	    || (int64_t)info.clip.y0 >= (int64_t)face->y1-face->y0)) return IO_ERROR_NOT_FOUND;
	const struct port *dst=&ports->entries[ports->current];
	if (!dst->info.viewport_active) return 0;
	struct st_rip3_rect from=viewport_absolute(&ports->entries[source]);
	struct st_rip3_rect to=viewport_absolute(dst),visible=to;
	struct st_rip3_surface *surface=port_surface(ports,ports->current);
	uint32_t width,height;
	status=st_rip3_surface_size(surface,&width,&height);
	if (status!=0) return status;
	const struct st_rip3_rect physical={0,0,(int32_t)width-1,(int32_t)height-1};
	if (!clip(&visible,&physical)) return 0;
	int64_t ox=(int64_t)to.x0+x,oy=(int64_t)to.y0+y;
	if (ox>INT32_MAX || oy>INT32_MAX || ox<INT32_MIN || oy<INT32_MIN) return 0;
	if (face!=NULL) {
		int64_t right=ox+(int64_t)face->x1-face->x0-1,bottom=oy+(int64_t)face->y1-face->y0-1;
		const struct st_rip3_rect limited={(int32_t)ox,(int32_t)oy,
		    right>INT32_MAX ? INT32_MAX : (int32_t)right,bottom>INT32_MAX ? INT32_MAX : (int32_t)bottom};
		if (!clip(&visible,&limited)) return 0;
	}
	/* Copy first, using the existing overlap snapshot. Padding before this
	 * transfer could corrupt a shared screen/source or self-aliased bitmap. */
	status=st_rip3_surface_blit(surface,&visible,(int32_t)ox,(int32_t)oy,
	    port_surface(ports,source),NULL,&from,ST_RIP3_TRANSFER_COPY);
	if (status!=0) return status;
	int64_t right=ox+(int64_t)from.x1-from.x0,bottom=oy+(int64_t)from.y1-from.y0;
	if (face!=NULL) {right=ox+(int64_t)face->x1-face->x0-1;bottom=oy+(int64_t)face->y1-face->y0-1;}
	int64_t left=ox+info.clip.x0,top=oy+info.clip.y0;
	int64_t backed_right=ox+info.clip.x1,backed_bottom=oy+info.clip.y1;
	status=clipboard_padding(surface,&visible,ox,oy,right,top-1);
	if (status==0) status=clipboard_padding(surface,&visible,ox,backed_bottom+1,right,bottom);
	if (status==0) status=clipboard_padding(surface,&visible,ox,top,left-1,backed_bottom);
	if (status==0) status=clipboard_padding(surface,&visible,backed_right+1,top,right,backed_bottom);
	return status;
}

int
st_rip3_ports_clipboard_blit(struct st_rip3_ports *ports,unsigned source,int32_t x,int32_t y)
{
	return clipboard_blit(ports,source,x,y,NULL);
}

int
st_rip3_ports_button_clipboard(struct st_rip3_ports *ports,unsigned source,const struct st_rip3_rect *face)
{
	if (face==NULL || face->x1<=face->x0 || face->y1<=face->y0) return IO_ERROR_INVALID_ARGUMENT;
	return clipboard_blit(ports,source,face->x0,face->y0,face);
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
st_rip3_ports_read_viewport(const struct st_rip3_ports *ports, unsigned number,
    int32_t x, int32_t y, uint8_t *result)
{
	struct st_rip3_port_info info;
	int status;

	if (ports == NULL || result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_info(ports,number,&info);
	if (status != 0) return status;
	if (!info.defined) return IO_ERROR_NOT_FOUND;
	if (x < info.clip.x0 || y < info.clip.y0 || x > info.clip.x1 || y > info.clip.y1)
		return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_rect view = viewport_absolute(&ports->entries[number]);
	return st_rip3_surface_get(port_surface(ports,number),view.x0+x,view.y0+y,result);
}

int
st_rip3_ports_snapshot(const struct st_rip3_ports *ports,
    const struct st_rip3_palette *palette, struct st_pixel_buffer **result)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return st_rip3_surface_snapshot(ports->screen, palette, result);
}

int
st_rip3_ports_screen_clone(const struct st_rip3_ports *ports,
    struct st_rip3_surface **result)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	return st_rip3_surface_clone(result, ports->screen);
}

int
st_rip3_ports_screen_restore(struct st_rip3_ports *ports,
    const struct st_rip3_surface *snapshot)
{
	struct st_rip3_surface *pixels;
	uint32_t width, height;
	int status;

	if (ports == NULL || snapshot == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_surface_size(snapshot, &width, &height);
	if (status != 0)
		return status;
	if (width != (uint32_t)ports->screen_bounds.x1 + 1
	    || height != (uint32_t)ports->screen_bounds.y1 + 1)
		return IO_ERROR_CONFLICT;
	if (!generations_available(ports,36)) return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_surface_clone(&pixels, snapshot);
	if (status != 0)
		return status;
	st_rip3_surface_destroy(ports->screen);
	ports->screen = pixels;
	for (unsigned i=0;i<36;i++) if (ports->entries[i].info.defined && !ports->entries[i].info.offscreen)
		ports->generations[i]=++ports->sequence;
	(void)st_rip3_surface_damage(pixels,ports->damage,ports->damage_length);
	if (ports->damage!=NULL) {
		for (size_t i=0; i<(size_t)width*height; i++) ports->damage[i]=1;
	}
	return 0;
}

int
st_rip3_ports_screen_clear_black(struct st_rip3_ports *ports)
{
	return ports==NULL ? IO_ERROR_INVALID_ARGUMENT : st_rip3_surface_clear_black(ports->screen);
}

int
st_rip3_ports_invert_rgb(struct st_rip3_ports *ports,const struct st_rip3_rect *area,
    const struct st_rip3_palette *palette)
{
	if (ports==NULL || area==NULL) return IO_ERROR_INVALID_ARGUMENT;
	const struct port *port=&ports->entries[ports->current];struct st_rip3_rect visible;
	if (!port->info.viewport_active || !translate(port,area,&visible)) return 0;
	return st_rip3_surface_invert_rgb(port_surface(ports,ports->current),NULL,&visible,palette);
}

int
st_rip3_ports_button_context(struct st_rip3_ports *ports,unsigned number,uint64_t generation,
    const struct st_rip3_port_info *view,int (*draw)(void *context),void *context)
{
	if (ports==NULL || number>=36 || view==NULL || draw==NULL || !view->defined) return IO_ERROR_INVALID_ARGUMENT;
	if (generation==0 || st_rip3_ports_generation(ports,number)!=generation) return IO_ERROR_CONFLICT;
	struct st_rip3_port_info saved=ports->entries[number].info;unsigned current=ports->current;
	uint64_t selected=st_rip3_ports_generation(ports,current);
	ports->entries[number].info=*view;ports->current=number;
	int status=draw(context);
	if (st_rip3_ports_generation(ports,number)==generation) ports->entries[number].info=saved;
	if (st_rip3_ports_generation(ports,current)==selected) ports->current=current;
	return status;
}

int
st_rip3_ports_button_view(struct st_rip3_ports *ports,const struct st_rip3_port_info *view,
    int (*draw)(void *context),void *context)
{
	if (ports==NULL || view==NULL || draw==NULL || !view->defined || view->offscreen) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info saved=ports->entries[0].info;unsigned current=ports->current;
	ports->entries[0].info=*view;ports->current=0;
	int status=draw(context);
	ports->entries[0].info=saved;ports->current=current;
	return status;
}

int
st_rip3_ports_screen_fill(struct st_rip3_ports *ports,
    const struct st_rip3_rect *area, uint8_t color)
{
	if (ports==NULL) return IO_ERROR_INVALID_ARGUMENT;
	return st_rip3_surface_fill(ports->screen,NULL,area,color,ST_RIP3_COPY);
}

int
st_rip3_ports_screen_pattern(struct st_rip3_ports *ports,
    const struct st_rip3_rect *area, const uint8_t rows[8], uint8_t foreground, uint8_t background)
{
	if (ports==NULL || rows==NULL || !rect_valid(area)) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_rect visible=*area;
	if (!clip(&visible,&ports->screen_bounds)) return 0;
	for (int32_t y=visible.y0;y<=visible.y1;y++) {
		int32_t start=visible.x0;
		while (start<=visible.x1) {
			bool on=(rows[(unsigned)y&7]&(UINT8_C(0x80)>>((unsigned)start&7)))!=0;
			int32_t end=start;
			/* Coalesce equal mask bits before the existing surface kernel;
			 * solid rows take one fill rather than one port lookup per pixel. */
			while (end<visible.x1 && ((rows[(unsigned)y&7]&(UINT8_C(0x80)>>((unsigned)(end+1)&7)))!=0)==on) end++;
			const struct st_rip3_rect span={start,y,end,y};
			int status=st_rip3_surface_fill(ports->screen,NULL,&span,on ? foreground : background,ST_RIP3_COPY);
			if (status!=0) return status;
			start=end+1;
		}
	}
	return 0;
}

int
st_rip3_ports_screen_damage(struct st_rip3_ports *ports, uint8_t *damage, size_t length)
{
	if (ports==NULL) return IO_ERROR_INVALID_ARGUMENT;
	int status=st_rip3_surface_damage(ports->screen,damage,length);
	if (status==0) {ports->damage=damage;ports->damage_length=length;}
	return status;
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
	/* The original offscreen save path has no cursor record. */
	if (source->info.offscreen) value.info.cursor = (struct st_rip3_cursor){0};

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
	if (!generations_available(ports,36)) return IO_ERROR_OUT_OF_RANGE;
	/* No live entry or screen pixel changes until every owned bitmap has
	 * been copied. Screen ports have definitions only, never a pixel copy. */
	for (size_t i = 0; i < 36; i++) {
		status = clone_entry(&entries[i], &snapshot->entries[i]);
		if (status != 0) {
			for (size_t j = 0; j < i; j++)
				clear_port(&entries[j]);
			return status;
		}
		/* Shipping restore reads saved screen cursor bytes but never copies
		 * them back. Retain live cursor state by port, including offscreen. */
		entries[i].info.cursor = ports->entries[i].info.cursor;
	}
	for (size_t i = 0; i < 36; i++) {
		clear_port(&ports->entries[i]);
		ports->entries[i] = entries[i];
		ports->generations[i]=++ports->sequence;
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
