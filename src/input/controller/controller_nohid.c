/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "controller_internal.h"

bool
pad_native_init(struct st_controllers *hub)
{
	(void)hub;
	return false;
}

void
pad_native_close(struct st_controllers *hub)
{
	(void)hub;
}

void
pad_native_poll(struct st_controllers *hub, st_controller_emit emit, void *context)
{
	(void)hub;
	(void)emit;
	(void)context;
}
