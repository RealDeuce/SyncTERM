/* Portable USB HID input through libusb-1.0. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>

#include <libusb.h>

#include "controller_internal.h"
#include "st_time.h"

struct usb_input {
	struct usb_input       *next;
	libusb_device_handle   *handle;
	struct libusb_transfer *transfer;
	struct pad_collections  collections;
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
pad_native_init(struct st_controllers *hub)
{
	struct usb_provider *p = calloc(1, sizeof(*p));
	if (!p)
		return false;
	if (libusb_init(&p->usb) != 0) {
		free(p);
		return false;
	}
	p->hub          = hub;
	hub->interfaces = p;
	return true;
}

static void LIBUSB_CALL
received(struct libusb_transfer *transfer)
{
	struct usb_input *d = transfer->user_data;
	d->pending          = false;
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED && !d->gone) {
		pad_input_collections(&d->collections, transfer->buffer, (size_t)transfer->actual_length,
		    d->owner->emit, d->owner->context);
		d->pending = libusb_submit_transfer(transfer) == 0;
	}
	if (!d->pending)
		d->gone = true;
}

static void
close_input(struct usb_input *d)
{
	pad_detach_collections(&d->collections);
	if (d->transfer)
		libusb_free_transfer(d->transfer);
	libusb_release_interface(d->handle, d->interface_number);
	libusb_close(d->handle);
	free(d);
}

static struct usb_input *
open_input(struct usb_provider *p, libusb_device *device, const struct libusb_interface_descriptor *iface,
    const struct libusb_device_descriptor *info)
{
	if (iface->bInterfaceClass != LIBUSB_CLASS_HID || iface->bInterfaceSubClass == 1
	    || iface->bAlternateSetting != 0)
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
	if (libusb_open(device, &handle) != 0)
		return NULL;
	/* Read the report descriptor on endpoint zero before claiming the
	 * interface, so an unrelated HID device never loses its owner. */
	unsigned char  descriptor[4096];
	int            size  = libusb_control_transfer(handle, 0x81, LIBUSB_REQUEST_GET_DESCRIPTOR, 0x2200,
	    iface->bInterfaceNumber, descriptor, sizeof descriptor, 100);
	unsigned       count = 0;
	struct st_pad *probe = size > 0 ? pad_parse(descriptor, (size_t)size, 0, &count) : NULL;
	if (probe) {
		free(probe->fields);
		free(probe);
	}
	if (!count || libusb_claim_interface(handle, iface->bInterfaceNumber) != 0) {
		libusb_close(handle);
		return NULL;
	}
	struct usb_input *d = calloc(1, sizeof(*d));
	if (!d) {
		libusb_release_interface(handle, iface->bInterfaceNumber);
		libusb_close(handle);
		return NULL;
	}
	d->handle               = handle;
	d->owner                = p;
	d->bus                  = libusb_get_bus_number(device);
	d->address              = libusb_get_device_address(device);
	d->interface_number     = iface->bInterfaceNumber;
	d->seen                 = true;
	unsigned char name[128] = {0};
	if (info->iProduct
	    && libusb_get_string_descriptor_ascii(handle, info->iProduct, name, sizeof name - 1) < 0)
		name[0] = 0;
	if (!pad_attach_collections(p->hub, &d->collections, descriptor, (size_t)size, info->idVendor,
		info->idProduct, (const char *)name))
		goto fail;
	d->transfer = libusb_alloc_transfer(0);
	if (!d->transfer)
		goto fail;
	int capacity = libusb_get_max_packet_size(device, endpoint->bEndpointAddress);
	if (capacity <= 0)
		goto fail;
	if ((size_t)capacity < d->collections.report_size)
		capacity = (int)d->collections.report_size;
	unsigned char *buffer = malloc((size_t)capacity);
	if (!buffer)
		goto fail;
	libusb_fill_interrupt_transfer(d->transfer, handle, endpoint->bEndpointAddress, buffer, capacity, received,
	    d, 0);
	d->transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER;
	d->pending         = libusb_submit_transfer(d->transfer) == 0;
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
	ssize_t         count = libusb_get_device_list(p->usb, &list);
	if (count < 0)
		return;
	for (struct usb_input *d = p->inputs; d; d = d->next)
		d->seen = false;
	for (ssize_t i = 0; i < count; i++) {
		struct libusb_config_descriptor *config;
		struct libusb_device_descriptor  info;
		if (libusb_get_device_descriptor(list[i], &info) != 0
		    || libusb_get_active_config_descriptor(list[i], &config) != 0)
			continue;
		for (unsigned j = 0; j < config->bNumInterfaces; j++) {
			if (!config->interface[j].num_altsetting)
				continue;
			const struct libusb_interface_descriptor *iface = &config->interface[j].altsetting[0];
			struct usb_input                         *d     = p->inputs;
			/* Enumeration may allocate a fresh libusb_device for an open device. */
			while (d
			       && (d->bus != libusb_get_bus_number(list[i])
			           || d->address != libusb_get_device_address(list[i])
			           || d->interface_number != iface->bInterfaceNumber))
				d = d->next;
			if (d)
				d->seen = true;
			else if ((d = open_input(p, list[i], iface, &info)) != NULL) {
				d->next   = p->inputs;
				p->inputs = d;
			}
		}
		libusb_free_config_descriptor(config);
	}
	libusb_free_device_list(list, 1);
}

static void
cancel(struct usb_input *d)
{
	if (!d->gone && d->pending)
		(void)libusb_cancel_transfer(d->transfer);
	d->gone = true;
}

void
pad_native_poll(struct st_controllers *hub, st_controller_emit emit, void *ctx)
{
	struct usb_provider *p   = hub->interfaces;
	uint64_t             now = 0;
	(void)st_time_monotonic_ms(&now);
	if (!hub->next_scan || now >= hub->next_scan) {
		scan(p);
		hub->next_scan = now + 1000;
	}
	p->emit             = emit;
	p->context          = ctx;
	struct timeval zero = {0};
	(void)libusb_handle_events_timeout_completed(p->usb, &zero, NULL);
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
pad_native_close(struct st_controllers *hub)
{
	struct usb_provider *p = hub->interfaces;
	for (struct usb_input *d = p->inputs; d; d = d->next)
		cancel(d);
	while (p->inputs) {
		struct usb_input *d = p->inputs;
		if (d->pending) {
			struct timeval timeout = {0, 50000};
			(void)libusb_handle_events_timeout_completed(p->usb, &timeout, NULL);
		}
		else {
			p->inputs = d->next;
			close_input(d);
		}
	}
	libusb_exit(p->usb);
	free(p);
}
