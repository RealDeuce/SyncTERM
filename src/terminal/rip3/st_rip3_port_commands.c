/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_command_internal.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "io_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int
coordinates(struct st_rip3_rect *result, const uint8_t *bytes,
    size_t length, size_t width, unsigned radix, bool shortened)
{
	uint64_t values[4];
	int status;

	if ((shortened && (length <= 3*width || length > 4*width))
	    || (!shortened && length != 4*width))
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; i < 4; i++) {
		size_t count = i == 3 ? length - 3*width : width;
		status = st_rip3_command_field(values+i, bytes+i*width, count, radix);
		if (status != 0)
			return status;
		if (values[i] > INT32_MAX)
			return IO_ERROR_OUT_OF_RANGE;
	}
	*result = (struct st_rip3_rect){(int32_t)values[0],(int32_t)values[1],
	    (int32_t)values[2],(int32_t)values[3]};
	return 0;
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
translate(const struct st_rip3_command_host *host, unsigned environment,
    const struct st_rip3_rect *world, struct st_rip3_rect *result)
{
	struct st_rip3_frame frame;
	int status = st_rip3_command_frame(host, environment, &frame);

	if (status != 0)
		return status;
	*result = (struct st_rip3_rect){
		clipped_coordinate(world->x0, frame.device_width, frame.world_width),
		clipped_coordinate(world->y0, frame.device_height, frame.world_height),
		clipped_coordinate(world->x1, frame.device_width, frame.world_width),
		clipped_coordinate(world->y1, frame.device_height, frame.world_height),
	};
	return 0;
}

static int
definition_budget(const struct st_rip3_command_host *host, unsigned replacement,
    uint64_t requested)
{
	uint64_t used = 0;

	if (host->offscreen_pixel_budget == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	/* Client admission policy counts the final table. Replacement is staged
	 * atomically, so its old allocation remains alive until the new one exists;
	 * the budget is a pixel-count limit, not a peak-memory guarantee. */
	for (unsigned i = 1; i < 36; i++) {
		struct st_rip3_port_info info;
		int status = st_rip3_ports_info(host->ports,i,&info);
		if (status != 0) return status;
		if (i == replacement || !info.defined || !info.offscreen) continue;
		uint64_t width = (uint64_t)((int64_t)info.bounds.x1-info.bounds.x0+1);
		uint64_t height = (uint64_t)((int64_t)info.bounds.y1-info.bounds.y0+1);
		uint64_t amount = width*height;
		if (amount > *host->offscreen_pixel_budget-used) return IO_ERROR_OUT_OF_MEMORY;
		used += amount;
	}
	return requested > *host->offscreen_pixel_budget-used ? IO_ERROR_OUT_OF_MEMORY : 0;
}

static int
delete_port(struct st_rip3_ports *ports, const uint8_t *bytes,
    size_t length, unsigned radix, bool *operation)
{
	uint64_t number, destination, reserved;
	int status;

	if (length < 2 || length > 4)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&number, bytes, 1, radix);
	if (status == 0)
		status = st_rip3_command_field(&destination, bytes+1, 1, radix);
	if (status == 0 && length > 2) {
		status = st_rip3_command_field(&reserved, bytes+2, length-2, radix);
		if (status == 0 && reserved != 0)
			return IO_ERROR_NOT_SUPPORTED;
	}
	if (status != 0)
		return status;
	if (number > 35 || destination > 35)
		return IO_ERROR_OUT_OF_RANGE;
	*operation = true;
	status = st_rip3_ports_delete(ports, (unsigned)number);
	if (status != 0)
		return status;
	status = st_rip3_ports_switch(ports, (unsigned)destination);
	/* An undefined destination's documented fallback to port zero is a
	 * successful delete command, including a destination just deleted. */
	return status == IO_ERROR_NOT_FOUND ? 0 : status;
}

