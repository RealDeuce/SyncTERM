/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_ports.h"
#include "st_rip3_backup.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_resident_query.h"
#include "st_pixels.h"

#include "io_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct port {
	struct st_rip3_port_info info;
	struct st_rip3_port_copy_pair pair;
	struct st_rip3_resident_query query;
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
	struct st_rip3_clipboard_selection *clipboard;
	void *screen_reader_context;
	int (*screen_reader)(void *,const struct st_rip3_rect *,struct st_pixel_buffer *);
	void *screen_writer_context;
	int (*screen_writer)(void *,const struct st_rip3_rect *,struct st_pixel_buffer *,
	    const struct st_pixel_mask *,const struct st_rip3_palette *);
	struct port entries[36];
	struct st_rip3_resident_query floating_query;
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
	st_rip3_resident_query_clear(&port->query);
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
	for (unsigned i = 0; i < 36; i++)
		clear_port(&ports->entries[i]);
	st_rip3_resident_query_clear(&ports->floating_query);
	st_rip3_surface_destroy(ports->screen);
	free(ports);
}

void
st_rip3_ports_bind_clipboard(struct st_rip3_ports *ports,struct st_rip3_clipboard_selection *selection)
{
	if (ports!=NULL) ports->clipboard=selection;
}

int
st_rip3_ports_clipboard_index(const struct st_rip3_ports *ports)
{
	return ports != NULL && ports->clipboard != NULL && ports->clipboard->set
	    ? (int)ports->clipboard->port : -1;
}

int
st_rip3_ports_bitmap_pixels(const struct st_rip3_ports *ports, unsigned excluding, uint64_t *result)
{
	if (ports == NULL || result == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (excluding >= 36) return IO_ERROR_OUT_OF_RANGE;
	uint64_t used = 0;
	for (unsigned i = 1; i < 36; ++i) {
		const struct st_rip3_port_info *info = &ports->entries[i].info;
		if (i == excluding || !info->defined || !info->offscreen) continue;
		uint64_t width = (uint64_t)((int64_t)info->bounds.x1 - info->bounds.x0 + 1);
		uint64_t height = (uint64_t)((int64_t)info->bounds.y1 - info->bounds.y0 + 1);
		uint64_t pixels = width * height;
		if (pixels > UINT64_MAX - used) return IO_ERROR_OUT_OF_MEMORY;
		used += pixels;
	}
	*result = used;
	return 0;
}

void
st_rip3_ports_bind_screen_reader(struct st_rip3_ports *ports,void *context,
    int (*read)(void *,const struct st_rip3_rect *,struct st_pixel_buffer *))
{
	if (ports!=NULL) {ports->screen_reader_context=context;ports->screen_reader=read;}
}

void
st_rip3_ports_bind_screen_writer(struct st_rip3_ports *ports,void *context,
    int (*write)(void *,const struct st_rip3_rect *,struct st_pixel_buffer *,
        const struct st_pixel_mask *,const struct st_rip3_palette *))
{
	if (ports!=NULL) {ports->screen_writer_context=context;ports->screen_writer=write;}
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

static void
delete_entry(struct st_rip3_ports *ports, unsigned number)
{
	clear_port(&ports->entries[number]);
	ports->generations[number] = ++ports->sequence;
	if (ports->clipboard != NULL && ports->clipboard->set && ports->clipboard->port == number)
		*ports->clipboard = (struct st_rip3_clipboard_selection){0};
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
		delete_entry(ports, number);
	}
	else {
		for (unsigned i = 1; i < 36; i++) {
			if (!ports->entries[i].info.protected)
				delete_entry(ports, i);
		}
	}
	if (!ports->entries[ports->current].info.defined)
		ports->current = 0;
	return 0;
}

int
st_rip3_ports_reset_entry(struct st_rip3_ports *ports, unsigned number, uint8_t background)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (number >= 36)
		return IO_ERROR_OUT_OF_RANGE;
	struct port *entry = &ports->entries[number];
	if (!entry->info.defined || entry->info.protected)
		return 0;
	if (number != 0)
		return st_rip3_ports_delete(ports, number);

	st_rip3_resident_query_clear(&entry->query);
	entry->info.viewport = port_bounds(entry);
	entry->info.viewport_active = true;
	struct st_rip3_rect view = viewport_absolute(entry);
	return st_rip3_surface_fill(ports->screen, NULL, &view, background, ST_RIP3_COPY);
}

