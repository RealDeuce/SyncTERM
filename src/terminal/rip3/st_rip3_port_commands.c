/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io_error.h"
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"

static int
coordinates(struct st_rip3_rect *result, struct st_rip3_fields *fields, size_t width, unsigned radix, bool final)
{
	uint64_t values[4];
	int      status;

	for (size_t i = 0; i < 4; i++) {
		status = st_rip3_command_next_field(fields, values + i, width, radix, final && i == 3);
		if (status != 0)
			return status;
		if (values[i] > INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
	}
	*result =
	    (struct st_rip3_rect) {(int32_t)values[0], (int32_t)values[1], (int32_t)values[2], (int32_t)values[3]};
	return 0;
}

/* These trailing reserved fields take part in delimiter-time collection,
 * but the original dispatcher supplies zero regardless of their value. */
static int
reserved_field(struct st_rip3_fields *fields, size_t width, unsigned radix)
{
	uint64_t ignored;

	if (fields->remaining == 0)
		return 0;
	return st_rip3_command_next_field(fields, &ignored, width, radix, true);
}

static bool
ordered(const struct st_rip3_rect *area)
{
	return area->x0 <= area->x1 && area->y0 <= area->y1;
}

static bool
all_zero(const struct st_rip3_rect *area)
{
	return area->x0 == 0 && area->y0 == 0 && area->x1 == 0 && area->y1 == 0;
}

static int32_t
clipped_coordinate(int32_t value, uint32_t device, uint32_t world)
{
	uint64_t projected = (uint64_t)(uint32_t)value * device / world;

	/* Wire coordinates are nonnegative. Surface dimensions are <=INT32_MAX,
	 * so inclusive port endpoints are strictly below INT32_MAX. Saturating
	 * there preserves both intersection and entirely-outside rejection while
	 * allowing huge upper endpoints to clip without intermediate overflow. */
	return projected > INT32_MAX ? INT32_MAX : (int32_t)projected;
}

static int
translate(const struct st_rip3_command_host *host, unsigned environment, const struct st_rip3_rect *world,
    struct st_rip3_rect *result)
{
	struct st_rip3_frame frame;
	int                  status = st_rip3_command_frame(host, environment, &frame);

	if (status != 0)
		return status;
	*result = (struct st_rip3_rect) {
	    clipped_coordinate(world->x0, frame.device_width, frame.world_width),
	    clipped_coordinate(world->y0, frame.device_height, frame.world_height),
	    clipped_coordinate(world->x1, frame.device_width, frame.world_width),
	    clipped_coordinate(world->y1, frame.device_height, frame.world_height),
	};
	return 0;
}

static int
definition_budget(const struct st_rip3_command_host *host, unsigned replacement, uint64_t requested)
{
	if (host->offscreen_pixel_budget == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	/* Admission counts the final table while replacement retains the old
	 * allocation until the new bitmap is ready. */
	uint64_t used, budget = *host->offscreen_pixel_budget;
	int      status = st_rip3_ports_bitmap_pixels(host->ports, replacement, &used);
	if (status != 0)
		return status;
	return used > budget || requested > budget - used ? IO_ERROR_OUT_OF_MEMORY : 0;
}

static int
delete_port(struct st_rip3_ports *ports, const uint8_t *bytes, size_t length, unsigned radix, bool *operation)
{
	uint64_t              number, destination;
	struct st_rip3_fields fields = {bytes, length};
	int                   status = st_rip3_command_next_field(&fields, &number, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &destination, 1, radix, false);
	if (status == 0)
		status = reserved_field(&fields, 2, radix);
	if (status != 0)
		return status;
	if (number > 35 || destination > 35)
		return IO_ERROR_OUT_OF_RANGE;
	*operation = true;
	status     = st_rip3_ports_delete(ports, (unsigned)number);
	if (status != 0)
		return status;
	status = st_rip3_ports_switch(ports, (unsigned)destination);
	/* An undefined destination's documented fallback to port zero is a
	 * successful delete command, including a destination just deleted. */
	return status == IO_ERROR_NOT_FOUND ? 0 : status;
}

static int
define_port(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const uint8_t *bytes, size_t length, bool *operation)
{
	struct st_rip3_rect      world, device;
	struct st_rip3_frame     frame;
	struct st_rip3_port_info target;
	uint64_t                 number, flags;
	struct st_rip3_fields    fields = {bytes, length};
	int                      status;

	/* A shortened flags field is only possible at the delimiter, when
	 * the trailing reserved field is omitted. */
	status = st_rip3_command_next_field(&fields, &number, 1, environment->base_math, false);
	if (status == 0)
		status = coordinates(&world, &fields, environment->coordinate_size, environment->base_math, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &flags, 4, environment->base_math, true);
	if (status == 0)
		status = reserved_field(&fields, 4, environment->base_math);
	if (status != 0)
		return status;
	if (number == 0 || number > 35)
		return IO_ERROR_OUT_OF_RANGE;
	/* Rejecting reserved flags is client policy; the protocol only requires
	 * senders to leave them zero. Every combination of the low four is valid. */
	if ((flags & ~UINT64_C(15)) != 0)
		return IO_ERROR_NOT_SUPPORTED;
	bool offscreen = (flags & 1) != 0;
	if (offscreen)
		world.x0 = world.y0 = 0;
	if (!ordered(&world))
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	uint64_t x0 = (uint64_t)(uint32_t)world.x0 * frame.device_width / frame.world_width;
	uint64_t y0 = (uint64_t)(uint32_t)world.y0 * frame.device_height / frame.world_height;
	uint64_t x1 = (uint64_t)(uint32_t)world.x1 * frame.device_width / frame.world_width;
	uint64_t y1 = (uint64_t)(uint32_t)world.y1 * frame.device_height / frame.world_height;
	if (x0 >= x1 || y0 >= y1)
		return IO_ERROR_OUT_OF_RANGE;
	if (offscreen) {
		if (x1 > INT32_MAX || y1 > INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
	}
	else {
		/* Supported screen definitions intersect the display. Keep the client
		 * rejection policy for wholly external definitions; no edge allocation
		 * or integer wrapping is inferred from the original DLL. */
		if (x0 >= frame.device_width || y0 >= frame.device_height)
			return IO_ERROR_NOT_SUPPORTED;
		if (x1 > frame.device_width)
			x1 = frame.device_width;
		if (y1 > frame.device_height)
			y1 = frame.device_height;
	}
	/* DEFINE_PORT supplies extent boundaries, whereas the device table takes
	 * inclusive last pixels. Convert exactly once, after projection/clipping. */
	device = (struct st_rip3_rect) {(int32_t)x0, (int32_t)y0, (int32_t)(x1 - 1), (int32_t)(y1 - 1)};
	status = st_rip3_ports_info(host->ports, (unsigned)number, &target);
	if (status != 0)
		return status;
	*operation = true;
	/* Protected redefinition reaches the table's port-zero fallback before
	 * resource admission. The protocol specifies this rejection state. */
	if (!target.protected && offscreen) {
		status = definition_budget(host, (unsigned)number, x1 * y1);
		if (status != 0)
			return status;
	}
	status = st_rip3_ports_define(host->ports, (unsigned)number, &device, offscreen);
	if (status != 0)
		return status;
	/* From this point all indices are defined/in range, and these operations
	 * cannot allocate. Apply selection last so nonselecting definitions keep
	 * the current port. Deactivate before protection. */
	if ((flags & 4) != 0) {
		unsigned port = (unsigned)number;
		status        = st_rip3_ports_operate_views(host->ports, &port, 1, ST_RIP3_VIEW_DEACTIVATE);
	}
	if (status == 0 && (flags & 8) != 0)
		status = st_rip3_ports_protect(host->ports, (unsigned)number, true);
	if (status == 0 && (flags & 2) != 0)
		status = st_rip3_ports_switch(host->ports, (unsigned)number);
	return status;
}

/* 2C consumes half-open extent boundaries, unlike the device kernel's
 * inclusive pixels. Retain wide projected values until after clipping so a
 * huge request cannot change the before-clipping scale decision. */
static struct st_rip3_copy_extent
copy_project(const struct st_rip3_rect *world, const struct st_rip3_frame *frame)
{
	return (struct st_rip3_copy_extent) {(int64_t)world->x0 * frame->device_width / frame->world_width,
	    (int64_t)world->y0 * frame->device_height / frame->world_height,
	    (int64_t)world->x1 * frame->device_width / frame->world_width,
	    (int64_t)world->y1 * frame->device_height / frame->world_height};
}

static bool
copy_zero(const struct st_rip3_copy_extent *area)
{
	return area->x0 == 0 && area->y0 == 0 && area->x1 == 0 && area->y1 == 0;
}

static int
transfer_palette(const struct st_rip3_command_host *host, uint64_t mode, struct st_rip3_palette *palette,
    const struct st_rip3_palette **result)
{
	*result = NULL;
	if (mode < ST_RIP3_TRANSFER_XOR || mode > ST_RIP3_TRANSFER_AND || host->palettes == NULL)
		return 0;
	*result = palette;
	return st_rip3_palettes_current_palette(host->palettes, palette);
}

static int
copy_port(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const uint8_t *bytes, size_t length, bool *operation)
{
	struct st_rip3_rect        source_world, destination_world;
	struct st_rip3_frame       frame;
	struct st_rip3_copy_extent from, to;
	struct st_rip3_port_info   source_info, destination_info;
	uint64_t                   source_number, destination_number, mode;
	struct st_rip3_fields      fields = {bytes, length};
	size_t                     width  = environment->coordinate_size;
	unsigned                   radix  = environment->base_math;
	int                        status;

	status = st_rip3_command_next_field(&fields, &source_number, 1, radix, false);
	if (status == 0)
		status = coordinates(&source_world, &fields, width, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &destination_number, 1, radix, false);
	if (status == 0)
		status = coordinates(&destination_world, &fields, width, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 1, radix, false);
	if (status == 0)
		status = reserved_field(&fields, 5, radix);
	if (status != 0)
		return status;
	if (source_number > 35 || destination_number > 35 || mode > 4)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_ports_info(host->ports, (unsigned)source_number, &source_info);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, (unsigned)destination_number, &destination_info);
	if (status != 0)
		return status;
	if (!source_info.defined || !destination_info.defined || !source_info.viewport_active
	    || !destination_info.viewport_active)
		return 0;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	*operation = true;
	int64_t sw = (int64_t)source_info.viewport.x1 - source_info.viewport.x0 + 1;
	int64_t sh = (int64_t)source_info.viewport.y1 - source_info.viewport.y0 + 1;
	int64_t dw = (int64_t)destination_info.viewport.x1 - destination_info.viewport.x0 + 1;
	int64_t dh = (int64_t)destination_info.viewport.y1 - destination_info.viewport.y0 + 1;
	from       = copy_project(&source_world, &frame);
	to         = copy_project(&destination_world, &frame);
	/* Original handler 0x46372 tests these sentinels after conversion. A
	 * fully zero destination takes precedence over its verbatim shorthand. */
	if (copy_zero(&from))
		from = (struct st_rip3_copy_extent) {0, 0, sw, sh};
	if (copy_zero(&to))
		to = (struct st_rip3_copy_extent) {0, 0, dw, dh};
	else if (to.x1 == 0 && to.y1 == 0) {
		to.x1 = to.x0 + from.x1 - from.x0;
		to.y1 = to.y0 + from.y1 - from.y0;
	}
	if (from.x0 >= from.x1 || from.y0 >= from.y1 || to.x0 >= to.x1 || to.y0 >= to.y1)
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_rip3_palette        palette;
	const struct st_rip3_palette *operand_palette;
	status = transfer_palette(host, mode, &palette, &operand_palette);
	if (status != 0)
		return status;
	return st_rip3_ports_copy_device(host->ports, (unsigned)source_number, &from, (unsigned)destination_number,
	    &to, (enum st_rip3_transfer_mode)mode, operand_palette);
}

static int
switch_port(struct st_rip3_ports *ports, const uint8_t *bytes, size_t length, unsigned radix, bool *operation)
{
	uint64_t destination, flags;
	unsigned current;
	int      status, selection_status;

	/* Only the four low flag bits have effects. */
	struct st_rip3_fields fields = {bytes, length};
	status                       = st_rip3_command_next_field(&fields, &destination, 1, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &flags, 2, radix, true);
	if (status != 0)
		return status;
	if (destination > 35)
		return IO_ERROR_OUT_OF_RANGE;
	*operation = true;
	status     = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	/* The original order is observable on same-port switches and failures.
	 * Protection writes to zero are ignored; unprotect follows protect. */
	if (current != 0 && (flags & 4) != 0)
		(void)st_rip3_ports_protect(ports, current, true);
	if (current != 0 && (flags & 8) != 0)
		(void)st_rip3_ports_protect(ports, current, false);
	selection_status = st_rip3_ports_select(ports, (unsigned)destination);
	status           = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	/* After failed creation, destination actions address actual fallback zero,
	 * never the requested absent target or the former source. */
	if (current != 0 && (flags & 1) != 0)
		(void)st_rip3_ports_protect(ports, current, true);
	if (current != 0 && (flags & 2) != 0)
		(void)st_rip3_ports_protect(ports, current, false);
	return selection_status;
}

static int
paste_clipboard(const struct st_rip3_command_host *host, unsigned environment_number,
    const struct st_rip3_environment *environment, const uint8_t *bytes, size_t length, bool *operation)
{
	uint64_t                 x, y, mode;
	struct st_rip3_port_info info;
	struct st_rip3_rect      world, device;
	unsigned                 destination;
	struct st_rip3_fields    fields = {bytes, length};
	size_t                   width  = environment->coordinate_size;
	unsigned                 radix  = environment->base_math;
	int                      status = st_rip3_command_next_field(&fields, &x, width, radix, false);

	if (status == 0)
		status = st_rip3_command_next_field(&fields, &y, width, radix, false);
	if (status == 0)
		status = st_rip3_command_next_field(&fields, &mode, 2, radix, true);
	if (status == 0)
		status = reserved_field(&fields, 1, radix);
	if (status != 0)
		return status;
	if (x > INT32_MAX || y > INT32_MAX || mode > 4)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->clipboard == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (!host->clipboard->set)
		return 0;
	if (host->clipboard->port == 0 || host->clipboard->port > 35)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_current(host->ports, &destination);
	if (status != 0 || destination == host->clipboard->port)
		return status;
	status = st_rip3_ports_info(host->ports, host->clipboard->port, &info);
	if (status != 0 || !info.defined)
		return status;
	status = st_rip3_ports_info(host->ports, destination, &info);
	if (status != 0 || !info.defined || !info.viewport_active)
		return status;
	world  = (struct st_rip3_rect) {(int32_t)x, (int32_t)y, (int32_t)x, (int32_t)y};
	status = translate(host, environment_number, &world, &device);
	if (status != 0)
		return status;
	struct st_rip3_palette        palette;
	const struct st_rip3_palette *present_palette;
	status = transfer_palette(host, mode, &palette, &present_palette);
	if (status != 0)
		return status;
	return st_rip3_ports_put_image(host->ports, host->clipboard->port, destination, device.x0, device.y0,
	    (enum st_rip3_transfer_mode)mode, present_palette);
}

int
st_rip3_port_commands_execute(const struct st_rip3_command_host *host, const uint8_t *command, size_t length,
    bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_rect        world, device;
	unsigned                   number;
	const uint8_t             *arguments;
	size_t                     count;
	int                        status;

	if (host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	arguments = command + (command[0] == 'v' ? 1 : 2);
	count     = length - (size_t)(arguments - command);
	if (command[0] == '2' && command[1] == 's')
		return switch_port(host->ports, arguments, count, environment.base_math, operation);
	if (command[0] == '1')
		return paste_clipboard(host, number, &environment, arguments, count, operation);
	if (command[0] != 'v') {
		if (command[1] == 'C')
			return copy_port(host, number, &environment, arguments, count, operation);
		if (command[1] == 'p')
			return delete_port(host->ports, arguments, count, environment.base_math, operation);
		return define_port(host, number, &environment, arguments, count, operation);
	}
	struct st_rip3_fields fields = {arguments, count};
	status = coordinates(&world, &fields, environment.coordinate_size, environment.base_math, true);
	if (status != 0)
		return status;
	if (world.x0 > world.x1) {
		int32_t swap = world.x0;
		world.x0     = world.x1;
		world.x1     = swap;
	}
	if (world.y0 > world.y1) {
		int32_t swap = world.y0;
		world.y0     = world.y1;
		world.y1     = swap;
	}
	struct st_rip3_frame frame;
	struct st_rip3_point first = {world.x0, world.y0}, last = {world.x1, world.y1};
	status = st_rip3_command_frame(host, number, &frame);
	if (status != 0)
		return status;
	*operation = true;
	status     = st_rip3_world_to_device(&frame, &first, &first);
	if (status == 0)
		status = st_rip3_world_to_device(&frame, &last, &last);
	if (status != 0)
		return status;
	/* The protocol's exclusive far edge must fit even though our device
	 * rectangle stores the corresponding inclusive pixel. */
	if (last.x == INT32_MAX || last.y == INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	device = (struct st_rip3_rect) {first.x, first.y, last.x, last.y};
	/* Original helper 0x33cc6 tests zero after conversion. Its stored viewport
	 * uses DEVICE coordinates directly, without adding the port origin or
	 * intersecting with port bounds. Internal rectangles already have inclusive
	 * endpoints, so no extra far unit belongs in this representation. */
	if (all_zero(&device))
		return st_rip3_ports_activate(host->ports, false);
	return st_rip3_ports_viewport_device(host->ports, &device);
}
