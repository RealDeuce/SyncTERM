/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "controller_internal.h"
#include "st_controller_config.h"
#include "st_time.h"

bool
pad_native_init(struct st_controllers *hub)
{
#if ST_CONTROLLER_HIDAPI
	hub->hid_ready = pad_hid_init(hub);
#endif
#if ST_CONTROLLER_LIBUSB
	(void)pad_usb_init(hub);
#endif
#if ST_CONTROLLER_EVDEV
	(void)pad_evdev_init(hub);
#endif
	return hub->hid_ready || hub->usb || hub->evdev || ST_CONTROLLER_XINPUT;
}

void
pad_native_poll(struct st_controllers *hub, st_controller_emit emit, void *context)
{
	uint64_t now = 0;
	(void)st_time_monotonic_ms(&now);
	bool discover = !hub->next_scan || now >= hub->next_scan;
	if (discover)
		hub->next_scan = now + 1000;
#if ST_CONTROLLER_HIDAPI
	if (hub->hid_ready)
		pad_hid_poll(hub, discover, emit, context);
#endif
#if ST_CONTROLLER_LIBUSB
	if (hub->usb)
		pad_usb_poll(hub, discover, emit, context);
#endif
#if ST_CONTROLLER_EVDEV
	if (hub->evdev)
		pad_evdev_poll(hub, discover, emit, context);
#endif
#if ST_CONTROLLER_XINPUT
	pad_xinput_poll(hub, discover, emit, context);
#endif
	(void)emit;
	(void)context;
}

void
pad_native_close(struct st_controllers *hub)
{
#if ST_CONTROLLER_HIDAPI
	if (hub->hid_ready)
		pad_hid_close(hub);
#endif
#if ST_CONTROLLER_LIBUSB
	if (hub->usb)
		pad_usb_close(hub);
#endif
#if ST_CONTROLLER_EVDEV
	if (hub->evdev)
		pad_evdev_close(hub);
#endif
	(void)hub;
}