int
st_rip3_ports_reset(struct st_rip3_ports *ports, bool hard)
{
	if (ports == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	ports->entries[ports->current].info.image = image_defaults;
	if (!generations_available(ports,36)) return IO_ERROR_OUT_OF_RANGE;
	for (unsigned i = 1; i < 36; i++) {
		if (hard || !ports->entries[i].info.protected)
			delete_entry(ports, i);
	}
	if (hard && ports->clipboard!=NULL) *ports->clipboard=(struct st_rip3_clipboard_selection){0};
	ports->generations[0]=++ports->sequence;
	st_rip3_resident_query_clear(&ports->entries[0].query);
	st_rip3_resident_query_clear(&ports->floating_query);
	ports->entries[0].info = (struct st_rip3_port_info){
		.defined = true,
		.viewport_active = true,
		.bounds = ports->screen_bounds,
		.viewport = ports->screen_bounds,
		.image = image_defaults,
	};
	ports->entries[0].pair=(struct st_rip3_port_copy_pair){0};
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
st_rip3_ports_header_prune(struct st_rip3_ports *ports, bool keep_current)
{
	if (!generations_available(ports, 36))
		return IO_ERROR_OUT_OF_RANGE;
	unsigned keep = keep_current ? ports->current : 0;
	for (unsigned i = 1; i < 36; ++i) {
		if (i != keep && !ports->entries[i].info.protected)
			delete_entry(ports, i);
	}
	if (!keep_current) ports->current = 0;
	if (!keep_current || keep != 0) {
		struct port *screen = &ports->entries[0];
		screen->info.viewport = screen->info.bounds;
		screen->info.cursor = (struct st_rip3_cursor){0};
		screen->info.image = image_defaults;
		screen->pair = (struct st_rip3_port_copy_pair){0};
	}
	return 0;
}

int
st_rip3_ports_header_delete_clipboard(struct st_rip3_ports *ports)
{
	if (ports == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (ports->clipboard == NULL || !ports->clipboard->set) return 0;
	unsigned number = ports->clipboard->port;
	int status = 0;
	if (number < 36 && ports->entries[number].info.defined) {
		if (ports->entries[number].info.protected) return 0;
		/* Ordinary wire delete(0) is a table-wide operation. Here the
		 * physical target is one slot, so a zero assignment cannot delete it. */
		status = number == 0 ? IO_ERROR_PERMISSION_DENIED : st_rip3_ports_delete(ports, number);
	}
	*ports->clipboard = (struct st_rip3_clipboard_selection){0};
	return status;
}

void
st_rip3_ports_header_clear_queries(struct st_rip3_ports *ports)
{
	st_rip3_ports_clear_query(ports, true, 0);
	for (unsigned i = 0; i < 36; ++i)
		st_rip3_ports_clear_query(ports, false, i);
}

int
st_rip3_ports_operate_views(struct st_rip3_ports *ports,
    const unsigned *numbers, size_t count, enum st_rip3_view_operation operation)
{
	if (ports == NULL || (count != 0 && numbers == NULL)
	    || operation < ST_RIP3_VIEW_ACTIVATE || operation > ST_RIP3_PORT_CLEAR_BLACK)
		return IO_ERROR_INVALID_ARGUMENT;
	for (size_t i = 0; i < count; i++) {
		if (numbers[i] >= 36)
			return IO_ERROR_OUT_OF_RANGE;
	}
	for (size_t i = 0; i < count; i++) {
		unsigned number = numbers[i];
		struct port *port = &ports->entries[number];

		if (!port->info.defined)
			continue;
		if (operation == ST_RIP3_PORT_CLEAR_BLACK) {
			if (!port->info.viewport_active)
				continue;
			int status = st_rip3_surface_clear_black(port_surface(ports, number), &port->info.bounds);
			if (status != 0)
				return status;
		}
		else if (!port->info.protected) {
			if (operation == ST_RIP3_VIEW_MAXIMIZE || operation == ST_RIP3_VIEW_RESET) {
				if (port->info.viewport_active || operation == ST_RIP3_VIEW_RESET)
					port->info.viewport = port_bounds(port);
				if (operation == ST_RIP3_VIEW_RESET) port->info.viewport_active=true;
			}
			else
				port->info.viewport_active = operation == ST_RIP3_VIEW_ACTIVATE;
		}
	}
	return 0;
}

static void
publish_copy(struct st_rip3_ports *ports,unsigned source,const struct st_rip3_rect *from,
    unsigned destination,const struct st_rip3_rect *to)
{
	struct st_rip3_rect sv=viewport_absolute(&ports->entries[source]);
	struct st_rip3_rect dv=viewport_absolute(&ports->entries[destination]);
	ports->entries[destination].pair=(struct st_rip3_port_copy_pair){.set=true,
	    .source={(int64_t)from->x0-sv.x0,(int64_t)from->y0-sv.y0,
	        (int64_t)from->x1-sv.x0+1,(int64_t)from->y1-sv.y0+1},
	    .destination={(int64_t)to->x0-dv.x0,(int64_t)to->y0-dv.y0,
	        (int64_t)to->x1-dv.x0+1,(int64_t)to->y1-dv.y0+1}};
}

static int
copy_native_screen(struct st_rip3_ports *ports,const struct st_rip3_rect *from,
    unsigned destination,const struct st_rip3_rect *to,enum st_rip3_transfer_mode mode,
    const struct st_rip3_palette *operand_palette)
{
	struct st_rip3_rect source=*from,target=*to;
	struct st_rip3_rect target_bounds=ports->entries[destination].info.offscreen
	    ? port_bounds(&ports->entries[destination]) : ports->screen_bounds;
	if (!clip(&source,&ports->screen_bounds) || !clip(&target,&target_bounds)) return 0;
	struct st_pixel_buffer *snapshot=NULL;
	int status=st_rip3_surface_capture_area_raw(ports->screen,&source,&snapshot);
	if (status==0 && snapshot!=NULL) status=ports->screen_reader(ports->screen_reader_context,&source,snapshot);
	/* Absolute logical source starts are nonnegative, so physical clipping
	 * only removes far edges. Retain the original logical span for sampling. */
	const struct st_rip3_rect local={0,0,from->x1-from->x0,from->y1-from->y0};
	if (status == 0 && snapshot != NULL)
		status = st_rip3_surface_copy_from_raw(port_surface(ports,destination), to,
		    snapshot, &local, mode, operand_palette);
	st_pixel_buffer_destroy(snapshot);return status;
}

struct blit_patch {
	struct st_rip3_rect area;
	struct st_pixel_buffer *pixels;
};

static int
blit_capture(const struct st_rip3_ports *ports,const struct st_rip3_surface *surface,
    const struct st_rip3_rect *area,bool screen,bool alternate,struct st_pixel_buffer **result)
{
	int status=st_rip3_surface_capture_area_raw(surface,area,result);
	if (status!=0 || *result==NULL || !screen || ports->screen_reader==NULL) return status;
	if (alternate && ports->screen_writer!=NULL) status=st_pixel_buffer_ensure_alternate(*result);
	if (status==0) status=ports->screen_reader(ports->screen_reader_context,area,*result);
	return status;
}

static int
store_pixels(struct st_rip3_ports *ports, struct st_rip3_surface *surface,
    const struct st_rip3_rect *area, bool screen, struct st_pixel_buffer *pixels,
    const struct st_pixel_mask *mask, const struct st_rip3_palette *palette)
{
	int status = st_rip3_surface_restore_area_raw(surface, pixels, mask,
	    (uint32_t)area->x0, (uint32_t)area->y0);
	if (status == 0 && screen && ports->screen_writer != NULL && palette != NULL)
		status = ports->screen_writer(ports->screen_writer_context, area, pixels, mask, palette);
	return status;
}

static void
scroll_corner(const struct st_rip3_ports *ports,const struct st_rip3_surface *surface,
    const struct st_rip3_rect *bounds,bool screen,const struct st_rip3_surface_blit_area *source,
    bool down,uint32_t color[2])
{
	int64_t x=down ? source->x0 : source->x1;
	int64_t y=down ? source->y0 : source->y1;
	if (x<bounds->x0 || x>bounds->x1 || y<bounds->y0 || y>bounds->y1) return;
	uint32_t primary;
	if (st_rip3_surface_read_color(surface,(int32_t)x,(int32_t)y,&primary)!=0) return;
	uint32_t alternate=primary;
	if (screen && ports->screen_reader!=NULL) {
		const struct st_rip3_rect point={(int32_t)x,(int32_t)y,(int32_t)x,(int32_t)y};
		struct st_pixel_buffer sample={.width=1,.height=1,.primary=&primary,.alternate=&alternate};
		if (ports->screen_reader(ports->screen_reader_context,&point,&sample)!=0) return;
	}
	color[0]=primary;color[1]=alternate;
}

static int
blit_fill(struct blit_patch *patch,const struct st_rip3_surface_blit_fill *fill,
    const uint32_t ink[2],bool alternate)
{
	const struct st_rip3_rect *a=&patch->area;
	uint32_t width=(uint32_t)(a->x1-a->x0)+1,height=(uint32_t)(a->y1-a->y0)+1;
	int status=st_pixel_buffer_create(&patch->pixels,width,height,alternate);
	if (status!=0) return status;
	for (uint32_t y=0;y<height;y++) for (uint32_t x=0;x<width;x++) {
		size_t at=(size_t)y*width+x;
		bool background=fill->mode==ST_RIP3_BLIT_PATTERN
		    && (fill->pattern[((uint32_t)a->y0+y)&7] & (0x80u>>(((uint32_t)a->x0+x)&7)))==0;
		patch->pixels->primary[at]=background ? fill->background : ink[0];
		if (alternate) patch->pixels->alternate[at]=background ? fill->background : ink[1];
	}
	return 0;
}

int
st_rip3_ports_blit_surface(struct st_rip3_ports *ports,const struct st_rip3_surface_blit_area *source,
    const struct st_rip3_surface_blit_fill *fill,const struct st_rip3_palette *palette)
{
	if (ports==NULL || source==NULL || fill==NULL || palette==NULL || source->x0<0 || source->y0<0
	    || source->x1<source->x0 || source->y1<source->y0 || source->destination_x<0 || source->destination_y<0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (source->x1>INT64_MAX/2 || source->y1>INT64_MAX/2
	    || source->destination_x>INT64_MAX/2 || source->destination_y>INT64_MAX/2) return IO_ERROR_OUT_OF_RANGE;
	int64_t dx=source->destination_x-source->x0,dy=source->destination_y-source->y0;
	if (fill->mode==ST_RIP3_BLIT_SCROLL_CORNER && (dx!=0 || dy==0)) return IO_ERROR_INVALID_ARGUMENT;
	if (dx==0 && dy==0) return 0;
	const struct port *port=&ports->entries[ports->current];
	if (!port->info.defined) return IO_ERROR_NOT_FOUND;
	if (!port->info.viewport_active) return 0;
	bool screen=!port->info.offscreen;
	struct st_rip3_rect bounds=port->info.bounds;
	if (screen && !clip(&bounds,&ports->screen_bounds)) return 0;
	if (source->x0>bounds.x1 || source->x1<bounds.x0 || source->y0>bounds.y1 || source->y1<bounds.y0) return 0;
	const struct st_rip3_rect available={
	    source->x0<bounds.x0 ? bounds.x0 : (int32_t)source->x0,
	    source->y0<bounds.y0 ? bounds.y0 : (int32_t)source->y0,
	    source->x1>bounds.x1 ? bounds.x1 : (int32_t)source->x1,
	    source->y1>bounds.y1 ? bounds.y1 : (int32_t)source->y1};
	/* Intersect the available source with the destination's bounds shifted
	 * back by the copy delta. Both sides lose the same pixels. */
	int64_t x0=(int64_t)bounds.x0-dx,x1=(int64_t)bounds.x1-dx;
	int64_t y0=(int64_t)bounds.y0-dy,y1=(int64_t)bounds.y1-dy;
	if (x0<available.x0) x0=available.x0;
	if (x1>available.x1) x1=available.x1;
	if (y0<available.y0) y0=available.y0;
	if (y1>available.y1) y1=available.y1;
	struct st_rip3_surface *surface=port_surface(ports,ports->current);
	struct blit_patch patches[5]={{0}};
	size_t count=0;
	int status=0;
	if (x0<=x1 && y0<=y1) {
		const struct st_rip3_rect from={(int32_t)x0,(int32_t)y0,(int32_t)x1,(int32_t)y1};
		struct blit_patch *copy=&patches[count++];
		copy->area=(struct st_rip3_rect){(int32_t)(x0+dx),(int32_t)(y0+dy),(int32_t)(x1+dx),(int32_t)(y1+dy)};
		status=blit_capture(ports,surface,&from,screen,true,&copy->pixels);
	}
	size_t copies=count;
	if (status==0 && fill->mode!=ST_RIP3_BLIT_KEEP) {
		struct st_rip3_rect covered=count==0 ? (struct st_rip3_rect){0,0,-1,-1} : patches[0].area;
		if (!clip(&covered,&available)) patches[count++].area=available;
		else {
			if (available.y0<covered.y0)
				patches[count++].area=(struct st_rip3_rect){available.x0,available.y0,available.x1,covered.y0-1};
			if (covered.y1<available.y1)
				patches[count++].area=(struct st_rip3_rect){available.x0,covered.y1+1,available.x1,available.y1};
			if (available.x0<covered.x0)
				patches[count++].area=(struct st_rip3_rect){available.x0,covered.y0,covered.x0-1,covered.y1};
			if (covered.x1<available.x1)
				patches[count++].area=(struct st_rip3_rect){covered.x1+1,covered.y0,available.x1,covered.y1};
		}
	}
	if (status==0 && count>copies) {
		uint32_t ink[2]={fill->ink,fill->ink};
		if (fill->mode==ST_RIP3_BLIT_SCROLL_CORNER) scroll_corner(ports,surface,&bounds,screen,source,dy>0,ink);
		for (size_t i=copies;status==0 && i<count;i++)
			status=blit_fill(&patches[i],fill,ink,screen && ports->screen_writer!=NULL);
	}
	/* All source reads and scratch allocations finish before the first
	 * write, including a corner that will itself be replaced. */
	for (size_t i=0;status==0 && i<count;i++) {
		struct blit_patch *patch=&patches[i];
		status = store_pixels(ports, surface, &patch->area, screen, patch->pixels, NULL, palette);
	}
	for (size_t i=0;i<count;i++) st_pixel_buffer_destroy(patches[i].pixels);
	return status;
}

int
st_rip3_ports_copy_pair(const struct st_rip3_ports *ports,unsigned number,
    struct st_rip3_port_copy_pair *result)
{
	if (ports==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	*result=ports->entries[number].pair;return 0;
}

/* The projected kernel samples at pixel centers. Sample i is available while
 * (2*i+1)*source < 2*destination*physical_source. Port bounds are nonnegative
 * and the span of either axis is at most 2^31. */
static uint64_t
projected_coverage(uint64_t source, uint64_t destination, uint64_t physical_source)
{
	return (2*destination*physical_source + source - 1)/(2*source);
}

static int
compose_screen_transfer(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, const struct st_rip3_rect *destination_area,
    enum st_rip3_transfer_mode mode, const struct st_rip3_palette *palette)
{
	bool source_on_screen = !ports->entries[source].info.offscreen;
	struct st_rip3_rect source_bounds = source_on_screen
	    ? ports->screen_bounds : port_bounds(&ports->entries[source]);
	struct st_rip3_rect from = *source_area, to = *destination_area;
	if (!clip(&from, &source_bounds) || !clip(&to, &ports->screen_bounds))
		return 0;
	/* Preserve the requested fit after clipping. Only capture and publish
	 * the part of the destination whose source samples are physically present. */
	const struct st_rip3_rect logical_source = {0,0,source_area->x1-source_area->x0,source_area->y1-source_area->y0};
	const struct st_rip3_rect logical_target = {0,0,destination_area->x1-destination_area->x0,destination_area->y1-destination_area->y0};
	uint64_t width = projected_coverage((uint64_t)logical_source.x1+1,
	    (uint64_t)logical_target.x1+1, (uint64_t)(from.x1-from.x0)+1);
	uint64_t height = projected_coverage((uint64_t)logical_source.y1+1,
	    (uint64_t)logical_target.y1+1, (uint64_t)(from.y1-from.y0)+1);
	if (width == 0 || height == 0)
		return 0;
	if (width < (uint64_t)(to.x1-to.x0)+1)
		to.x1 = to.x0 + (int32_t)(width-1);
	if (height < (uint64_t)(to.y1-to.y0)+1)
		to.y1 = to.y0 + (int32_t)(height-1);
	if (logical_source.x1 == logical_target.x1)
		from.x1 = from.x0 + (to.x1-to.x0);
	if (logical_source.y1 == logical_target.y1)
		from.y1 = from.y0 + (to.y1-to.y0);

	struct st_pixel_buffer *source_pixels = NULL, *destination_pixels = NULL;
	int status = blit_capture(ports, port_surface(ports, source), &from, source_on_screen, true, &source_pixels);
	if (status == 0)
		status = blit_capture(ports, ports->screen, &to, true, true, &destination_pixels);
	if (status == 0 && source_pixels != NULL && destination_pixels != NULL) {
		status = st_rip3_surface_copy_snapshots(destination_pixels, &logical_target,
		    source_pixels, &logical_source, mode, palette);
		if (status == 0)
			status = store_pixels(ports, ports->screen, &to, true, destination_pixels, NULL, palette);
	}
	st_pixel_buffer_destroy(source_pixels);
	st_pixel_buffer_destroy(destination_pixels);
	return status;
}

static int
copy_port_area(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, unsigned destination,
    const struct st_rip3_rect *destination_area, enum st_rip3_transfer_mode mode,
    const struct st_rip3_palette *palette)
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
	int status;
	if (!dst->info.offscreen && ports->screen_reader != NULL
	    && mode >= ST_RIP3_TRANSFER_XOR && mode <= ST_RIP3_TRANSFER_AND)
		status = compose_screen_transfer(ports, source, &from, &to, mode, palette);
	else if (!src->info.offscreen && ports->screen_reader != NULL)
		status = copy_native_screen(ports, &from, destination, &to, mode, palette);
	else
		status = st_rip3_surface_copy_projected_colors(port_surface(ports, destination), &to,
		    port_surface(ports, source), &from, mode, palette);
	/* Logical success publishes even when physical storage clips every sample.
	 * A failed overlap snapshot or transfer leaves the old pair unchanged. */
	if (status==0) publish_copy(ports,source,&from,destination,&to);
	return status;
}

int
st_rip3_ports_copy(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, unsigned destination,
    const struct st_rip3_rect *destination_area, enum st_rip3_transfer_mode mode)
{
	return copy_port_area(ports, source, source_area, destination, destination_area, mode, NULL);
}

static bool
trim_copy_extent(struct st_rip3_copy_extent *area,struct st_rip3_copy_extent *other,
    int64_t width,int64_t height)
{
	if (area->x0>=width || area->y0>=height) return false;
	if (area->x1>width) {
		if (other!=NULL) other->x1-=area->x1-width;
		area->x1=width;
	}
	if (area->y1>height) {
		if (other!=NULL) other->y1-=area->y1-height;
		area->y1=height;
	}
	return area->x0<area->x1 && area->y0<area->y1;
}

int
st_rip3_ports_copy_device(struct st_rip3_ports *ports,unsigned source,
    const struct st_rip3_copy_extent *source_area,unsigned destination,
    const struct st_rip3_copy_extent *destination_area, enum st_rip3_transfer_mode mode,
    const struct st_rip3_palette *palette)
{
	if (ports==NULL || source_area==NULL || destination_area==NULL
	    || source_area->x0<0 || source_area->y0<0 || destination_area->x0<0 || destination_area->y0<0
	    || source_area->x0>=source_area->x1 || source_area->y0>=source_area->y1
	    || destination_area->x0>=destination_area->x1 || destination_area->y0>=destination_area->y1
	    || mode<ST_RIP3_TRANSFER_COPY || mode>ST_RIP3_TRANSFER_INVERT) return IO_ERROR_INVALID_ARGUMENT;
	if (source>=36 || destination>=36) return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_port_info *src=&ports->entries[source].info,*dst=&ports->entries[destination].info;
	if (!src->defined || !dst->defined || !src->viewport_active || !dst->viewport_active) return 0;
	struct st_rip3_copy_extent from=*source_area,to=*destination_area;
	bool equal=from.x1-from.x0==to.x1-to.x0 && from.y1-from.y0==to.y1-to.y0;
	if (!trim_copy_extent(&from,equal ? &to : NULL,(int64_t)src->viewport.x1-src->viewport.x0+1,(int64_t)src->viewport.y1-src->viewport.y0+1)
	    || !trim_copy_extent(&to,equal ? &from : NULL,(int64_t)dst->viewport.x1-dst->viewport.x0+1,(int64_t)dst->viewport.y1-dst->viewport.y0+1)) return 0;
	/* Narrow only after logical viewport clipping. Physical clipping remains
	 * in the existing raster kernel and never refits or shrinks this pair. */
	const struct st_rip3_rect s={(int32_t)from.x0,(int32_t)from.y0,(int32_t)(from.x1-1),(int32_t)(from.y1-1)};
	const struct st_rip3_rect d={(int32_t)to.x0,(int32_t)to.y0,(int32_t)(to.x1-1),(int32_t)(to.y1-1)};
	return copy_port_area(ports, source, &s, destination, &d, mode, palette);
}

int
st_rip3_ports_paste(struct st_rip3_ports *ports,const unsigned *source)
{
	if (ports==NULL) return IO_ERROR_INVALID_ARGUMENT;
	unsigned number;
	if (source==NULL) {
		if (ports->clipboard==NULL || !ports->clipboard->set || ports->clipboard->port>=36) return 0;
		number=ports->clipboard->port;
	}
	else number=*source;
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	/* Copy values before a same-port operation can replace this owner's pair. */
	struct st_rip3_port_copy_pair pair=ports->entries[number].pair;
	if (!pair.set) return 0;
	return st_rip3_ports_copy_device(ports,number,&pair.destination,ports->current,&pair.source,ST_RIP3_TRANSFER_COPY,NULL);
}

static int
blit(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, unsigned destination,
    int32_t destination_x, int32_t destination_y,
    enum st_rip3_transfer_mode mode, bool read_inactive, const struct st_rip3_palette *palette)
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
	    || (!src->info.viewport_active && !read_inactive) || !dst->info.viewport_active
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
	/* Compute logical destination clipping separately from the physical blit.
	 * Both keep the original translation; only the logical pair is remembered. */
	int64_t left=x<destination_view.x0 ? destination_view.x0 : x;
	int64_t top=y<destination_view.y0 ? destination_view.y0 : y;
	int64_t right=x+(int64_t)from.x1-from.x0,bottom=y+(int64_t)from.y1-from.y0;
	if (right>destination_view.x1) right=destination_view.x1;
	if (bottom>destination_view.y1) bottom=destination_view.y1;
	if (left>right || top>bottom) return 0;
	struct st_rip3_rect logical_source={(int32_t)(from.x0+left-x),(int32_t)(from.y0+top-y),
	    (int32_t)(from.x0+right-x),(int32_t)(from.y0+bottom-y)};
	struct st_rip3_rect logical_destination={(int32_t)left,(int32_t)top,(int32_t)right,(int32_t)bottom};
	int status;
	if (read_inactive && !dst->info.offscreen && ports->screen_reader != NULL
	    && mode >= ST_RIP3_TRANSFER_XOR && mode <= ST_RIP3_TRANSFER_AND)
		status = compose_screen_transfer(ports, source, &logical_source, &logical_destination, mode, palette);
	else if (!src->info.offscreen && ports->screen_reader != NULL)
		status = copy_native_screen(ports, &logical_source, destination, &logical_destination, mode, palette);
	else if (palette != NULL)
		status = st_rip3_surface_copy_projected_colors(port_surface(ports, destination),
		    &logical_destination, port_surface(ports, source), &logical_source, mode, palette);
	else
		status = st_rip3_surface_blit(port_surface(ports, destination),
		    &destination_view, (int32_t)x, (int32_t)y,
		    port_surface(ports, source), NULL, &from, mode);
	if (status==0) publish_copy(ports,source,&logical_source,destination,&logical_destination);
	return status;
}

int
st_rip3_ports_blit(struct st_rip3_ports *ports, unsigned source,
    const struct st_rip3_rect *source_area, unsigned destination,
    int32_t x, int32_t y, enum st_rip3_transfer_mode mode)
{
	return blit(ports,source,source_area,destination,x,y,mode,false,NULL);
}

int
st_rip3_ports_put_image(struct st_rip3_ports *ports, unsigned source,
    unsigned destination, int32_t x, int32_t y, enum st_rip3_transfer_mode mode,
    const struct st_rip3_palette *palette)
{
	return blit(ports,source,NULL,destination,x,y,mode,true,palette);
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
st_rip3_ports_read_color(const struct st_rip3_ports *ports,unsigned number,int32_t x,int32_t y,bool viewport,uint32_t *result)
{
	if (ports==NULL || result==NULL) return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info info;int status=st_rip3_ports_info(ports,number,&info);
	if (status!=0) return status;if (!info.defined) return IO_ERROR_NOT_FOUND;
	if (viewport) {
		if (x<info.clip.x0 || y<info.clip.y0 || x>info.clip.x1 || y>info.clip.y1) return IO_ERROR_OUT_OF_RANGE;
		const struct st_rip3_rect view=viewport_absolute(&ports->entries[number]);
		return st_rip3_surface_read_color(port_surface(ports,number),view.x0+x,view.y0+y,result);
	}
	const struct st_rip3_rect bounds=port_bounds(&ports->entries[number]);
	if (x<0 || y<0 || x>bounds.x1 || y>bounds.y1) return IO_ERROR_OUT_OF_RANGE;
	return st_rip3_surface_read_color(port_surface(ports,number),x+info.bounds.x0,y+info.bounds.y0,result);
}

int
st_rip3_ports_capture_pixels(const struct st_rip3_ports *ports, unsigned number,
    const struct st_rip3_rect *area, bool viewport, bool alternate, struct st_pixel_buffer **result)
{
	if (!rect_valid(area) || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info info;
	int status = st_rip3_ports_info(ports, number, &info);
	if (status != 0) return status;
	if (!info.defined) return IO_ERROR_NOT_FOUND;
	const struct port *port = &ports->entries[number];
	struct st_rip3_rect bounds = viewport ? info.clip : port_bounds(port);
	if (area->x0 < bounds.x0 || area->y0 < bounds.y0 || area->x1 > bounds.x1 || area->y1 > bounds.y1)
		return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_rect origin = viewport ? viewport_absolute(port) : info.bounds;
	struct st_rip3_rect absolute = {origin.x0 + area->x0, origin.y0 + area->y0,
	    origin.x0 + area->x1, origin.y0 + area->y1};
	struct st_pixel_buffer *pixels = NULL;
	status = blit_capture(ports, port_surface(ports, number), &absolute, !info.offscreen, alternate, &pixels);
	if (status != 0) {
		st_pixel_buffer_destroy(pixels);
		return status;
	}
	if (pixels == NULL) return IO_ERROR_OUT_OF_RANGE;
	*result = pixels;
	return 0;
}

int
st_rip3_ports_write_viewport_pixels(struct st_rip3_ports *ports, unsigned number,
    int32_t x, int32_t y, struct st_pixel_buffer *pixels, const struct st_pixel_mask *mask,
    const struct st_rip3_palette *palette)
{
	if (pixels == NULL || pixels->width == 0 || pixels->height == 0)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_port_info info;
	int status = st_rip3_ports_info(ports, number, &info);
	if (status != 0 || !info.viewport_active)
		return status;
	int64_t right = (int64_t)x + pixels->width - 1, bottom = (int64_t)y + pixels->height - 1;
	if (x < info.clip.x0 || y < info.clip.y0 || right > info.clip.x1 || bottom > info.clip.y1)
		return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_rect view = viewport_absolute(&ports->entries[number]);
	const struct st_rip3_rect area = {view.x0+x, view.y0+y,
	    view.x0+(int32_t)right, view.y0+(int32_t)bottom};
	return store_pixels(ports, port_surface(ports, number), &area, !info.offscreen, pixels, mask, palette);
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
st_rip3_ports_screen_capture(const struct st_rip3_ports *ports,struct st_pixel_buffer **result)
{
	return ports==NULL ? IO_ERROR_INVALID_ARGUMENT : st_rip3_surface_capture_raw(ports->screen,result);
}

int
st_rip3_ports_screen_restore(struct st_rip3_ports *ports,const struct st_pixel_buffer *snapshot)
{
	return ports==NULL ? IO_ERROR_INVALID_ARGUMENT : st_rip3_surface_restore_raw(ports->screen,snapshot);
}

int
st_rip3_ports_screen_clear_black(struct st_rip3_ports *ports)
{
	return ports==NULL ? IO_ERROR_INVALID_ARGUMENT : st_rip3_surface_clear_black(ports->screen, NULL);
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
st_rip3_ports_draw_clipped(struct st_rip3_ports *ports, const struct st_rip3_rect *area,
    int (*draw)(void *, struct st_rip3_point), void *context)
{
	if (ports == NULL || !rect_valid(area) || draw == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	struct port *port = &ports->entries[ports->current];
	struct st_rip3_rect visible = *area;
	struct st_rip3_rect view = viewport_absolute(port);
	if (!port->info.viewport_active || !clip(&visible, &view))
		return 0;
	struct st_rip3_rect saved = port->info.viewport;
	const struct st_rip3_rect *bounds = &port->info.bounds;
	port->info.viewport = (struct st_rip3_rect){visible.x0 - bounds->x0, visible.y0 - bounds->y0,
	    visible.x1 - bounds->x0, visible.y1 - bounds->y0};
	int status = draw(context, (struct st_rip3_point){visible.x0, visible.y0});
	port->info.viewport = saved;
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
	struct st_rip3_clipboard_selection clipboard;
	struct st_rip3_rect screen_bounds;
	unsigned current;
	struct port entries[36];
	struct st_rip3_resident_query floating_query;
};

static void
port_backup_destroy(void *object)
{
	struct port_snapshot *snapshot = object;

	if (snapshot == NULL)
		return;
	for (size_t i = 0; i < 36; i++)
		clear_port(&snapshot->entries[i]);
	st_rip3_resident_query_clear(&snapshot->floating_query);
	free(snapshot);
}

static int
clone_entry(struct port *result, const struct port *source)
{
	struct port value = {.info = source->info,.pair=source->pair};
	int status;
	/* The original offscreen save path has no cursor record. */
	if (source->info.offscreen) value.info.cursor = (struct st_rip3_cursor){0};

	if (source->bitmap != NULL) {
		status = st_rip3_surface_clone(&value.bitmap, source->bitmap);
		if (status != 0)
			return status;
	}
	status=st_rip3_resident_query_copy(&value.query,&source->query);
	if (status!=0) {clear_port(&value);return status;}
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
	if (ports->clipboard!=NULL) snapshot->clipboard=*ports->clipboard;
	status=st_rip3_resident_query_copy(&snapshot->floating_query,&ports->floating_query);
	if (status!=0) {port_backup_destroy(snapshot);return status;}
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
	struct st_rip3_resident_query floating={0};
	int status;

	if (ports->screen_bounds.x0 != snapshot->screen_bounds.x0
	    || ports->screen_bounds.y0 != snapshot->screen_bounds.y0
	    || ports->screen_bounds.x1 != snapshot->screen_bounds.x1
	    || ports->screen_bounds.y1 != snapshot->screen_bounds.y1)
		return IO_ERROR_CONFLICT;
	if (snapshot->clipboard.set && ports->clipboard==NULL) return IO_ERROR_NOT_SUPPORTED;
	if (!generations_available(ports,36)) return IO_ERROR_OUT_OF_RANGE;
	status=st_rip3_resident_query_copy(&floating,&snapshot->floating_query);
	if (status!=0) return status;
	/* No live entry or screen pixel changes until every owned bitmap has
	 * been copied. Screen ports have definitions only, never a pixel copy. */
	for (size_t i = 0; i < 36; i++) {
		/* Initial cleanup skips protected live ports; undefined saved slots
		 * do not replace those survivors or invalidate their identity. */
		if (!snapshot->entries[i].info.defined && ports->entries[i].info.protected) continue;
		status = clone_entry(&entries[i], &snapshot->entries[i]);
		if (status != 0) {
			for (size_t j = 0; j < i; j++)
				clear_port(&entries[j]);
			st_rip3_resident_query_clear(&floating);
			return status;
		}
		/* Shipping restore reads saved screen cursor bytes but never copies
		 * them back. Retain live cursor state by port, including offscreen. */
		entries[i].info.cursor = ports->entries[i].info.cursor;
	}
	for (size_t i = 0; i < 36; i++) {
		if (!snapshot->entries[i].info.defined && ports->entries[i].info.protected) continue;
		clear_port(&ports->entries[i]);
		ports->entries[i] = entries[i];
		ports->generations[i]=++ports->sequence;
	}
	st_rip3_resident_query_clear(&ports->floating_query);ports->floating_query=floating;
	ports->current = snapshot->current;
	if (ports->clipboard!=NULL) *ports->clipboard=snapshot->clipboard;
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

void
st_rip3_ports_clear_query(struct st_rip3_ports *ports, bool floating, unsigned number)
{
	if (ports == NULL)
		return;
	if (floating)
		st_rip3_resident_query_clear(&ports->floating_query);
	else if (number < 36) {
		struct port *entry = &ports->entries[number];
		if (entry->info.defined && !entry->info.protected && !entry->info.offscreen)
			st_rip3_resident_query_clear(&entry->query);
	}
}

int
st_rip3_ports_define_query(struct st_rip3_ports *ports,bool floating,unsigned number,
    const uint8_t *bytes,size_t length,bool converted)
{
	if (ports==NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (floating) return st_rip3_resident_query_set(&ports->floating_query,bytes,length,converted);
	if (number>=36) return IO_ERROR_OUT_OF_RANGE;
	const struct st_rip3_port_info *info=&ports->entries[number].info;
	if (!info->defined) return IO_ERROR_NOT_FOUND;
	if (info->protected) return IO_ERROR_PERMISSION_DENIED;
	if (info->offscreen) return IO_ERROR_NOT_SUPPORTED;
	return st_rip3_resident_query_set(&ports->entries[number].query,bytes,length,converted);
}

const struct st_rip3_resident_query *
st_rip3_ports_query(const struct st_rip3_ports *ports,bool floating,unsigned number)
{
	if (ports==NULL) return NULL;
	if (floating) return &ports->floating_query;
	return number>=36 || !ports->entries[number].info.defined || ports->entries[number].info.offscreen
	    ? NULL : &ports->entries[number].query;
}
