/* Read wired XUSB interfaces owned by a kernel input driver. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libevdev/libevdev.h>
#include <libudev.h>

#include "controller_internal.h"
#include "st_controller_config.h"

#define ST_API_NAME devices
#define ST_API_PREFIX udev_
#define ST_API_LINKED ST_DEP_UDEV_LINKED
#define ST_API_NAMES "udev"
#define ST_API_MAJOR 1
#define ST_API_FUNCTIONS(X) \
	X(const char *, device_get_devnode, (struct udev_device *)) \
	X(dev_t, device_get_devnum, (struct udev_device *)) \
	X(struct udev_device *, device_get_parent_with_subsystem_devtype, (struct udev_device *, const char *, const char *)) \
	X(const char *, device_get_sysattr_value, (struct udev_device *, const char *)) \
	X(struct udev_device *, device_new_from_syspath, (struct udev *, const char *)) \
	X(struct udev_device *, device_unref, (struct udev_device *)) \
	X(int, enumerate_add_match_subsystem, (struct udev_enumerate *, const char *)) \
	X(int, enumerate_add_match_sysname, (struct udev_enumerate *, const char *)) \
	X(struct udev_list_entry *, enumerate_get_list_entry, (struct udev_enumerate *)) \
	X(struct udev_enumerate *, enumerate_new, (struct udev *)) \
	X(int, enumerate_scan_devices, (struct udev_enumerate *)) \
	X(struct udev_enumerate *, enumerate_unref, (struct udev_enumerate *)) \
	X(const char *, list_entry_get_name, (struct udev_list_entry *)) \
	X(struct udev_list_entry *, list_entry_get_next, (struct udev_list_entry *)) \
	X(struct udev *, new, (void)) \
	X(struct udev *, unref, (struct udev *))
#include "dynamic_library_api.inc"

#define ST_API_NAME events
#define ST_API_PREFIX libevdev_
#define ST_API_LINKED ST_DEP_EVDEV_LINKED
#define ST_API_NAMES "evdev"
#define ST_API_MAJOR 2
#define ST_API_FUNCTIONS(X) \
	X(void, free, (struct libevdev *)) \
	X(int, get_abs_maximum, (const struct libevdev *, unsigned int)) \
	X(int, get_abs_minimum, (const struct libevdev *, unsigned int)) \
	X(int, get_event_value, (const struct libevdev *, unsigned int, unsigned int)) \
	X(int, get_fd, (const struct libevdev *)) \
	X(int, get_id_product, (const struct libevdev *)) \
	X(int, get_id_vendor, (const struct libevdev *)) \
	X(const char *, get_name, (const struct libevdev *)) \
	X(int, has_event_code, (const struct libevdev *, unsigned int, unsigned int)) \
	X(int, new_from_fd, (int, struct libevdev **)) \
	X(int, next_event, (struct libevdev *, unsigned int, struct input_event *))
#include "dynamic_library_api.inc"

struct evdev_binding {
	unsigned type, code;
	int      hat_sign;
};

struct evdev_pad {
	struct evdev_pad    *next;
	struct libevdev     *dev;
	struct st_pad       *pad;
	dev_t                device;
	bool                 syncing;
	struct evdev_binding bindings[];
};

struct evdev_provider {
	struct udev      *udev;
	struct evdev_pad *pads;
};

bool
pad_evdev_init(struct st_controllers *hub)
{
	if (!devices_available())
		return false;
	if (!events_available())
		return false;
	struct evdev_provider *provider = calloc(1, sizeof(*provider));
	if (!provider)
		return false;
	provider->udev = devices.new();
	if (!provider->udev) {
		free(provider);
		return false;
	}
	hub->evdev = provider;
	return true;
}

static void
close_pad(struct evdev_pad *d)
{
	if (d->pad)
		d->pad->removed = true;
	int fd = events.get_fd(d->dev);
	events.free(d->dev);
	close(fd);
	free(d);
}

static bool
attr_is(struct udev_device *device, const char *name, const char *value)
{
	const char *attr = devices.device_get_sysattr_value(device, name);
	return attr && strcmp(attr, value) == 0;
}

static void
attach(struct st_controllers *hub, struct udev_device *event)
{
	struct udev_device *usb = devices.device_get_parent_with_subsystem_devtype(event, "usb", "usb_interface");
	if (!usb || !attr_is(usb, "bInterfaceClass", "ff") || !attr_is(usb, "bInterfaceSubClass", "5d")
	    || !attr_is(usb, "bInterfaceProtocol", "01"))
		return;

	dev_t device = devices.device_get_devnum(event);
	for (struct evdev_pad *d = hub->evdev->pads; d; d = d->next)
		if (d->device == device)
			return;
	const char *path = devices.device_get_devnode(event);
	if (!path)
		return;
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return;
	struct libevdev *dev;
	if (events.new_from_fd(fd, &dev) < 0) {
		close(fd);
		return;
	}

	size_t            targets = st_controller_target_count();
	struct evdev_pad *d       = calloc(1, sizeof(*d) + targets * sizeof(*d->bindings));
	if (!d) {
		events.free(dev);
		close(fd);
		return;
	}
	d->dev             = dev;
	d->device          = device;
	struct st_pad *pad = calloc(1, sizeof(*pad));
	if (!pad)
		goto fail;
	pad->fields = calloc(targets, sizeof(*pad->fields));
	if (!pad->fields)
		goto fail;

	/* Linux may present the four XUSB D-pad switches as a pair of hat axes. */
	/* Hash the ordered evdev identities because profiles store bindings by field index. */
	uint64_t layout = UINT64_C(14695981039346656037);
	for (size_t i = 0; i < targets; ++i) {
		const struct st_control_target *t = st_controller_target(i);
		struct evdev_binding b = {.type = t->kind == ST_BUTTON ? EV_KEY : EV_ABS, .code = t->code};
		if (t->kind == ST_HAT)
			continue;
		if (t->code >= BTN_DPAD_UP && t->code <= BTN_DPAD_RIGHT
		    && !events.has_event_code(dev, EV_KEY, t->code)) {
			b.type     = EV_ABS;
			b.code     = t->code <= BTN_DPAD_DOWN ? ABS_HAT0Y : ABS_HAT0X;
			b.hat_sign = t->code == BTN_DPAD_UP || t->code == BTN_DPAD_LEFT ? -1 : 1;
		}
		if (!events.has_event_code(dev, b.type, b.code))
			continue;
		struct pad_field *f = &pad->fields[pad->count];
		f->kind             = t->kind;
		f->target           = (int)i;
		f->minimum          = t->kind == ST_BUTTON ? 0 : events.get_abs_minimum(dev, b.code);
		f->maximum          = t->kind == ST_BUTTON ? 1 : events.get_abs_maximum(dev, b.code);
		snprintf(f->label, sizeof f->label, "%s", t->label);
		d->bindings[pad->count++] = b;
		layout                    = (layout ^ t->kind) * UINT64_C(1099511628211);
		layout                    = (layout ^ t->code) * UINT64_C(1099511628211);
	}
	if (!pad->count)
		goto fail;
	pad->source            = 3;
	pad->device_class      = 2;
	pad->descriptor_fields = pad->count;
	const char *name       = events.get_name(dev);
	snprintf(pad->name, sizeof pad->name, "%s", name ? name : "XUSB gamepad");
	snprintf(pad->profile, sizeof pad->profile, "xusb-%04x-%04x-evdev-%" PRIx64 ".map",
	    (unsigned)events.get_id_vendor(dev), (unsigned)events.get_id_product(dev), layout);
	if (!pad_attach(hub, pad))
		goto fail;
	d->pad           = pad;
	d->next          = hub->evdev->pads;
	hub->evdev->pads = d;
	return;
