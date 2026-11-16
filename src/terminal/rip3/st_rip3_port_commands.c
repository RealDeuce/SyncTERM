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
	if (values[0] > values[2] || values[1] > values[3])
		return IO_ERROR_INVALID_FORMAT;
	*result = (struct st_rip3_rect){(int32_t)values[0],(int32_t)values[1],
	    (int32_t)values[2],(int32_t)values[3]};
	return 0;
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
	struct st_rip3_port_info screen;
	int status = st_rip3_environments_frame(host->environments, environment, &frame);

	if (status == 0)
		status = st_rip3_ports_info(host->ports, 0, &screen);
	if (status != 0)
		return status;
	if (screen.bounds.x0 != 0 || screen.bounds.y0 != 0
	    || (uint32_t)screen.bounds.x1 + 1 != frame.device_width
	    || (uint32_t)screen.bounds.y1 + 1 != frame.device_height)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = (struct st_rip3_rect){
		clipped_coordinate(world->x0, frame.device_width, frame.world_width),
		clipped_coordinate(world->y0, frame.device_height, frame.world_height),
		clipped_coordinate(world->x1, frame.device_width, frame.world_width),
		clipped_coordinate(world->y1, frame.device_height, frame.world_height),
	};
	return 0;
}

static int
clip_to_port(struct st_rip3_ports *ports, unsigned number, struct st_rip3_rect *area)
{
	struct st_rip3_port_info info;
	int status = st_rip3_ports_info(ports, number, &info);
	int32_t x1, y1;

	if (status != 0)
		return status;
	x1 = info.bounds.x1 - info.bounds.x0;
	y1 = info.bounds.y1 - info.bounds.y0;
	/* Partial intersections are defined. A wholly external wire rectangle's
	 * edge-clamp/empty/reject policy is unresolved under #10. Do not infer
	 * it from the device kernel's empty-intersection rejection. */
	if (area->x0 > x1 || area->y0 > y1)
		return IO_ERROR_NOT_SUPPORTED;
	if (area->x1 > x1) area->x1 = x1;
	if (area->y1 > y1) area->y1 = y1;
	return 0;
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
	if ((flags & ~UINT64_C(15)) != 0 || (flags & 1) != 0)
		return IO_ERROR_NOT_SUPPORTED; /* Offscreen world extents: #10. */
	status = translate(host, environment_number, &world, &device);
	if (status == 0)
		status = clip_to_port(host->ports, 0, &device);
	if (status != 0)
		return status;
	*operation = true;
	status = st_rip3_ports_define(host->ports, (unsigned)number, &device, false);
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
	if (command[0] != 'v') {
		if (command[1] == 'p')
			return delete_port(host->ports, arguments, count, environment.base_math, operation);
		return define_port(host, number, &environment, arguments, count, operation);
	}
	status = coordinates(&world, arguments, count, environment.coordinate_size,
	    environment.base_math, true);
	if (status == 0)
		status = translate(host, number, &world, &device);
	if (status != 0)
		return status;
	/* The deactivation sentinel belongs to WORLD fields, before rounding.
	 * A nonzero world rectangle can map to one device pixel at the origin. */
	if (world.x0 == 0 && world.y0 == 0 && world.x1 == 0 && world.y1 == 0) {
		*operation = true;
		return st_rip3_ports_activate(host->ports, false);
	}
	status = st_rip3_ports_current(host->ports, &number);
	if (status == 0)
		status = clip_to_port(host->ports, number, &device);
	if (status != 0)
		return status;
	*operation = true;
	return st_rip3_ports_viewport(host->ports, &device);
}