static int
define_port(const struct st_rip3_command_host *host,
    unsigned environment_number, const struct st_rip3_environment *environment,
    const uint8_t *bytes, size_t length, bool *operation)
{
	struct st_rip3_rect world, device;
	struct st_rip3_frame frame;
	struct st_rip3_port_info target;
	uint64_t number, flags, reserved;
	size_t coordinate_bytes = 4*environment->coordinate_size;
	size_t prefix = 1 + coordinate_bytes;
	int status;

	/* Flags are the last nonreserved parameter: they may shorten only when
	 * the reserved parameter is wholly omitted. Full flags permit 0..4
	 * transmitted reserved zero digits. Validate before any table mutation. */
	if (length <= prefix || length > prefix+8)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&number, bytes, 1, environment->base_math);
	if (status == 0)
		status = coordinates(&world, bytes+1, coordinate_bytes,
		    environment->coordinate_size, environment->base_math, false);
	if (status == 0)
		status = st_rip3_command_field(&flags, bytes+prefix,
		    length - prefix > 4 ? 4 : length - prefix, environment->base_math);
	if (status == 0 && length > prefix+4) {
		status = st_rip3_command_field(&reserved, bytes+prefix+4,
		    length-prefix-4, environment->base_math);
		if (status == 0 && reserved != 0)
			return IO_ERROR_NOT_SUPPORTED;
	}
	if (status != 0)
		return status;
	if (number == 0 || number > 35)
		return IO_ERROR_OUT_OF_RANGE;
	/* Rejecting reserved flags is client policy; the protocol only requires
	 * senders to leave them zero. Every combination of the low four is valid. */
	if ((flags & ~UINT64_C(15)) != 0)
		return IO_ERROR_NOT_SUPPORTED;
	bool offscreen = (flags & 1) != 0;
	if (offscreen) world.x0 = world.y0 = 0;
	if (!ordered(&world)) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0) return status;
	uint64_t x0 = (uint64_t)(uint32_t)world.x0*frame.device_width/frame.world_width;
	uint64_t y0 = (uint64_t)(uint32_t)world.y0*frame.device_height/frame.world_height;
	uint64_t x1 = (uint64_t)(uint32_t)world.x1*frame.device_width/frame.world_width;
	uint64_t y1 = (uint64_t)(uint32_t)world.y1*frame.device_height/frame.world_height;
	if (x0 >= x1 || y0 >= y1) return IO_ERROR_OUT_OF_RANGE;
	if (offscreen) {
		if (x1 > INT32_MAX || y1 > INT32_MAX) return IO_ERROR_OUT_OF_RANGE;
	}
	else {
		/* Supported screen definitions intersect the display. Keep the client
		 * rejection policy for wholly external definitions; no edge allocation
		 * or integer wrapping is inferred from the original DLL. */
		if (x0 >= frame.device_width || y0 >= frame.device_height)
			return IO_ERROR_NOT_SUPPORTED;
		if (x1 > frame.device_width) x1 = frame.device_width;
		if (y1 > frame.device_height) y1 = frame.device_height;
	}
	/* DEFINE_PORT supplies extent boundaries, whereas the device table takes
	 * inclusive last pixels. Convert exactly once, after projection/clipping. */
	device = (struct st_rip3_rect){(int32_t)x0,(int32_t)y0,(int32_t)(x1-1),(int32_t)(y1-1)};
	status = st_rip3_ports_info(host->ports,(unsigned)number,&target);
	if (status != 0)
		return status;
	*operation = true;
	/* Protected redefinition reaches the table's port-zero fallback before
	 * resource admission. The protocol specifies this rejection state. */
	if (!target.protected && offscreen) {
		status = definition_budget(host,(unsigned)number,x1*y1);
		if (status != 0) return status;
	}
	status = st_rip3_ports_define(host->ports, (unsigned)number, &device, offscreen);
	if (status != 0)
		return status;
	/* From this point all indices are defined/in range, and these operations
	 * cannot allocate. Apply selection last so nonselecting definitions keep
	 * the current port. Deactivate before protection. */
	if ((flags & 4) != 0) {
		unsigned port = (unsigned)number;
		status = st_rip3_ports_operate_views(host->ports, &port, 1,
		    ST_RIP3_VIEW_DEACTIVATE, 0);
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
	return (struct st_rip3_copy_extent){
	    (int64_t)world->x0*frame->device_width/frame->world_width,
	    (int64_t)world->y0*frame->device_height/frame->world_height,
	    (int64_t)world->x1*frame->device_width/frame->world_width,
	    (int64_t)world->y1*frame->device_height/frame->world_height};
}

static bool
copy_zero(const struct st_rip3_copy_extent *area)
{
	return area->x0 == 0 && area->y0 == 0 && area->x1 == 0 && area->y1 == 0;
}

