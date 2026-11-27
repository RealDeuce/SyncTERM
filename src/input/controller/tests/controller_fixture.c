/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>

#include "controller_fixture.h"
#include "controller_internal.h"
#include "io_error.h"

struct request {
	struct request *next;
	uint8_t         action, slot;
	size_t          size;
	uint8_t         data[];
};

struct fixture_pad {
	struct pad_collections hid;
	struct st_pad         *xusb;
};

static void
detach(struct fixture_pad *slot)
{
	pad_detach_collections(&slot->hid);
	if (slot->xusb)
		slot->xusb->removed = true;
	slot->xusb = NULL;
}

struct fixture {
	struct request    *head, *tail;
	struct fixture_pad slot[4];
};

static void
fixture_poll(struct st_controllers *hub, st_controller_emit emit, void *context)
{
	struct fixture *f = hub->interfaces;
	while (f->head) {
		struct request         *r    = f->head;
		struct fixture_pad     *slot = &f->slot[r->slot];
		struct pad_collections *set  = &slot->hid;
		if (r->action == 1) {
			detach(slot);
			pad_attach_collections(hub, set, r->data, r->size, 0x1234, 0x4321, "Fixture gamepad");
		}
		else if (r->action == 5) {
			detach(slot);
			slot->xusb = pad_xusb_attach(hub, 0x1234, 0x4321, "Fixture XUSB gamepad", true);
		}
		else if (r->action == 2 && slot->xusb)
			pad_xusb_report(slot->xusb, r->data, r->size, emit, context);
		else if (r->action == 2)
			pad_input_collections(set, r->data, r->size, emit, context);
		else if (r->action == 3)
			detach(slot);
		else {
			struct st_pad *pad    = slot->xusb ? slot->xusb : set->count ? set->pads[0] : NULL;
			int            target = pad_target(r->data[1], r->data[2] | (unsigned)r->data[3] << 8);
			if (pad && target >= 0)
				st_controller_bind(hub, pad, r->data[0], target, false, false);
		}
		f->head = r->next;
		free(r);
	}
	f->tail = NULL;
}

static void
fixture_close(struct st_controllers *hub)
{
	struct fixture *f = hub->interfaces;
	fixture_poll(hub, NULL, NULL);
	for (size_t i = 0; i < 4; i++)
		detach(&f->slot[i]);
	free(f);
}

static bool
fixture_init(struct st_controllers *hub)
{
	hub->interfaces = calloc(1, sizeof(struct fixture));
	return hub->interfaces != NULL;
}

struct st_controllers *
st_controller_fixture_create(const char *settings)
{
	static const struct pad_driver driver = {fixture_init, fixture_poll, fixture_close};
	return pad_create(settings, NULL, &driver);
}

int
st_controller_fixture_request(void *context, const uint8_t *bytes, size_t size)
{
	struct st_controllers *hub = context;
	if (!st_controllers_available(hub) || size < 2 || size > 4098 || bytes[0] < 1 || bytes[0] > 5 || bytes[1] >= 4
	    || (bytes[0] == 4 && size != 6) || (bytes[0] == 5 && size != 2))
		return IO_ERROR_INVALID_ARGUMENT;
	struct fixture *f = hub->interfaces;
	struct request *r = malloc(sizeof(*r) + size - 2);
	if (!r)
		return IO_ERROR_OUT_OF_MEMORY;
	*r = (struct request) {.action = bytes[0], .slot = bytes[1], .size = size - 2};
	memcpy(r->data, bytes + 2, r->size);
	if (f->tail)
		f->tail->next = r;
	else
		f->head = r;
	f->tail = r;
	return 0;
}