fail:
	if (pad)
		free(pad->fields);
	free(pad);
	close_pad(d);
}

static void
scan(struct st_controllers *hub)
{
	struct udev_enumerate *list = devices.enumerate_new(hub->evdev->udev);
	if (!list)
		return;
	if (devices.enumerate_add_match_subsystem(list, "input") == 0
	    && devices.enumerate_add_match_sysname(list, "event*") == 0 && devices.enumerate_scan_devices(list) == 0) {
		struct udev_list_entry *entry;
		for (entry = devices.enumerate_get_list_entry(list); entry; entry = devices.list_entry_get_next(entry)) {
			struct udev_device *event =
			    devices.device_new_from_syspath(hub->evdev->udev, devices.list_entry_get_name(entry));
			if (event) {
				attach(hub, event);
				devices.device_unref(event);
			}
		}
	}
	devices.enumerate_unref(list);
}

static void
publish(struct evdev_pad *d, st_controller_emit emit, void *ctx)
{
	for (size_t i = 0; i < d->pad->count; i++) {
		const struct evdev_binding *b     = &d->bindings[i];
		int                         value = events.get_event_value(d->dev, b->type, b->code);
		pad_value(d->pad, i, b->hat_sign ? value == b->hat_sign : value);
	}
	pad_publish(d->pad, emit, ctx);
}

void
pad_evdev_poll(struct st_controllers *hub, bool discover, st_controller_emit emit, void *ctx)
{
	if (discover)
		scan(hub);
	for (struct evdev_pad **link = &hub->evdev->pads; *link;) {
		struct evdev_pad *d      = *link;
		int               status = 0;
		if (!d->pad->fields[0].known)
			publish(d, emit, ctx);
		for (unsigned n = 0; n < 256; n++) {
			struct input_event e;
			status = events.next_event(d->dev,
			    d->syncing ? LIBEVDEV_READ_FLAG_SYNC : LIBEVDEV_READ_FLAG_NORMAL, &e);
			if (status == -EAGAIN && d->syncing) {
				d->syncing = false;
				publish(d, emit, ctx);
				continue;
			}
			if (status < 0)
				break;
			if (status == LIBEVDEV_READ_STATUS_SYNC)
				d->syncing = true;
			else if (e.type == EV_SYN && e.code == SYN_REPORT)
				publish(d, emit, ctx);
		}
		if (status < 0 && status != -EAGAIN && status != -EINTR) {
			*link = d->next;
			close_pad(d);
		}
		else
			link = &d->next;
	}
}

void
pad_evdev_close(struct st_controllers *hub)
{
	while (hub->evdev->pads) {
		struct evdev_pad *d = hub->evdev->pads;
		hub->evdev->pads    = d->next;
		close_pad(d);
	}
	devices.unref(hub->evdev->udev);
	free(hub->evdev);
}