static int
copy_port(const struct st_rip3_command_host *host,
    unsigned environment_number, const struct st_rip3_environment *environment,
    const uint8_t *bytes, size_t length, bool *operation)
{
	struct st_rip3_rect source_world, destination_world;
	struct st_rip3_frame frame;
	struct st_rip3_copy_extent from, to;
	struct st_rip3_port_info source_info, destination_info;
	uint64_t source_number, destination_number, mode, reserved;
	size_t coordinate_bytes = 4*environment->coordinate_size;
	size_t minimum = 2*coordinate_bytes+3, destination_offset = coordinate_bytes+1;
	int status;

	if (length < minimum || length > minimum+5)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&source_number, bytes, 1, environment->base_math);
	if (status == 0)
		status = coordinates(&source_world, bytes+1, coordinate_bytes,
		    environment->coordinate_size, environment->base_math, false);
	if (status == 0)
		status = st_rip3_command_field(&destination_number, bytes+destination_offset,
		    1, environment->base_math);
	if (status == 0)
		status = coordinates(&destination_world, bytes+destination_offset+1,
		    coordinate_bytes, environment->coordinate_size, environment->base_math, false);
	if (status == 0)
		status = st_rip3_command_field(&mode, bytes+minimum-1, 1, environment->base_math);
	if (status == 0 && length > minimum) {
		status = st_rip3_command_field(&reserved, bytes+minimum,
		    length-minimum, environment->base_math);
		if (status == 0 && reserved != 0)
			return IO_ERROR_NOT_SUPPORTED;
	}
	if (status != 0)
		return status;
	if (source_number > 35 || destination_number > 35 || mode > 4)
		return IO_ERROR_OUT_OF_RANGE;
	status = st_rip3_ports_info(host->ports, (unsigned)source_number, &source_info);
	if (status == 0)
		status = st_rip3_ports_info(host->ports, (unsigned)destination_number, &destination_info);
	if (status != 0)
		return status;
	if (!source_info.defined || !destination_info.defined
	    || !source_info.viewport_active || !destination_info.viewport_active)
		return 0;
	status = st_rip3_command_frame(host, environment_number, &frame);
	if (status != 0)
		return status;
	*operation = true;
	int64_t sw = (int64_t)source_info.viewport.x1-source_info.viewport.x0+1;
	int64_t sh = (int64_t)source_info.viewport.y1-source_info.viewport.y0+1;
	int64_t dw = (int64_t)destination_info.viewport.x1-destination_info.viewport.x0+1;
	int64_t dh = (int64_t)destination_info.viewport.y1-destination_info.viewport.y0+1;
	from = copy_project(&source_world,&frame);
	to = copy_project(&destination_world,&frame);
	/* Original handler 0x46372 tests these sentinels after conversion. A
	 * fully zero destination takes precedence over its verbatim shorthand. */
	if (copy_zero(&from)) from = (struct st_rip3_copy_extent){0,0,sw,sh};
	if (copy_zero(&to)) to = (struct st_rip3_copy_extent){0,0,dw,dh};
	else if (to.x1 == 0 && to.y1 == 0) {
		to.x1 = to.x0+from.x1-from.x0;
		to.y1 = to.y0+from.y1-from.y0;
	}
	if (from.x0 >= from.x1 || from.y0 >= from.y1 || to.x0 >= to.x1 || to.y0 >= to.y1)
		return IO_ERROR_INVALID_ARGUMENT;
	return st_rip3_ports_copy_device(host->ports,(unsigned)source_number,&from,
	    (unsigned)destination_number,&to,(enum st_rip3_transfer_mode)mode);
}

static int
switch_port(struct st_rip3_ports *ports, const uint8_t *bytes,
    size_t length, unsigned radix, bool *operation)
{
	uint64_t destination, flags;
	unsigned current;
	int status, selection_status;

	/* Original slot 118 has exactly these two fields and no reserved field.
	 * Higher flag bits are accepted but only the four low bits have effects. */
	if (length != 3) return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&destination,bytes,1,radix);
	if (status == 0) status = st_rip3_command_field(&flags,bytes+1,2,radix);
	if (status != 0) return status;
	if (destination > 35) return IO_ERROR_OUT_OF_RANGE;
	*operation = true;
	status = st_rip3_ports_current(ports,&current);
	if (status != 0) return status;
	/* The original order is observable on same-port switches and failures.
	 * Protection writes to zero are ignored; unprotect follows protect. */
	if (current != 0 && (flags & 4) != 0)
		(void)st_rip3_ports_protect(ports,current,true);
	if (current != 0 && (flags & 8) != 0)
		(void)st_rip3_ports_protect(ports,current,false);
	selection_status = st_rip3_ports_select(ports,(unsigned)destination);
	status = st_rip3_ports_current(ports,&current);
	if (status != 0) return status;
	/* After failed creation, destination actions address actual fallback zero,
	 * never the requested absent target or the former source. */
	if (current != 0 && (flags & 1) != 0)
		(void)st_rip3_ports_protect(ports,current,true);
	if (current != 0 && (flags & 2) != 0)
		(void)st_rip3_ports_protect(ports,current,false);
	return selection_status;
}

