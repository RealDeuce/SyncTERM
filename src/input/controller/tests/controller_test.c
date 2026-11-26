/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller_internal.h"

#define CHECK(expr)                                                   \
	do {                                                          \
		if (!(expr)) {                                        \
			fprintf(stderr, "%d: %s\n", __LINE__, #expr); \
			return 1;                                     \
		}                                                     \
	} while (0)

static int
scaling(void)
{
	struct pad_field f = {.minimum = 0, .maximum = 255};
	CHECK(pad_scale(&f, ST_STICK) == -32768);
	f.raw = 128;
	CHECK(pad_scale(&f, ST_STICK) == 0 && pad_scale(&f, ST_TRIGGER) == 16448);
	f.inverted = true;
	CHECK(pad_scale(&f, ST_STICK) == 0);
	f.raw = 255;
	CHECK(pad_scale(&f, ST_STICK) == -32768 && pad_scale(&f, ST_TRIGGER) == 0);
	f.minimum = INT32_MIN;
	f.maximum = INT32_MAX;
	f.raw     = INT32_MIN;
	CHECK(pad_scale(&f, ST_STICK) == 32767 && pad_scale(&f, ST_TRIGGER) == 32767);
	f.raw = INT32_MAX;
	CHECK(pad_scale(&f, ST_STICK) == -32768);
	f.inverted = false;
	f.raw      = 0;
	CHECK(pad_scale(&f, ST_STICK) == 0);
	return 0;
}

static const uint8_t descriptor[] = {0x05, 1, 0x09, 5, 0xa1, 1, 0x85, 3, 0x05, 9, 0x09, 7, 0x09, 7, 0x15, 0, 0x25,
    1, 0x75, 1, 0x95, 2, 0x81, 2, 0x75, 6, 0x95, 1, 0x81, 3, 0x05, 1, 0x09, 0x30, 0x26, 0xff, 0, 0x75, 8, 0x81, 2,
    0x09, 0x39, 0x25, 7, 0x75, 4, 0x81, 0x42, 0xc0};

struct events {
	struct st_controller_event records[128];
	size_t                     count;
};

static void
record(void *context, const struct st_controller_event *e)
{
	struct events *events = context;
	if (events->count >= 128)
		abort();
	events->records[events->count++] = *e;
}

static int
mapping(void)
{
	struct st_controllers *hub = pad_create(NULL, NULL, NULL);
	CHECK(hub);
	struct pad_collections a = {0}, b = {0};
	CHECK(pad_attach_collections(hub, &a, descriptor, sizeof descriptor, 10, 20, "Test"));
	CHECK(pad_attach_collections(hub, &b, descriptor, sizeof descriptor, 10, 20, "Test"));
	struct st_pad *p = a.pads[0], *twin = b.pads[0];
	CHECK(p->descriptor_fields == 4 && p->count == 8 && p->id != twin->id && p->fields[0].target == -1
	      && p->fields[1].target == -1);
	CHECK(strcmp(st_controller_field(p, 0), "Button 7 #1") == 0);
	CHECK(strcmp(st_controller_field(p, 1), "Button 7 #2") == 0);
	CHECK(strcmp(st_controller_field(p, 2), "X axis") == 0);
	struct events out = {0};
	pad_publish(p, record, &out);
	CHECK(out.count == 5 && out.records[0].record == ST_PAD_ADD && out.records[4].record == ST_PAD_FRAME);
	uint8_t sample[] = {3, 1, 128, 8};
	pad_input_collections(&a, sample, sizeof sample, record, &out);
	CHECK(out.count == 9 && out.records[5].value == 0 && out.records[8].record == ST_PAD_FRAME);
	st_controller_arm(p, -1);
	int value;
	sample[2] = 132;
	pad_report(p, sample, sizeof sample, true);
	CHECK(st_controller_actuation(p, &value) == -1);
	sample[1] = 3;
	pad_report(p, sample, sizeof sample, true);
	CHECK(st_controller_actuation(p, &value) == 1 && value == 1);
	int south = pad_target(1, 304);
	CHECK(st_controller_bind(hub, p, 1, south, false, false) == 0 && twin->fields[1].target == south);
	out.count = 0;
	pad_publish(p, record, &out);
	CHECK(out.records[0].record == ST_PAD_REMOVE && out.records[1].record == ST_PAD_ADD);
	CHECK(out.records[2].type == 1 && out.records[2].code == 304);
	CHECK(st_controller_bind(hub, p, 0, south, false, false) == 0 && twin->fields[1].target == -1);
	CHECK(st_controller_bind(hub, p, 0, pad_target(3, 0), false, false) < 0);
	st_controller_arm(p, -1);
	sample[3] = 3;
	pad_report(p, sample, sizeof sample, true);
	CHECK(st_controller_actuation(p, &value) == 3 && value == 3);
	st_controller_arm(p, -1);
	sample[2] = 255;
	pad_report(p, sample, sizeof sample, true);
	CHECK(st_controller_actuation(p, &value) == 2 && value == 32767);
	st_controller_arm(p, -1);
	sample[2] = 128;
	pad_report(p, sample, sizeof sample, true);
	CHECK(st_controller_actuation(p, &value) == -1);
	sample[2] = 0;
	pad_report(p, sample, sizeof sample - 1, true);
	CHECK(st_controller_actuation(p, &value) == -1);
	pad_report(p, sample, sizeof sample, true);
	CHECK(st_controller_actuation(p, &value) == 2 && value == -32768);
	CHECK(st_controller_bind(hub, p, 2, pad_target(3, 2), true, false) == 0);
	CHECK(st_controller_preview(p, 2, &value) && value == 32767);
	pad_detach_collections(&a);
	pad_detach_collections(&b);
	st_controllers_destroy(hub);
	return 0;
}

static int
composite(void)
{
	/* A keyboard's nested gamepad usage still belongs to the keyboard.
	 * The following two application collections share its report layout. */
	static const uint8_t descriptor[] = {5, 1, 9, 6, 0xa1, 1, 9, 5, 0xa1, 1, 0x15, 0, 0x25, 127, 0x75, 8, 0x95,
	    1, 9, 0x30, 0x81, 2, 0xc0, 0xc0, 9, 5, 0xa1, 1, 9, 0x30, 0x81, 2, 0xc0, 9, 4, 0xa1, 1, 9, 0x31, 0x81,
	    2, 0xc0};
	struct st_controllers *hub        = pad_create(NULL, NULL, NULL);
	struct pad_collections set        = {0};
	CHECK(hub && pad_attach_collections(hub, &set, descriptor, sizeof descriptor, 1, 2, NULL));
	CHECK(set.count == 2 && set.report_size == 3 && set.pads[0]->device_class == 2
	      && set.pads[1]->device_class == 1);
	const uint8_t sample[] = {42, 123, 45};
	pad_input_collections(&set, sample, sizeof sample, NULL, NULL);
	CHECK(set.pads[0]->fields[0].raw == 123 && set.pads[1]->fields[0].raw == 45);
	pad_detach_collections(&set);
	st_controllers_destroy(hub);
	return 0;
}

static int
repeated_vendor_usage(void)
{
	/* A vendor can repeat one usage across sixteen one-bit inputs. */
	static const uint8_t desc[] = {5, 1, 9, 4, 0xa1, 1, 6, 0, 0xff, 0x15, 0, 0x25, 1, 0x75, 1, 0x95, 16, 9, 1,
	    0x81, 2, 0xc0};
	unsigned             collections;
	struct st_pad       *p = pad_parse(desc, sizeof desc, 0, &collections);
	CHECK(p && collections == 1 && st_controller_fields(p) == 16);

	const uint8_t report[] = {0x01, 0x80};
	pad_report(p, report, sizeof report, false);
	for (size_t i = 0; i < 16; ++i) {
		char label[64];
		snprintf(label, sizeof label, "Vendor Defined FF00:0001 #%zu", i + 1);
		CHECK(strcmp(st_controller_field(p, i), label) == 0);
		int value;
		CHECK(st_controller_preview(p, i, &value) && value == (i == 0 || i == 15));
	}

	free(p->fields);
	free(p);
	return 0;
}

static int
button_array(void)
{
	/* Two selector slots, button usages 0 (empty) through 5, trailing padding. */
	static const uint8_t desc[] = {5, 1, 9, 4, 0xa1, 1, 5, 9, 0x19, 0, 0x29, 5, 0x15, 0, 0x25, 5, 0x75, 4,
	    0x95, 2, 0x81, 0, 0x75, 8, 0x95, 1, 0x81, 3, 0xc0};
	unsigned             count;
	struct st_pad       *p = pad_parse(desc, sizeof desc, 0, &count);
	CHECK(p && count == 1 && p->count == 5);
	uint8_t data[] = {0x25, 0};
	pad_report(p, data, 1, false);
	CHECK(!p->fields[0].known);
	pad_report(p, data, sizeof data, false);
	CHECK(p->fields[0].known && !p->fields[0].raw && p->fields[1].raw && p->fields[4].raw);
	data[0] = 0xf1;
	pad_report(p, data, sizeof data, false);
	CHECK(p->fields[0].raw && !p->fields[1].raw && !p->fields[4].raw);
	free(p->fields);
	free(p);
	return 0;
}

static int
xusb(void)
{
	struct st_controllers *hub = pad_create(NULL, NULL, NULL);
	CHECK(hub);
	struct st_pad *p = pad_xusb_attach(hub, 0x1234, 0x5678, "Test pad", true);
	CHECK(p && st_controller_fields(p) == 21);
	uint8_t       report[20] = {0, 20, 0x81, 0x14, 128, 255, 0, 128, 255, 127, 255, 127, 0, 128};
	struct events out        = {0};
	pad_xusb_report(p, report, 19, record, &out);
	report[0] = 1;
	pad_xusb_report(p, report, sizeof report, record, &out);
	report[0] = 0;
	report[1] = 3;
	pad_xusb_report(p, report, sizeof report, record, &out);
	CHECK(!out.count);
	report[1] = 20;
	pad_xusb_report(p, report, sizeof report, record, &out);
	CHECK(out.count == 44 && out.records[0].source == 3 && out.records[43].record == ST_PAD_FRAME);
	for (size_t i = 22; i < 43; ++i) {
		const struct st_controller_event *e = &out.records[i];
		CHECK(e->record == ST_PAD_VALUE);
		int expected = e->type == 1
		                   ? (e->code == 544 || e->code == 318 || e->code == 316 || e->code == 304)
		               : e->code == 2                   ? 16448
		               : (e->code == 0 || e->code == 1) ? -32768
		                                                : 32767;
		CHECK(e->value == expected);
	}
	out.count = 0;
	pad_xusb_report(p, report, sizeof report, record, &out);
	CHECK(!out.count);
	memset(report + 2, 0, 18);
	pad_xusb_report(p, report, sizeof report, record, &out);
	CHECK(out.count == 11);
	for (size_t i = 0; i + 1 < out.count; ++i)
		CHECK(out.records[i].value == 0);
	st_controllers_destroy(hub);
	return 0;
}

static struct st_port_sample next_sample;
static bool                  pending;

static bool
next_port(struct st_port_sample *s)
{
	if (!pending)
		return false;
	*s      = next_sample;
	pending = false;
	return true;
}

static int
ports(void)
{
	struct st_controllers *hub = st_controllers_create(NULL, next_port);
	CHECK(hub && st_controllers_available(hub));
	struct events out = {0};
	next_sample       = (struct st_port_sample) {.port = 2, .generation = 1, .present = true, .analog = true};
	pending           = true;
	st_controllers_poll(hub, record, &out);
	CHECK(out.count == 46 && out.records[0].source == 2 && out.records[0].device_class == 2);
	next_sample.buttons     = 1 | 256;
	next_sample.sticks[0]   = INT16_MIN;
	next_sample.triggers[0] = INT16_MAX;
	out.count               = 0;
	pending                 = true;
	st_controllers_poll(hub, record, &out);
	CHECK(out.count == 5 && out.records[0].code == 304 && out.records[1].code == 305
	      && out.records[2].value == -32768 && out.records[3].value == 32767
	      && out.records[4].record == ST_PAD_FRAME);
	next_sample.present = false;
	pending             = true;
	st_controllers_poll(hub, record, &out);
	CHECK(out.count == 6 && out.records[5].record == ST_PAD_REMOVE);
	st_controllers_destroy(hub);
	return 0;
}

int
main(void)
{
	return scaling() || mapping() || composite() || repeated_vendor_usage() || button_array() || xusb()
	       || ports();
}
