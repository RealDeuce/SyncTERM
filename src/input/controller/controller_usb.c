/* Portable USB HID and wired XUSB input through libusb-1.0. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>

#include <libusb.h>

#include "controller_internal.h"
#include "st_controller_config.h"

#define ST_API_NAME usb
#define ST_API_PREFIX libusb_
#define ST_API_LINKED ST_DEP_LIBUSB_LINKED
#define ST_API_NAMES ST_CONTROLLER_USB_LIBRARY
#define ST_API_MAJOR ST_CONTROLLER_USB_MAJOR
#define ST_API_CALL LIBUSB_CALL
#define ST_API_FUNCTIONS(X) \
	X(struct libusb_transfer *, alloc_transfer, (int)) \
	X(int, cancel_transfer, (struct libusb_transfer *)) \
	X(int, claim_interface, (libusb_device_handle *, int)) \
	X(void, close, (libusb_device_handle *)) \
	X(int, control_transfer, (libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t)) \
	X(void, exit, (struct libusb_context *)) \
	X(void, free_config_descriptor, (struct libusb_config_descriptor *)) \
	X(void, free_device_list, (libusb_device **, int)) \
	X(void, free_transfer, (struct libusb_transfer *)) \
	X(int, get_active_config_descriptor, (libusb_device *, struct libusb_config_descriptor **)) \
	X(uint8_t, get_bus_number, (libusb_device *)) \
	X(uint8_t, get_device_address, (libusb_device *)) \
	X(int, get_device_descriptor, (libusb_device *, struct libusb_device_descriptor *)) \
	X(ssize_t, get_device_list, (libusb_context *, libusb_device ***)) \
	X(int, get_max_packet_size, (libusb_device *, uint8_t)) \
	X(int, get_string_descriptor_ascii, (libusb_device_handle *, uint8_t, uint8_t *, int)) \
	X(int, handle_events_timeout_completed, (libusb_context *, struct timeval *, int *)) \
	X(int, init, (libusb_context **)) \
	X(int, open, (libusb_device *, libusb_device_handle **)) \
	X(int, release_interface, (libusb_device_handle *, int)) \
	X(int, submit_transfer, (struct libusb_transfer *))
#include "dynamic_library_api.inc"

struct usb_input {
	struct usb_input       *next;
	libusb_device_handle   *handle;
	struct libusb_transfer *transfer;
	struct pad_collections  collections;
	struct st_pad          *xusb;
	uint8_t                 bus, address, interface_number;
	bool                    pending, gone, seen;
	struct usb_provider    *owner;
};

struct usb_provider {
	libusb_context        *usb;
	struct usb_input      *inputs;
	struct st_controllers *hub;
	st_controller_emit     emit;
	void                  *context;
};

bool
pad_usb_init(struct st_controllers *hub)
{
	if (!usb_available())
		return false;
	struct usb_provider *p = calloc(1, sizeof(*p));
	if (!p)
		return false;
	if (usb.init(&p->usb) != 0) {
		free(p);
		return false;
	}
	p->hub   = hub;
	hub->usb = p;
	return true;
}

static void LIBUSB_CALL
received(struct libusb_transfer *transfer)
{
	struct usb_input *d = transfer->user_data;
	d->pending          = false;
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED && !d->gone) {
		if (d->xusb)
			pad_xusb_report(d->xusb, transfer->buffer, (size_t)transfer->actual_length, d->owner->emit,
			    d->owner->context);
		else
			pad_input_collections(&d->collections, transfer->buffer, (size_t)transfer->actual_length,
			    d->owner->emit, d->owner->context);
		d->pending = usb.submit_transfer(transfer) == 0;
	}
	if (!d->pending)
		d->gone = true;
}

static void
close_input(struct usb_input *d)
{
	if (d->xusb)
		d->xusb->removed = true;
	pad_detach_collections(&d->collections);
	if (d->transfer)
		usb.free_transfer(d->transfer);
	usb.release_interface(d->handle, d->interface_number);
	usb.close(d->handle);
	free(d);
}

static struct usb_input *
open_input(struct usb_provider *p, libusb_device *device, const struct libusb_interface_descriptor *iface,
    const struct libusb_device_descriptor *info)
{
	bool xusb = iface->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC && iface->bInterfaceSubClass == 0x5d
	            && iface->bInterfaceProtocol == 1;
	bool hid =
	    !p->hub->hid_ready && iface->bInterfaceClass == LIBUSB_CLASS_HID && iface->bInterfaceSubClass != 1;
	if ((!hid && !xusb) || iface->bAlternateSetting != 0)
		return NULL;
	const struct libusb_endpoint_descriptor *endpoint = NULL;
	for (unsigned i = 0; i < iface->bNumEndpoints; i++) {
		const struct libusb_endpoint_descriptor *e = &iface->endpoint[i];
		if ((e->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_INTERRUPT
		    && (e->bEndpointAddress & LIBUSB_ENDPOINT_IN)) {
			endpoint = e;
			break;
		}
	}
	if (!endpoint)
		return NULL;
	libusb_device_handle *handle = NULL;
	if (usb.open(device, &handle) != 0)
		return NULL;
	/* Read the report descriptor on endpoint zero before claiming the
	 * interface, so an unrelated HID device never loses its owner. */
	unsigned char descriptor[4096];
	int           size = 0;
	if (hid) {
		size                 = usb.control_transfer(handle, 0x81, LIBUSB_REQUEST_GET_DESCRIPTOR, 0x2200,
		    iface->bInterfaceNumber, descriptor, sizeof descriptor, 100);
		unsigned       count = 0;
		struct st_pad *probe = size > 0 ? pad_parse(descriptor, (size_t)size, 0, &count) : NULL;
		if (probe) {
			free(probe->fields);
			free(probe);
		}
		if (!count) {
			usb.close(handle);
			return NULL;
		}
	}
	if (usb.claim_interface(handle, iface->bInterfaceNumber) != 0) {
		usb.close(handle);
		return NULL;
	}
	struct usb_input *d = calloc(1, sizeof(*d));
	if (!d) {
		usb.release_interface(handle, iface->bInterfaceNumber);
		usb.close(handle);
		return NULL;
	}
	d->handle               = handle;
	d->owner                = p;
	d->bus                  = usb.get_bus_number(device);
	d->address              = usb.get_device_address(device);
	d->interface_number     = iface->bInterfaceNumber;
	d->seen                 = true;
	unsigned char name[128] = {0};
	if (info->iProduct
	    && usb.get_string_descriptor_ascii(handle, info->iProduct, name, sizeof name - 1) < 0)
		name[0] = 0;
	if (xusb) {
		d->xusb = pad_xusb_attach(p->hub, info->idVendor, info->idProduct, (const char *)name, true);
		if (!d->xusb)
			goto fail;
	}
	else if (!pad_attach_collections(p->hub, &d->collections, descriptor, (size_t)size, info->idVendor,
		     info->idProduct, (const char *)name))
		goto fail;
	d->transfer = usb.alloc_transfer(0);
	if (!d->transfer)
		goto fail;
	int capacity = usb.get_max_packet_size(device, endpoint->bEndpointAddress);
	if (capacity <= 0)
		goto fail;
	if ((size_t)capacity < d->collections.report_size)
		capacity = (int)d->collections.report_size;
	unsigned char *buffer = malloc((size_t)capacity);
	if (!buffer)
		goto fail;
	/* Some libusb SDKs inline the fill helper and others export it. */
	d->transfer->dev_handle = handle;
	d->transfer->endpoint = endpoint->bEndpointAddress;
	d->transfer->type = LIBUSB_TRANSFER_TYPE_INTERRUPT;
	d->transfer->timeout = 0;
	d->transfer->buffer = buffer;
	d->transfer->length = capacity;
	d->transfer->callback = received;
	d->transfer->user_data = d;
	d->transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER;
	d->pending         = usb.submit_transfer(d->transfer) == 0;
	if (d->pending)
		return d;
