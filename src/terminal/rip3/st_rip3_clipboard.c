/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_clipboard.h"
#include "st_rip3_ports.h"
#include "st_rip3_surface.h"
#include "io_error.h"

#include <stddef.h>

static int32_t
bounded(int64_t value)
{
	return value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : (int32_t)value;
}

static int
budget_available(struct st_rip3_ports *ports, uint64_t budget, uint64_t requested)
{
	uint64_t used = 0;
	for (unsigned i = 1; i < 36; i++) {
		struct st_rip3_port_info info;
		int status = st_rip3_ports_info(ports,i,&info);
		if (status != 0) return status;
		if (!info.defined || !info.offscreen) continue;
		uint64_t width = (uint64_t)((int64_t)info.bounds.x1-info.bounds.x0+1);
		uint64_t height = (uint64_t)((int64_t)info.bounds.y1-info.bounds.y0+1);
		uint64_t amount = width*height;
		if (used > UINT64_MAX-amount) return IO_ERROR_OUT_OF_MEMORY;
		used += amount;
	}
	return used > budget || requested > budget-used ? IO_ERROR_OUT_OF_MEMORY : 0;
}

int
st_rip3_clipboard_capture(struct st_rip3_ports *ports,
    struct st_rip3_clipboard_selection *selection,
    const struct st_rip3_clipboard_area *area, uint64_t pixel_budget)
{
	struct st_rip3_surface *bitmap = NULL;
	struct st_rip3_port_info target;
	unsigned number = 0, source;
	int status;
	const int64_t limit = INT64_C(1024)*INT32_MAX;

	if (ports == NULL || selection == NULL || area == NULL
	    || area->left > area->right || area->top > area->bottom
	    || area->left < -limit || area->right > limit || area->top < -limit || area->bottom > limit)
		return IO_ERROR_INVALID_ARGUMENT;
	uint16_t width = (uint16_t)(area->right-area->left+1);
	uint16_t height = (uint16_t)(area->bottom-area->top+1);
	/* Do not invent native zero-dimension allocation behavior. */
	if (width == 0 || height == 0) return IO_ERROR_NOT_SUPPORTED;
	if (selection->set) {
		number = selection->port;
		if (number >= 36) return IO_ERROR_OUT_OF_RANGE;
		if (number == 0) return IO_ERROR_PERMISSION_DENIED;
		status = st_rip3_ports_info(ports,number,&target);
		if (status != 0) return status;
		if (target.protected) return IO_ERROR_PERMISSION_DENIED;
		/* Current-target deletion selects zero. Do not save a source index
		 * or the old bitmap: replacement deliberately precedes allocation. */
		status = st_rip3_ports_delete(ports,number);
		if (status != 0) return status;
	}
	else {
		for (unsigned i = 1; i < 36; i++) {
			status = st_rip3_ports_info(ports,i,&target);
			if (status != 0) return status;
			if (!target.defined) { number = i; break; }
		}
		if (number == 0) return IO_ERROR_NO_SPACE;
	}
	selection->set = true; selection->port = number;
	status = budget_available(ports,pixel_budget,(uint64_t)width*height);
	if (status == 0) status = st_rip3_surface_create(&bitmap,width,height);
	if (status == 0) status = st_rip3_ports_install_bitmap(ports,number,&bitmap);
	st_rip3_surface_destroy(bitmap);
	if (status != 0) {
		*selection = (struct st_rip3_clipboard_selection){0};
		return status;
	}
	status = st_rip3_ports_current(ports,&source);
	if (status != 0) return status;
	/* Saturation preserves intersection with any representable viewport.
	 * Empty/trim/copy failures retain the newly installed clear allocation. */
	const struct st_rip3_rect from = {bounded(area->left),bounded(area->top),
	    bounded(area->right-1),bounded(area->bottom-1)};
	(void)st_rip3_ports_copy(ports,source,&from,number,NULL,ST_RIP3_TRANSFER_COPY);
	return 0;
}
