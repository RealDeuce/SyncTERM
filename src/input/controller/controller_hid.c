/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include <hidapi.h>

#include "controller_internal.h"
#include "st_controller_config.h"
#if ST_CONTROLLER_DARWIN_HID
 #include <hidapi_darwin.h>
#endif

#include "st_utf8.h"

#if ST_CONTROLLER_DARWIN_HID
 #define ST_HID_DARWIN_FUNCTIONS(X) X(void, darwin_set_open_exclusive, (int))
#else
 #define ST_HID_DARWIN_FUNCTIONS(X)
#endif
#define ST_API_NAME hid
#define ST_API_PREFIX hid_
#define ST_API_LINKED ST_DEP_HIDAPI_LINKED
#define ST_API_NAMES ST_CONTROLLER_HID_NAMES
#define ST_API_MAJOR 0
#define ST_API_CALL HID_API_CALL
#define ST_API_FUNCTIONS(X) \
	X(void, close, (hid_device *)) \
	X(struct hid_device_info *, enumerate, (unsigned short, unsigned short)) \
	X(int, exit, (void)) \
	X(void, free_enumeration, (struct hid_device_info *)) \
	X(int, get_report_descriptor, (hid_device *, unsigned char *, size_t)) \
	X(int, init, (void)) \
	X(hid_device *, open_path, (const char *)) \
	X(int, read_timeout, (hid_device *, unsigned char *, size_t, int)) \
	X(int, set_nonblocking, (hid_device *, int)) \
	ST_HID_DARWIN_FUNCTIONS(X)
#include "dynamic_library_api.inc"

struct hid_interface {
	struct hid_interface  *next;
	hid_device            *handle;
	char                  *path;
	bool                   seen;
	struct pad_collections collections;
	uint8_t               *report;
};

bool
pad_hid_init(struct st_controllers *hub)
{
	(void)hub;
	if (!hid_available())
		return false;
	if (hid.init() != 0)
		return false;
#if ST_CONTROLLER_DARWIN_HID
	hid.darwin_set_open_exclusive(0);
#endif
	return true;
}

static void
close_interface(struct hid_interface *dev)
{
	pad_detach_collections(&dev->collections);
	free(dev->path);
	if (dev->handle)
		hid.close(dev->handle);
	free(dev->report);
	free(dev);
}

/* HIDAPI uses UTF-16 on Windows and Unicode scalar values on Unix. */
static void
product_name(char *text, size_t capacity, const wchar_t *name)
{
	size_t length = 0;
	while (name && *name) {
		uint32_t ch = (uint32_t)*name++;
		if (ch >= 0xd800 && ch <= 0xdbff && (uint32_t)*name >= 0xdc00 && (uint32_t)*name <= 0xdfff)
			ch = 0x10000 + (ch - 0xd800) * 1024 + (uint32_t)*name++ - 0xdc00;
		ptrdiff_t bytes = st_utf8_encode_one(text + length, capacity - length - 1, ch);
		if (bytes <= 0 || (size_t)bytes >= capacity - length)
			break;
		length += (size_t)bytes;
	}
	text[length] = 0;
}

static struct hid_interface *
open_interface(struct st_controllers *hub, const struct hid_device_info *info)
{
	struct hid_interface *dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;
	dev->path = malloc(strlen(info->path) + 1);
	if (!dev->path)
		goto fail;
	strcpy(dev->path, info->path);
	dev->handle = hid.open_path(info->path);
	if (!dev->handle || hid.set_nonblocking(dev->handle, 1) != 0)
		goto fail;
	unsigned char descriptor[HID_API_MAX_REPORT_DESCRIPTOR_SIZE];
	int           size = hid.get_report_descriptor(dev->handle, descriptor, sizeof descriptor);
	if (size <= 0)
		goto fail;
	char product[96];
	product_name(product, sizeof product, info->product_string);
	if (!pad_attach_collections(hub, &dev->collections, descriptor, (size_t)size, info->vendor_id,
		info->product_id, product))
		goto fail;
	dev->report = malloc(dev->collections.report_size);
	if (!dev->report)
		goto fail;
	dev->seen = true;
	return dev;
fail:
	close_interface(dev);
	return NULL;
}

static void
scan(struct st_controllers *hub)
{
	struct hid_interface *devices = hub->interfaces;
	for (struct hid_interface *d = devices; d; d = d->next)
		d->seen = false;
	struct hid_device_info *list = hid.enumerate(0, 0);
	for (struct hid_device_info *i = list; i; i = i->next) {
		if (!i->path || i->bus_type != HID_API_BUS_USB
		    || (i->usage_page != 1 || (i->usage != 4 && i->usage != 5)))
			continue;
#if ST_CONTROLLER_XINPUT
		/* XInput devices also have a compatibility HID collection on Windows. */
		if (strstr(i->path, "&ig_") || strstr(i->path, "&IG_"))
			continue;
#endif
		struct hid_interface *d = devices;
		while (d && strcmp(d->path, i->path))
			d = d->next;
		if (d)
			d->seen = true;
		else if ((d = open_interface(hub, i)) != NULL) {
			d->next = devices;
			devices = d;
		}
	}
	hid.free_enumeration(list);
	hub->interfaces = devices;
}

void
pad_hid_poll(struct st_controllers *hub, bool discover, st_controller_emit emit, void *ctx)
{
	if (discover)
		scan(hub);
	struct hid_interface *devices = hub->interfaces;
	for (struct hid_interface **link = &devices; *link;) {
		struct hid_interface *d = *link;
		/* Bound work per interface so a busy controller cannot starve terminal I/O. */
		for (int n = 0; d->seen && n < 64; n++) {
			int size = hid.read_timeout(d->handle, d->report, d->collections.report_size, 0);
			if (size < 0)
				d->seen = false;
			if (size <= 0)
				break;
			pad_input_collections(&d->collections, d->report, (size_t)size, emit, ctx);
		}
		if (!d->seen) {
			*link = d->next;
			close_interface(d);
		}
		else
			link = &d->next;
	}
	hub->interfaces = devices;
}

void
pad_hid_close(struct st_controllers *hub)
{
	while (hub->interfaces) {
		struct hid_interface *d = hub->interfaces;
		hub->interfaces         = d->next;
		close_interface(d);
	}
	hid.exit();
}