static int
paste_clipboard(const struct st_rip3_command_host *host,
    unsigned environment_number, const struct st_rip3_environment *environment,
    const uint8_t *bytes, size_t length, bool *operation)
{
	uint64_t x, y, mode, reserved;
	struct st_rip3_port_info info;
	struct st_rip3_rect world, device;
	unsigned destination;
	size_t width = environment->coordinate_size, prefix = 2*width;
	int status;

	if (length <= prefix || length > prefix+3)
		return IO_ERROR_INVALID_FORMAT;
	status = st_rip3_command_field(&x,bytes,width,environment->base_math);
	if (status == 0)
		status = st_rip3_command_field(&y,bytes+width,width,environment->base_math);
	if (status == 0)
		status = st_rip3_command_field(&mode,bytes+prefix,
		    length-prefix > 2 ? 2 : length-prefix,environment->base_math);
	if (status == 0 && length == prefix+3) {
		status = st_rip3_command_field(&reserved,bytes+prefix+2,1,environment->base_math);
		if (status == 0 && reserved != 0) return IO_ERROR_NOT_SUPPORTED;
	}
	if (status != 0) return status;
	if (x > INT32_MAX || y > INT32_MAX || mode > 4) return IO_ERROR_OUT_OF_RANGE;
	if (host->clipboard == NULL) return IO_ERROR_NOT_SUPPORTED;
	*operation = true;
	if (!host->clipboard->set) return 0;
	if (host->clipboard->port == 0 || host->clipboard->port > 35)
		return IO_ERROR_INVALID_ARGUMENT;
	status = st_rip3_ports_current(host->ports,&destination);
	if (status != 0 || destination == host->clipboard->port) return status;
	status = st_rip3_ports_info(host->ports,host->clipboard->port,&info);
	if (status != 0) return status;
	/* Pointer lifecycle and inactive-source paste behavior are unresolved
	 * under #42. Do not invent a fallback or clear the owner's state. */
	if (!info.defined || !info.viewport_active) return IO_ERROR_NOT_SUPPORTED;
	world = (struct st_rip3_rect){(int32_t)x,(int32_t)y,(int32_t)x,(int32_t)y};
	status = translate(host,environment_number,&world,&device);
	if (status != 0) return status;
	return st_rip3_ports_blit(host->ports,host->clipboard->port,NULL,destination,
	    device.x0,device.y0,(enum st_rip3_transfer_mode)mode);
}

int
st_rip3_port_commands_execute(const struct st_rip3_command_host *host,
    const uint8_t *command, size_t length, bool *operation)
{
	struct st_rip3_environment environment;
	struct st_rip3_rect world, device;
	unsigned number;
	const uint8_t *arguments;
	size_t count;
	int status;

	if (host->ports == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_command_environment(host, &number, &environment);
	if (status != 0)
		return status;
	arguments = command + (command[0] == 'v' ? 1 : 2);
	count = length - (size_t)(arguments - command);
	if (memchr(arguments, ' ', count) != NULL)
		return IO_ERROR_NOT_SUPPORTED; /* General numeric comments: #7. */
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
	status = coordinates(&world, arguments, count, environment.coordinate_size,
	    environment.base_math, true);
	if (status != 0) return status;
	if (world.x0 > world.x1) { int32_t swap = world.x0; world.x0 = world.x1; world.x1 = swap; }
	if (world.y0 > world.y1) { int32_t swap = world.y0; world.y0 = world.y1; world.y1 = swap; }
	struct st_rip3_frame frame;
	struct st_rip3_point first = {world.x0,world.y0}, last = {world.x1,world.y1};
	status = st_rip3_command_frame(host,number,&frame);
	if (status != 0) return status;
	*operation = true;
	status = st_rip3_world_to_device(&frame,&first,&first);
	if (status == 0) status = st_rip3_world_to_device(&frame,&last,&last);
	if (status != 0) return status;
	device = (struct st_rip3_rect){first.x,first.y,last.x,last.y};
	/* Original helper 0x33cc6 tests zero after conversion. Its stored viewport
	 * uses DEVICE coordinates directly, without adding the port origin or
	 * intersecting with port bounds. Internal rectangles already have inclusive
	 * endpoints, so no extra far unit belongs in this representation. */
	if (all_zero(&device)) return st_rip3_ports_activate(host->ports,false);
	return st_rip3_ports_viewport_device(host->ports,&device);
}