fail:
	close_input(d);
	return NULL;
}

static void
scan(struct usb_provider *p)
{
	libusb_device **list;
	ssize_t         count = usb.get_device_list(p->usb, &list);
	if (count < 0)
		return;
	for (struct usb_input *d = p->inputs; d; d = d->next)
		d->seen = false;
	for (ssize_t i = 0; i < count; i++) {
		struct libusb_config_descriptor *config;
		struct libusb_device_descriptor  info;
		if (usb.get_device_descriptor(list[i], &info) != 0
		    || usb.get_active_config_descriptor(list[i], &config) != 0)
			continue;
		for (unsigned j = 0; j < config->bNumInterfaces; j++) {
			if (!config->interface[j].num_altsetting)
				continue;
			const struct libusb_interface_descriptor *iface = &config->interface[j].altsetting[0];
			struct usb_input                         *d     = p->inputs;
			/* Enumeration may allocate a fresh libusb_device for an open device. */
			while (d
			       && (d->bus != usb.get_bus_number(list[i])
			           || d->address != usb.get_device_address(list[i])
			           || d->interface_number != iface->bInterfaceNumber))
				d = d->next;
			if (d)
				d->seen = true;
			else if ((d = open_input(p, list[i], iface, &info)) != NULL) {
				d->next   = p->inputs;
				p->inputs = d;
			}
		}
		usb.free_config_descriptor(config);
	}
	usb.free_device_list(list, 1);
}

static void
cancel(struct usb_input *d)
{
	if (!d->gone && d->pending)
		(void)usb.cancel_transfer(d->transfer);
	d->gone = true;
}

void
pad_usb_poll(struct st_controllers *hub, bool discover, st_controller_emit emit, void *ctx)
{
	struct usb_provider *p = hub->usb;
	if (discover)
		scan(p);
	p->emit             = emit;
	p->context          = ctx;
	struct timeval zero = {0};
	(void)usb.handle_events_timeout_completed(p->usb, &zero, NULL);
	p->emit    = NULL;
	p->context = NULL;
	for (struct usb_input **link = &p->inputs; *link;) {
		struct usb_input *d = *link;
		if (!d->seen)
			cancel(d);
		if (!d->pending) {
			*link = d->next;
			close_input(d);
		}
		else
			link = &d->next;
	}
}

void
pad_usb_close(struct st_controllers *hub)
{
	struct usb_provider *p = hub->usb;
	for (struct usb_input *d = p->inputs; d; d = d->next)
		cancel(d);
	while (p->inputs) {
		struct usb_input *d = p->inputs;
		if (d->pending) {
			struct timeval timeout = {0, 50000};
			(void)usb.handle_events_timeout_completed(p->usb, &timeout, NULL);
		}
		else {
			p->inputs = d->next;
			close_input(d);
		}
	}
	usb.exit(p->usb);
	free(p);
}
